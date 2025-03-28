/*
 * Copyright 2021 Alyssa Rosenzweig
 * Copyright 2020 Collabora Ltd.
 * Copyright 2016 Broadcom
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_builtin_builder.h"
#include "agx_compiler.h"
#include "agx_internal_formats.h"
#include "agx_nir.h"
#include "nir_builder_opcodes.h"
#include "nir_intrinsics.h"
#include "nir_intrinsics_indices.h"

#define AGX_TEXTURE_DESC_STRIDE   24
#define AGX_FORMAT_RGB32_EMULATED 0x36
#define AGX_LAYOUT_LINEAR         0x0

static nir_ssa_def *
texture_descriptor_ptr_for_handle(nir_builder *b, nir_ssa_def *handle)
{
   /* Bindless handles are a vec2, where the first source is the (constant)
    * uniform register number and the second source is the byte offset.
    */
   nir_ssa_scalar uniform = nir_ssa_scalar_resolved(handle, 0);
   unsigned uniform_idx = nir_ssa_scalar_as_uint(uniform);

   nir_ssa_def *base = nir_load_preamble(b, 1, 64, uniform_idx);
   nir_ssa_def *offset = nir_u2u64(b, nir_channel(b, handle, 1));

   return nir_iadd(b, base, offset);
}

static nir_ssa_def *
texture_descriptor_ptr_for_index(nir_builder *b, nir_ssa_def *index)
{
   return nir_iadd(
      b, nir_load_texture_base_agx(b),
      nir_u2u64(b, nir_imul_imm(b, index, AGX_TEXTURE_DESC_STRIDE)));
}

static nir_ssa_def *
texture_descriptor_ptr(nir_builder *b, nir_tex_instr *tex)
{
   int handle_idx = nir_tex_instr_src_index(tex, nir_tex_src_texture_handle);
   if (handle_idx >= 0)
      return texture_descriptor_ptr_for_handle(b, tex->src[handle_idx].src.ssa);

   /* For non-bindless, compute from the texture index */
   nir_ssa_def *index;

   int offs_idx = nir_tex_instr_src_index(tex, nir_tex_src_texture_offset);
   if (offs_idx >= 0)
      index = tex->src[offs_idx].src.ssa;
   else
      index = nir_imm_int(b, tex->texture_index);

   return texture_descriptor_ptr_for_index(b, index);
}

/* Implement txs for buffer textures. There is no mipmapping to worry about, so
 * this is just a uniform pull. However, we lower buffer textures to 2D so the
 * original size is irrecoverable. Instead, we stash it in the "Acceleration
 * buffer" field, which is unused for linear images. Fetch just that.
 */
static nir_ssa_def *
agx_txs_buffer(nir_builder *b, nir_ssa_def *descriptor)
{
   nir_ssa_def *size_ptr = nir_iadd_imm(b, descriptor, 16);

   return nir_load_global_constant(b, size_ptr, 8, 1, 32);
}

