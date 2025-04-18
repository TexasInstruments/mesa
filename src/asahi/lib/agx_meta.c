/*
 * Copyright 2022 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "agx_meta.h"
#include "agx_compile.h"
#include "agx_device.h" /* for AGX_MEMORY_TYPE_SHADER */
#include "agx_tilebuffer.h"
#include "nir_builder.h"

static struct agx_meta_shader *
agx_compile_meta_shader(struct agx_meta_cache *cache, nir_shader *shader,
                        struct agx_shader_key *key,
                        struct agx_tilebuffer_layout *tib)
{
   struct util_dynarray binary;
   util_dynarray_init(&binary, NULL);

   agx_preprocess_nir(shader, false, NULL);
   if (tib) {
      unsigned bindless_base = 0;
      agx_nir_lower_tilebuffer(shader, tib, NULL, &bindless_base, NULL);
      agx_nir_lower_monolithic_msaa(
         shader, &(struct agx_msaa_state){.nr_samples = tib->nr_samples});
   }

   struct agx_meta_shader *res = rzalloc(cache->ht, struct agx_meta_shader);
   agx_compile_shader_nir(shader, key, NULL, &binary, &res->info);

   res->ptr = agx_pool_upload_aligned_with_bo(&cache->pool, binary.data,
                                              binary.size, 128, &res->bo);
   util_dynarray_fini(&binary);
   ralloc_free(shader);

   return res;
}

static nir_ssa_def *
build_background_op(nir_builder *b, enum agx_meta_op op, unsigned rt,
                    unsigned nr, bool msaa)
{
   if (op == AGX_META_OP_LOAD) {
      nir_tex_instr *tex = nir_tex_instr_create(b->shader, msaa ? 2 : 1);
      /* The type doesn't matter as long as it matches the store */
      tex->dest_type = nir_type_uint32;
      tex->sampler_dim = msaa ? GLSL_SAMPLER_DIM_MS : GLSL_SAMPLER_DIM_2D;
      tex->op = msaa ? nir_texop_txf_ms : nir_texop_txf;
      tex->src[0] = nir_tex_src_for_ssa(nir_tex_src_coord,
                                        nir_u2u32(b, nir_load_pixel_coord(b)));

      if (msaa) {
         tex->src[1] =
            nir_tex_src_for_ssa(nir_tex_src_ms_index, nir_load_sample_id(b));
         b->shader->info.fs.uses_sample_shading = true;
      }

      tex->coord_components = 2;
      tex->texture_index = rt;
      nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32);
      nir_builder_instr_insert(b, &tex->instr);

      return nir_trim_vector(b, &tex->dest.ssa, nr);
   } else {
      assert(op == AGX_META_OP_CLEAR);

      return nir_load_preamble(b, nr, 32, 4 + (rt * 8));
   }
}

static struct agx_meta_shader *
agx_build_background_shader(struct agx_meta_cache *cache,
                            struct agx_meta_key *key)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_FRAGMENT, &agx_nir_options, "agx_background");
   b.shader->info.fs.untyped_color_outputs = true;

   struct agx_shader_key compiler_key = {
      .fs.ignore_tib_dependencies = true,
      .fs.nr_samples = key->tib.nr_samples,
      .reserved_preamble = key->reserved_preamble,
   };

   for (unsigned rt = 0; rt < ARRAY_SIZE(key->op); ++rt) {
      if (key->op[rt] == AGX_META_OP_NONE)
         continue;

      unsigned nr = util_format_get_nr_components(key->tib.logical_format[rt]);
      bool msaa = key->tib.nr_samples > 1;
      assert(nr > 0);

      nir_store_output(&b, build_background_op(&b, key->op[rt], rt, nr, msaa),
                       nir_imm_int(&b, 0), .write_mask = BITFIELD_MASK(nr),
                       .src_type = nir_type_uint32,
                       .io_semantics.location = FRAG_RESULT_DATA0 + rt,
                       .io_semantics.num_slots = 1);

      b.shader->info.outputs_written |= BITFIELD64_BIT(FRAG_RESULT_DATA0 + rt);
   }

   return agx_compile_meta_shader(cache, b.shader, &compiler_key, &key->tib);
}

static struct agx_meta_shader *
agx_build_end_of_tile_shader(struct agx_meta_cache *cache,
                             struct agx_meta_key *key)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE,
                                                  &agx_nir_options, "agx_eot");

   enum glsl_sampler_dim dim =
      (key->tib.nr_samples > 1) ? GLSL_SAMPLER_DIM_MS : GLSL_SAMPLER_DIM_2D;

   for (unsigned rt = 0; rt < ARRAY_SIZE(key->op); ++rt) {
      if (key->op[rt] == AGX_META_OP_NONE)
         continue;

      /* The end-of-tile shader is unsuitable to handle spilled render targets.
       * Skip them. If blits are needed with spilled render targets, other parts
       * of the driver need to implement them.
       */
      if (key->tib.spilled[rt])
         continue;

      assert(key->op[rt] == AGX_META_OP_STORE);
      unsigned offset_B = agx_tilebuffer_offset_B(&key->tib, rt);

      nir_block_image_store_agx(
         &b, nir_imm_int(&b, rt), nir_imm_intN_t(&b, offset_B, 16),
         .format = agx_tilebuffer_physical_format(&key->tib, rt),
         .image_dim = dim);
   }

   struct agx_shader_key compiler_key = {
      .reserved_preamble = key->reserved_preamble,
   };

   return agx_compile_meta_shader(cache, b.shader, &compiler_key, NULL);
}

struct agx_meta_shader *
agx_get_meta_shader(struct agx_meta_cache *cache, struct agx_meta_key *key)
{
   struct hash_entry *ent = _mesa_hash_table_search(cache->ht, key);
   if (ent)
      return ent->data;

   struct agx_meta_shader *ret = NULL;

   for (unsigned rt = 0; rt < ARRAY_SIZE(key->op); ++rt) {
      if (key->op[rt] == AGX_META_OP_STORE) {
         ret = agx_build_end_of_tile_shader(cache, key);
         break;
      }
   }

   if (!ret)
      ret = agx_build_background_shader(cache, key);

   ret->key = *key;
   _mesa_hash_table_insert(cache->ht, &ret->key, ret);
   return ret;
}

static uint32_t
key_hash(const void *key)
{
   return _mesa_hash_data(key, sizeof(struct agx_meta_key));
}

static bool
key_compare(const void *a, const void *b)
{
   return memcmp(a, b, sizeof(struct agx_meta_key)) == 0;
}

void
agx_meta_init(struct agx_meta_cache *cache, struct agx_device *dev)
{
   agx_pool_init(&cache->pool, dev, AGX_BO_EXEC | AGX_BO_LOW_VA, true);
   cache->ht = _mesa_hash_table_create(NULL, key_hash, key_compare);
}

void
agx_meta_cleanup(struct agx_meta_cache *cache)
{
   agx_pool_cleanup(&cache->pool);
   _mesa_hash_table_destroy(cache->ht, NULL);
   cache->ht = NULL;
}