static nir_ssa_def *
agx_txs(nir_builder *b, nir_tex_instr *tex)
{
   nir_ssa_def *ptr = texture_descriptor_ptr(b, tex);
   nir_ssa_def *comp[4] = {NULL};

   if (tex->sampler_dim == GLSL_SAMPLER_DIM_BUF)
      return agx_txs_buffer(b, ptr);

   nir_ssa_def *desc = nir_load_global_constant(b, ptr, 8, 4, 32);
   nir_ssa_def *w0 = nir_channel(b, desc, 0);
   nir_ssa_def *w1 = nir_channel(b, desc, 1);
   nir_ssa_def *w3 = nir_channel(b, desc, 3);

   /* Width minus 1: bits [28, 42) */
   nir_ssa_def *width_m1 =
      nir_extr_agx(b, w0, w1, nir_imm_int(b, 28), nir_imm_int(b, 14));

   /* Height minus 1: bits [42, 56) */
   nir_ssa_def *height_m1 = nir_ubitfield_extract_imm(b, w1, 42 - 32, 14);

   /* Depth minus 1: bits [110, 124) */
   nir_ssa_def *depth_m1 = nir_ubitfield_extract_imm(b, w3, 110 - 96, 14);

   /* First level: bits [56, 60) */
   nir_ssa_def *lod = nir_ubitfield_extract_imm(b, w1, 56 - 32, 4);

   /* Add LOD offset to first level to get the interesting LOD */
   int lod_idx = nir_tex_instr_src_index(tex, nir_tex_src_lod);
   if (lod_idx >= 0) {
      lod = nir_iadd(
         b, lod, nir_u2u32(b, nir_ssa_for_src(b, tex->src[lod_idx].src, 1)));
   }

   if (tex->sampler_dim == GLSL_SAMPLER_DIM_2D && tex->is_array) {
      /* Linear 2D arrays are special and have their depth in the next word,
       * since the depth read above is actually the stride for linear. We handle
       * this case specially.
       *
       * TODO: Optimize this, since linear 2D arrays aren't needed for APIs and
       * this just gets used internally for blits.
       */
      nir_ssa_def *layout = nir_ubitfield_extract_imm(b, w0, 4, 2);

      /* Get the 2 bytes after the first 128-bit descriptor */
      nir_ssa_def *extension =
         nir_load_global_constant(b, nir_iadd_imm(b, ptr, 16), 8, 1, 16);

      nir_ssa_def *depth_linear_m1 =
         nir_iand_imm(b, extension, BITFIELD_MASK(11));

      depth_linear_m1 = nir_u2uN(b, depth_linear_m1, depth_m1->bit_size);

      depth_m1 = nir_bcsel(b, nir_ieq_imm(b, layout, AGX_LAYOUT_LINEAR),
                           depth_linear_m1, depth_m1);
   }

   /* Add 1 to width-1, height-1 to get base dimensions */
   nir_ssa_def *width = nir_iadd_imm(b, width_m1, 1);
   nir_ssa_def *height = nir_iadd_imm(b, height_m1, 1);
   nir_ssa_def *depth = nir_iadd_imm(b, depth_m1, 1);

   /* 1D Arrays have their second component as the layer count */
   if (tex->sampler_dim == GLSL_SAMPLER_DIM_1D && tex->is_array)
      height = depth;

   /* How we finish depends on the size of the result */
   unsigned nr_comps = nir_dest_num_components(tex->dest);
   assert(nr_comps <= 3);

   /* Adjust for LOD, do not adjust array size */
   assert(!(nr_comps <= 1 && tex->is_array));
   width = nir_imax(b, nir_ushr(b, width, lod), nir_imm_int(b, 1));

   if (!(nr_comps == 2 && tex->is_array))
      height = nir_imax(b, nir_ushr(b, height, lod), nir_imm_int(b, 1));

   if (!(nr_comps == 3 && tex->is_array))
      depth = nir_imax(b, nir_ushr(b, depth, lod), nir_imm_int(b, 1));

   /* Cube maps have equal width and height, we save some instructions by only
    * reading one. Dead code elimination will remove the redundant instructions.
    */
   if (tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE)
      height = width;

   comp[0] = width;
   comp[1] = height;
   comp[2] = depth;

   return nir_vec(b, comp, nr_comps);
}

static bool
lower_txs(nir_builder *b, nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_tex)
      return false;

   nir_tex_instr *tex = nir_instr_as_tex(instr);
   b->cursor = nir_before_instr(instr);

   if (tex->op != nir_texop_txs)
      return false;

   nir_ssa_def *res = agx_txs(b, tex);
   nir_ssa_def_rewrite_uses_after(&tex->dest.ssa, res, instr);
   nir_instr_remove(instr);
   return true;
}

static nir_ssa_def *
format_is_rgb32(nir_builder *b, nir_tex_instr *tex)
{
   nir_ssa_def *ptr = texture_descriptor_ptr(b, tex);
   nir_ssa_def *desc = nir_load_global_constant(b, ptr, 8, 1, 32);
   nir_ssa_def *channels = nir_ubitfield_extract_imm(b, desc, 6, 7);

   return nir_ieq_imm(b, channels, AGX_FORMAT_RGB32_EMULATED);
}

/* Load from an RGB32 buffer texture */
static nir_ssa_def *
load_rgb32(nir_builder *b, nir_tex_instr *tex, nir_ssa_def *coordinate)
{
   /* Base address right-shifted 4: bits [66, 102) */
   nir_ssa_def *ptr_hi = nir_iadd_imm(b, texture_descriptor_ptr(b, tex), 8);
   nir_ssa_def *desc_hi_words = nir_load_global_constant(b, ptr_hi, 8, 2, 32);
   nir_ssa_def *desc_hi = nir_pack_64_2x32(b, desc_hi_words);
   nir_ssa_def *base_shr4 =
      nir_iand_imm(b, nir_ushr_imm(b, desc_hi, 2), BITFIELD64_MASK(36));
   nir_ssa_def *base = nir_ishl_imm(b, base_shr4, 4);

   nir_ssa_def *raw = nir_load_constant_agx(
      b, 3, nir_dest_bit_size(tex->dest), base, nir_imul_imm(b, coordinate, 3),
      .format = AGX_INTERNAL_FORMAT_I32);

   /* Set alpha to 1 (in the appropriate format) */
   bool is_float = nir_alu_type_get_base_type(tex->dest_type) == nir_type_float;

   nir_ssa_def *swizzled[4] = {
      nir_channel(b, raw, 0), nir_channel(b, raw, 1), nir_channel(b, raw, 2),
      is_float ? nir_imm_float(b, 1.0) : nir_imm_int(b, 1)};

   return nir_vec(b, swizzled, nir_tex_instr_dest_size(tex));
}

/*
 * Given a 1D buffer texture coordinate, calculate the 2D coordinate vector that
 * will be used to access the linear 2D texture bound to the buffer.
 */
static nir_ssa_def *
coords_for_buffer_texture(nir_builder *b, nir_ssa_def *coord)
{
   return nir_vec2(b, nir_iand_imm(b, coord, BITFIELD_MASK(10)),
                   nir_ushr_imm(b, coord, 10));
}

/*
 * Buffer textures are lowered to 2D (1024xN) textures in the driver to access
 * more storage. When lowering, we need to fix up the coordinate accordingly.
 *
 * Furthermore, RGB32 formats are emulated by lowering to global memory access,
 * so to read a buffer texture we generate code that looks like:
 *
 *    if (descriptor->format == RGB32)
 *       return ((uint32_t *) descriptor->address)[x];
 *    else
 *       return txf(texture_as_2d, vec2(x % 1024, x / 1024));
 */
static bool
lower_buffer_texture(nir_builder *b, nir_tex_instr *tex)
{
   nir_ssa_def *coord = nir_steal_tex_src(tex, nir_tex_src_coord);

   /* The OpenGL ES 3.2 specification says on page 187:
    *
    *    When a buffer texture is accessed in a shader, the results of a texel
    *    fetch are undefined if the specified texel coordinate is negative, or
    *    greater than or equal to the clamped number of texels in the texture
    *    image.
    *
    * However, faulting would be undesirable for robustness, so clamp.
    */
   nir_ssa_def *size = nir_get_texture_size(b, tex);
   coord = nir_umin(b, coord, nir_iadd_imm(b, size, -1));

   /* Lower RGB32 reads if the format requires */
   nir_if *nif = nir_push_if(b, format_is_rgb32(b, tex));
   nir_ssa_def *rgb32 = load_rgb32(b, tex, coord);
   nir_push_else(b, nif);

   /* Otherwise, lower the texture instruction to read from 2D */
   assert(coord->num_components == 1 && "buffer textures are 1D");
   tex->sampler_dim = GLSL_SAMPLER_DIM_2D;

   nir_ssa_def *coord2d = coords_for_buffer_texture(b, coord);
   nir_instr_remove(&tex->instr);
   nir_builder_instr_insert(b, &tex->instr);
   nir_tex_instr_add_src(tex, nir_tex_src_backend1, nir_src_for_ssa(coord2d));
   nir_block *else_block = nir_cursor_current_block(b->cursor);
   nir_pop_if(b, nif);

   /* Put it together with a phi */
   nir_ssa_def *phi = nir_if_phi(b, rgb32, &tex->dest.ssa);
   nir_ssa_def_rewrite_uses(&tex->dest.ssa, phi);
   nir_phi_instr *phi_instr = nir_instr_as_phi(phi->parent_instr);
   nir_phi_src *else_src = nir_phi_get_src_from_block(phi_instr, else_block);
   nir_instr_rewrite_src_ssa(phi->parent_instr, &else_src->src, &tex->dest.ssa);
   return true;
}

/*
 * NIR indexes into array textures with unclamped floats (integer for txf). AGX
 * requires the index to be a clamped integer. Lower tex_src_coord into
 * tex_src_backend1 for array textures by type-converting and clamping.
 */
static bool
lower_regular_texture(nir_builder *b, nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_tex)
      return false;

   nir_tex_instr *tex = nir_instr_as_tex(instr);
   b->cursor = nir_before_instr(instr);

   if (nir_tex_instr_is_query(tex))
      return false;

   if (tex->sampler_dim == GLSL_SAMPLER_DIM_BUF)
      return lower_buffer_texture(b, tex);

   /* Get the coordinates */
   nir_ssa_def *coord = nir_steal_tex_src(tex, nir_tex_src_coord);
   nir_ssa_def *ms_idx = nir_steal_tex_src(tex, nir_tex_src_ms_index);

   /* It's unclear if mipmapped 1D textures work in the hardware. For now, we
    * always lower to 2D.
    */
   if (tex->sampler_dim == GLSL_SAMPLER_DIM_1D) {
      /* Add a zero Y component to the coordinate */
      if (tex->is_array) {
         assert(coord->num_components == 2);
         coord = nir_vec3(b, nir_channel(b, coord, 0),
                          nir_imm_intN_t(b, 0, coord->bit_size),
                          nir_channel(b, coord, 1));
      } else {
         assert(coord->num_components == 1);
         coord = nir_vec2(b, coord, nir_imm_intN_t(b, 0, coord->bit_size));
      }

      /* Add a zero Y component to other sources */
      nir_tex_src_type other_srcs[] = {
         nir_tex_src_ddx,
         nir_tex_src_ddy,
         nir_tex_src_offset,
      };

      for (unsigned i = 0; i < ARRAY_SIZE(other_srcs); ++i) {
         nir_ssa_def *src = nir_steal_tex_src(tex, other_srcs[i]);

         if (!src)
            continue;

         assert(src->num_components == 1);
         src = nir_vec2(b, src, nir_imm_intN_t(b, 0, src->bit_size));
         nir_tex_instr_add_src(tex, other_srcs[i], nir_src_for_ssa(src));
      }

      tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
      tex->coord_components++;
   }

   /* The layer is always the last component of the NIR coordinate, split it off
    * because we'll need to swizzle.
    */
   nir_ssa_def *layer = NULL;

   if (tex->is_array) {
      unsigned lidx = coord->num_components - 1;
      nir_ssa_def *unclamped_layer = nir_channel(b, coord, lidx);
      coord = nir_trim_vector(b, coord, lidx);

      /* Round layer to nearest even */
      if (tex->op != nir_texop_txf && tex->op != nir_texop_txf_ms)
         unclamped_layer = nir_f2u32(b, nir_fround_even(b, unclamped_layer));

      /* Clamp to max layer = (# of layers - 1) for out-of-bounds handling.
       * Layer must be 16-bits for the hardware, drop top bits after clamping.
       */
      nir_ssa_def *txs = nir_get_texture_size(b, tex);
      nir_ssa_def *nr_layers = nir_channel(b, txs, lidx);
      nir_ssa_def *max_layer = nir_iadd_imm(b, nr_layers, -1);
      layer = nir_u2u16(b, nir_umin(b, unclamped_layer, max_layer));
   }

   /* Combine layer and multisample index into 32-bit so we don't need a vec5 or
    * vec6 16-bit coordinate tuple, which would be inconvenient in NIR for
    * little benefit (a minor optimization, I guess).
    */
   nir_ssa_def *sample_array = (ms_idx && layer)
                                  ? nir_pack_32_2x16_split(b, ms_idx, layer)
                               : ms_idx ? nir_u2u32(b, ms_idx)
                               : layer  ? nir_u2u32(b, layer)
                                        : NULL;

   /* Combine into the final 32-bit tuple */
   if (sample_array != NULL) {
      unsigned end = coord->num_components;
      coord = nir_pad_vector(b, coord, end + 1);
      coord = nir_vector_insert_imm(b, coord, sample_array, end);
   }

   nir_tex_instr_add_src(tex, nir_tex_src_backend1, nir_src_for_ssa(coord));

   /* Furthermore, if there is an offset vector, it must be packed */
   nir_ssa_def *offset = nir_steal_tex_src(tex, nir_tex_src_offset);

   if (offset != NULL) {
      nir_ssa_def *packed = NULL;

      for (unsigned c = 0; c < offset->num_components; ++c) {
         nir_ssa_def *nibble = nir_iand_imm(b, nir_channel(b, offset, c), 0xF);
         nir_ssa_def *shifted = nir_ishl_imm(b, nibble, 4 * c);

         if (packed != NULL)
            packed = nir_ior(b, packed, shifted);
         else
            packed = shifted;
      }

      nir_tex_instr_add_src(tex, nir_tex_src_backend2, nir_src_for_ssa(packed));
   }

   return true;
}

static nir_ssa_def *
bias_for_tex(nir_builder *b, nir_tex_instr *tex)
{
   nir_instr *instr = nir_get_texture_size(b, tex)->parent_instr;
   nir_tex_instr *query = nir_instr_as_tex(instr);

   query->op = nir_texop_lod_bias_agx;
   query->dest_type = nir_type_float16;

   nir_ssa_dest_init(instr, &query->dest, 1, 16);
   return &query->dest.ssa;
}

static bool
lower_sampler_bias(nir_builder *b, nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_tex)
      return false;

   nir_tex_instr *tex = nir_instr_as_tex(instr);
   b->cursor = nir_before_instr(instr);

   switch (tex->op) {
   case nir_texop_tex: {
      tex->op = nir_texop_txb;
      nir_tex_instr_add_src(tex, nir_tex_src_bias,
                            nir_src_for_ssa(bias_for_tex(b, tex)));
      return true;
   }

   case nir_texop_txb:
   case nir_texop_txl: {
      nir_tex_src_type src =
         tex->op == nir_texop_txl ? nir_tex_src_lod : nir_tex_src_bias;

      nir_ssa_def *orig = nir_steal_tex_src(tex, src);
      assert(orig != NULL && "invalid NIR");

      if (orig->bit_size != 16)
         orig = nir_f2f16(b, orig);

      nir_tex_instr_add_src(
         tex, src, nir_src_for_ssa(nir_fadd(b, orig, bias_for_tex(b, tex))));
      return true;
   }

   case nir_texop_txd: {
      /* For txd, the computed level-of-detail is log2(rho)
       * where rho should scale proportionally to all
       * derivatives. So scale derivatives by exp2(bias) to
       * get level-of-detail log2(exp2(bias) * rho) = bias + log2(rho).
       */
      nir_ssa_def *scale = nir_fexp2(b, nir_f2f32(b, bias_for_tex(b, tex)));
      nir_tex_src_type src[] = {nir_tex_src_ddx, nir_tex_src_ddy};

      for (unsigned s = 0; s < ARRAY_SIZE(src); ++s) {
         nir_ssa_def *orig = nir_steal_tex_src(tex, src[s]);
         assert(orig != NULL && "invalid");

         nir_ssa_def *scaled = nir_fmul(b, nir_f2f32(b, orig), scale);
         nir_tex_instr_add_src(tex, src[s], nir_src_for_ssa(scaled));
      }

      return true;
   }

   case nir_texop_txf:
   case nir_texop_txf_ms:
   case nir_texop_txs:
   case nir_texop_tg4:
   case nir_texop_texture_samples:
   case nir_texop_samples_identical:
      /* These operations do not use a sampler */
      return false;

   default:
      unreachable("Unhandled texture operation");
   }
}

static bool
legalize_image_lod(nir_builder *b, nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   nir_src *src;

#define CASE(op, idx)                                                          \
   case nir_intrinsic_##op:                                                    \
   case nir_intrinsic_bindless_##op:                                           \
      src = &intr->src[idx];                                                   \
      break;

   switch (intr->intrinsic) {
      CASE(image_load, 3)
      CASE(image_store, 4)
      CASE(image_size, 1)
   default:
      return false;
   }

#undef CASE

   if (src->ssa->bit_size == 16)
      return false;

   b->cursor = nir_before_instr(instr);
   nir_src_rewrite_ssa(src, nir_i2i16(b, src->ssa));
   return true;
}

static nir_ssa_def *
txs_for_image(nir_builder *b, nir_intrinsic_instr *intr,
              unsigned num_components, unsigned bit_size)
{
   bool bindless = intr->intrinsic == nir_intrinsic_bindless_image_size;

   nir_tex_instr *tex = nir_tex_instr_create(b->shader, 1 + (int)bindless);
   tex->op = nir_texop_txs;
   tex->is_array = nir_intrinsic_image_array(intr);
   tex->dest_type = nir_type_uint32;
   tex->sampler_dim = nir_intrinsic_image_dim(intr);

   unsigned s = 0;
   tex->src[s++] = nir_tex_src_for_ssa(nir_tex_src_lod, intr->src[1].ssa);

   if (bindless) {
      tex->src[s++] =
         nir_tex_src_for_ssa(nir_tex_src_texture_handle, intr->src[0].ssa);
   } else if (nir_src_is_const(intr->src[0])) {
      tex->texture_index = nir_src_as_uint(intr->src[0]);
   } else {
      tex->src[s++] =
         nir_tex_src_for_ssa(nir_tex_src_texture_offset, intr->src[0].ssa);
   }

   nir_ssa_dest_init(&tex->instr, &tex->dest, num_components, bit_size);
   nir_builder_instr_insert(b, &tex->instr);
   return &tex->dest.ssa;
}

static nir_ssa_def *
nir_bitfield_mask(nir_builder *b, nir_ssa_def *x)
{
   nir_ssa_def *one = nir_imm_intN_t(b, 1, x->bit_size);
   return nir_iadd_imm(b, nir_ishl(b, one, nir_u2u32(b, x)), -1);
}

static nir_ssa_def *
calculate_twiddled_coordinates(nir_builder *b, nir_ssa_def *coord,
                               nir_ssa_def *tile_w_px_log2,
                               nir_ssa_def *tile_h_px_log2,
                               nir_ssa_def *width_tl,
                               nir_ssa_def *layer_stride_el)
{
   /* SIMD-within-a-register */
   nir_ssa_def *coord_px = nir_pack_32_2x16(b, nir_u2u16(b, coord));
   nir_ssa_def *tile_mask =
      nir_pack_32_2x16_split(b, nir_bitfield_mask(b, tile_w_px_log2),
                             nir_bitfield_mask(b, tile_h_px_log2));

   /* Modulo by the tile width/height to get the offsets within the tile */
   nir_ssa_def *offs_xy_px = nir_iand(b, coord_px, tile_mask);

   /* Get the coordinates of the corner of the tile */
   nir_ssa_def *tile_xy_px = nir_isub(b, coord_px, offs_xy_px);

   /* Unpack SIMD-within-a-register */
   nir_ssa_def *offs_x_px = nir_unpack_32_2x16_split_x(b, offs_xy_px);
   nir_ssa_def *offs_y_px = nir_unpack_32_2x16_split_y(b, offs_xy_px);
   nir_ssa_def *tile_x_px =
      nir_u2u32(b, nir_unpack_32_2x16_split_x(b, tile_xy_px));
   nir_ssa_def *tile_y_px =
      nir_u2u32(b, nir_unpack_32_2x16_split_y(b, tile_xy_px));

   /* Get the tile size */
   nir_ssa_def *one_32 = nir_imm_int(b, 1);
   nir_ssa_def *tile_w_px = nir_ishl(b, one_32, nir_u2u32(b, tile_w_px_log2));
   nir_ssa_def *tile_h_px = nir_ishl(b, one_32, nir_u2u32(b, tile_h_px_log2));

   /* tile row start (px) =
    *   (y // tile height) * (# of tiles/row) * (# of pix/tile) =
    *   align_down(y, tile height) / tile height * width_tl *tile width *
    *        tile height =
    *   align_down(y, tile height) * width_tl * tile width
    */
   nir_ssa_def *tile_row_start_px =
      nir_imul(b, nir_u2u32(b, tile_y_px), nir_imul(b, width_tl, tile_w_px));

   /* tile column start (px) =
    *   (x // tile width) * (# of pix/tile) =
    *   align(x, tile width) / tile width * tile width * tile height =
    *   align(x, tile width) * tile height
    */
   nir_ssa_def *tile_col_start_px = nir_imul(b, tile_x_px, tile_h_px);

   /* The pixel at which the tile starts is thus... */
   nir_ssa_def *tile_offset_px =
      nir_iadd(b, tile_row_start_px, tile_col_start_px);

   /* Get the total offset */
   nir_ssa_def *offs_px = nir_interleave_agx(b, offs_x_px, offs_y_px);
   nir_ssa_def *total_px = nir_iadd(b, tile_offset_px, nir_u2u32(b, offs_px));

   if (layer_stride_el) {
      nir_ssa_def *layer = nir_channel(b, coord, 2);
      nir_ssa_def *layer_offset_px = nir_imul(b, layer, layer_stride_el);
      total_px = nir_iadd(b, total_px, layer_offset_px);
   }

   return total_px;
}

static nir_ssa_def *
image_texel_address(nir_builder *b, nir_intrinsic_instr *intr,
                    bool return_index)
{
   /* First, calculate the address of the PBE descriptor */
   nir_ssa_def *desc_address;
   if (intr->intrinsic == nir_intrinsic_bindless_image_texel_address ||
       intr->intrinsic == nir_intrinsic_bindless_image_store)
      desc_address = texture_descriptor_ptr_for_handle(b, intr->src[0].ssa);
   else
      desc_address = texture_descriptor_ptr_for_index(b, intr->src[0].ssa);

   nir_ssa_def *coord = intr->src[1].ssa;

   enum glsl_sampler_dim dim = nir_intrinsic_image_dim(intr);
   bool layered = nir_intrinsic_image_array(intr) ||
                  (nir_intrinsic_image_dim(intr) == GLSL_SAMPLER_DIM_CUBE) ||
                  (nir_intrinsic_image_dim(intr) == GLSL_SAMPLER_DIM_3D);

   /* The last 8 bytes of the 24-byte PBE descriptor contain either the
    * software-defined atomic descriptor, or (if array image) a pointer to the
    * descriptor. Grab it.
    */
   nir_ssa_def *meta_ptr = nir_iadd_imm(b, desc_address, 16);
   nir_ssa_def *meta = nir_load_global_constant(b, meta_ptr, 8, 1, 64);
   nir_ssa_def *layer_stride_el = NULL;

   if (layered) {
      nir_ssa_def *desc = nir_load_global_constant(b, meta, 8, 3, 32);
      meta = nir_pack_64_2x32(b, nir_trim_vector(b, desc, 2));
      layer_stride_el = nir_channel(b, desc, 2);
   }

   nir_ssa_def *meta_hi = nir_unpack_64_2x32_split_y(b, meta);

   /* See the GenXML definitions of the software-defined atomic descriptors */
   nir_ssa_def *base;

   if (dim == GLSL_SAMPLER_DIM_BUF)
      base = meta;
   else
      base = nir_ishl_imm(b, nir_iand_imm(b, meta, BITFIELD64_MASK(33)), 7);

   nir_ssa_def *tile_w_px_log2 =
      nir_u2u16(b, nir_ubitfield_extract_imm(b, meta_hi, 33 - 32, 3));
   nir_ssa_def *tile_h_px_log2 =
      nir_u2u16(b, nir_ubitfield_extract_imm(b, meta_hi, 36 - 32, 3));
   nir_ssa_def *width_tl = nir_ubitfield_extract_imm(b, meta_hi, 39 - 32, 14);

   /* We do not allow atomics on linear 2D or linear 2D arrays, as there are no
    * known use cases. So, we're linear if buffer or 1D, and twiddled otherwise.
    */
   nir_ssa_def *total_px;
   if (dim == GLSL_SAMPLER_DIM_BUF || dim == GLSL_SAMPLER_DIM_1D) {
      /* 1D linear is indexed directly */
      total_px = nir_channel(b, coord, 0);
   } else {
      total_px = calculate_twiddled_coordinates(
         b, coord, tile_w_px_log2, tile_h_px_log2, width_tl, layer_stride_el);
   }

   nir_ssa_def *total_sa;

   if (dim == GLSL_SAMPLER_DIM_MS) {
      nir_ssa_def *sample_idx = intr->src[2].ssa;
      nir_ssa_def *samples_log2 =
         nir_ubitfield_extract_imm(b, meta_hi, 54 - 32, 2);

      total_sa = nir_iadd(b, nir_ishl(b, total_px, samples_log2), sample_idx);
   } else {
      total_sa = total_px /* * 1 sa/px */;
   }

   /* Early return if we just want a linearized texel index */
   if (return_index)
      return total_sa;

   /* Calculate the full texel address. This sequence is written carefully to
    * ensure it will be entirely folded into the atomic's addressing arithmetic.
    */
   enum pipe_format format = nir_intrinsic_format(intr);
   unsigned bytes_per_sample_B = util_format_get_blocksize(format);

   nir_ssa_def *total_B = nir_imul_imm(b, total_sa, bytes_per_sample_B);
   return nir_iadd(b, base, nir_u2u64(b, total_B));
}

static bool
lower_buffer_image(nir_builder *b, nir_intrinsic_instr *intr)
{
   if (nir_intrinsic_image_dim(intr) != GLSL_SAMPLER_DIM_BUF)
      return false;

   nir_ssa_def *coord_vector = intr->src[1].ssa;
   nir_ssa_def *coord = nir_channel(b, coord_vector, 0);

   /* Lower the buffer load/store to a 2D image load/store, matching the 2D
    * texture/PBE descriptor the driver supplies for buffer images.
    */
   nir_ssa_def *coord2d = coords_for_buffer_texture(b, coord);
   nir_src_rewrite_ssa(&intr->src[1], nir_pad_vector(b, coord2d, 4));
   nir_intrinsic_set_image_dim(intr, GLSL_SAMPLER_DIM_2D);
   return true;
}

static bool
lower_images(nir_builder *b, nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   b->cursor = nir_before_instr(instr);

   switch (intr->intrinsic) {
   case nir_intrinsic_image_load:
   case nir_intrinsic_image_store:
   case nir_intrinsic_bindless_image_load:
   case nir_intrinsic_bindless_image_store:
      return lower_buffer_image(b, intr);

   case nir_intrinsic_image_size:
   case nir_intrinsic_bindless_image_size:
      nir_ssa_def_rewrite_uses(
         &intr->dest.ssa,
         txs_for_image(b, intr, nir_dest_num_components(intr->dest),
                       nir_dest_bit_size(intr->dest)));
      return true;

   case nir_intrinsic_image_texel_address:
   case nir_intrinsic_bindless_image_texel_address:
      nir_ssa_def_rewrite_uses(&intr->dest.ssa,
                               image_texel_address(b, intr, false));
      return true;

   default:
      return false;
   }
}

bool
agx_nir_lower_texture(nir_shader *s, bool support_lod_bias)
{
   bool progress = false;

   nir_lower_tex_options lower_tex_options = {
      .lower_txp = ~0,
      .lower_invalid_implicit_lod = true,
      .lower_tg4_offsets = true,
      .lower_index_to_offset = true,

      /* XXX: Metal seems to handle just like 3D txd, so why doesn't it work?
       * TODO: Stop using this lowering
       */
      .lower_txd_cube_map = true,
   };

   nir_tex_src_type_constraints tex_constraints = {
      [nir_tex_src_lod] = {true, 16},
      [nir_tex_src_bias] = {true, 16},
      [nir_tex_src_ms_index] = {true, 16},
      [nir_tex_src_texture_offset] = {true, 16},
      [nir_tex_src_sampler_offset] = {true, 16},
   };

   NIR_PASS(progress, s, nir_lower_tex, &lower_tex_options);

   /* Insert fences before lowering image atomics, since image atomics need
    * different fencing than other image operations.
    */
   NIR_PASS(progress, s, agx_nir_fence_images);

   NIR_PASS(progress, s, nir_lower_image_atomics_to_global);

   /* Lower bias after nir_lower_tex (to get rid of txd) but before
    * lower_regular_texture (which will shuffle around the sources)
    */
   if (support_lod_bias) {
      NIR_PASS(progress, s, nir_shader_instructions_pass, lower_sampler_bias,
               nir_metadata_block_index | nir_metadata_dominance, NULL);
   }

   NIR_PASS(progress, s, nir_shader_instructions_pass, legalize_image_lod,
            nir_metadata_block_index | nir_metadata_dominance, NULL);
   NIR_PASS(progress, s, nir_shader_instructions_pass, lower_images,
            nir_metadata_block_index | nir_metadata_dominance, NULL);
   NIR_PASS(progress, s, nir_legalize_16bit_sampler_srcs, tex_constraints);

   /* Lower texture sources after legalizing types (as the lowering depends on
    * 16-bit multisample indices) but before lowering queries (as the lowering
    * generates txs for array textures).
    */
   NIR_PASS(progress, s, nir_shader_instructions_pass, lower_regular_texture,
            nir_metadata_block_index | nir_metadata_dominance, NULL);
   NIR_PASS(progress, s, nir_shader_instructions_pass, lower_txs,
            nir_metadata_block_index | nir_metadata_dominance, NULL);

   return progress;
}

static bool
lower_multisampled_store(nir_builder *b, nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   b->cursor = nir_before_instr(instr);

   if (intr->intrinsic != nir_intrinsic_image_store &&
       intr->intrinsic != nir_intrinsic_bindless_image_store)
      return false;

   if (nir_intrinsic_image_dim(intr) != GLSL_SAMPLER_DIM_MS)
      return false;

   nir_ssa_def *index_px = image_texel_address(b, intr, true);
   nir_ssa_def *coord2d = coords_for_buffer_texture(b, index_px);

   nir_src_rewrite_ssa(&intr->src[1], nir_pad_vector(b, coord2d, 4));
   nir_intrinsic_set_image_dim(intr, GLSL_SAMPLER_DIM_2D);
   return true;
}

bool
agx_nir_lower_multisampled_image_store(nir_shader *s)
{
   return nir_shader_instructions_pass(
      s, lower_multisampled_store,
      nir_metadata_block_index | nir_metadata_dominance, NULL);
}
