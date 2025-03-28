/*
 * Copyright 2021 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "agx_compiler.h"

/* Binary patches needed for branch offsets */
struct agx_branch_fixup {
   /* Offset into the binary to patch */
   off_t offset;

   /* Value to patch with will be block->offset */
   agx_block *block;
};

static void
assert_register_is_aligned(agx_index reg)
{
   assert(reg.type == AGX_INDEX_REGISTER);

   switch (reg.size) {
   case AGX_SIZE_16:
      return;
   case AGX_SIZE_32:
      assert((reg.value & 1) == 0 && "unaligned reg");
      return;
   case AGX_SIZE_64:
      assert((reg.value & 3) == 0 && "unaligned reg");
      return;
   }

   unreachable("Invalid register size");
}

/* Texturing has its own operands */
static unsigned
agx_pack_sample_coords(agx_index index, bool *flag, bool *is_16)
{
   /* TODO: Do we have a use case for 16-bit coords? */
   assert(index.size == AGX_SIZE_32);
   assert(index.value < 0x100);

   *is_16 = false;
   *flag = index.discard;
   return index.value;
}

static unsigned
agx_pack_texture(agx_index base, agx_index index, unsigned *packed_base,
                 unsigned *flag)
{
   if (base.type == AGX_INDEX_IMMEDIATE) {
      assert(base.value == 0);

      /* Texture state registers */
      *packed_base = 0;

      if (index.type == AGX_INDEX_REGISTER) {
         assert(index.size == AGX_SIZE_16);
         *flag = 1;
      } else {
         assert(index.type == AGX_INDEX_IMMEDIATE);
         *flag = 0;
      }
   } else {
      assert(base.type == AGX_INDEX_UNIFORM);
      assert(base.size == AGX_SIZE_64);
      assert((base.value & 3) == 0);
      assert(index.size == AGX_SIZE_32);

      /* Bindless */
      *packed_base = base.value >> 2;
      *flag = 3;
   }

   return index.value;
}

static unsigned
agx_pack_sampler(agx_index index, bool *flag)
{
   if (index.type == AGX_INDEX_REGISTER) {
      assert(index.size == AGX_SIZE_16);
      *flag = 1;
   } else {
      assert(index.type == AGX_INDEX_IMMEDIATE);
      *flag = 0;
   }

   return index.value;
}

static unsigned
agx_pack_sample_compare_offset(agx_index index)
{
   if (index.type == AGX_INDEX_NULL)
      return 0;

   assert(index.size == AGX_SIZE_32);
   assert(index.value < 0x100);
   assert_register_is_aligned(index);
   return index.value;
}

static unsigned
agx_pack_lod(agx_index index, unsigned *lod_mode)
{
   /* For automatic LOD, the LOD field is unused. Assert as much. */
   if ((*lod_mode) == AGX_LOD_MODE_AUTO_LOD) {
      assert(index.type == AGX_INDEX_IMMEDIATE);
      assert(index.value == 0);
      return 0;
   }

   if (index.type == AGX_INDEX_UNIFORM) {
      /* Translate LOD mode from register mode to uniform mode */
      assert(((*lod_mode) & BITFIELD_BIT(2)) && "must start as reg mode");
      *lod_mode = (*lod_mode) & ~BITFIELD_BIT(2);
      assert(index.value < 0x200);
   } else {
      /* Otherwise must be registers */
      assert(index.type == AGX_INDEX_REGISTER);
      assert(index.value < 0x100);
   }

   return index.value;
}

static unsigned
agx_pack_pbe_source(agx_index index, bool *flag)
{
   assert(index.size == AGX_SIZE_16 || index.size == AGX_SIZE_32);
   assert_register_is_aligned(index);

   *flag = (index.size == AGX_SIZE_32);
   return index.value;
}

static unsigned
agx_pack_pbe_lod(agx_index index, bool *flag)
{
   assert(index.size == AGX_SIZE_16);

   if (index.type == AGX_INDEX_IMMEDIATE)
      *flag = true;
   else if (index.type == AGX_INDEX_REGISTER)
      *flag = false;
   else
      unreachable("Invalid PBE LOD type");

   return index.value;
}

/* Load/stores have their own operands */

static unsigned
agx_pack_memory_reg(agx_index index, bool *flag)
{
   assert(index.size == AGX_SIZE_16 || index.size == AGX_SIZE_32);
   assert_register_is_aligned(index);

   *flag = (index.size == AGX_SIZE_32);
   return index.value;
}

static unsigned
agx_pack_memory_base(agx_index index, bool *flag)
{
   assert(index.size == AGX_SIZE_64);
   assert((index.value & 1) == 0);

   /* Can't seem to access high uniforms from memory instructions */
   assert(index.value < 0x100);

   if (index.type == AGX_INDEX_UNIFORM) {
      *flag = 1;
   } else {
      assert(index.type == AGX_INDEX_REGISTER);
      *flag = 0;
   }

   return index.value;
}

static unsigned
agx_pack_memory_index(agx_index index, bool *flag)
{
   if (index.type == AGX_INDEX_IMMEDIATE) {
      assert(index.value < 0x10000);
      *flag = 1;

      return index.value;
   } else {
      assert(index.type == AGX_INDEX_REGISTER);
      assert(index.size == AGX_SIZE_32);
      assert((index.value & 1) == 0);
      assert(index.value < 0x100);

      *flag = 0;
      return index.value;
   }
}

static uint16_t
agx_pack_local_base(agx_index index, unsigned *flags)
{
   assert(index.size == AGX_SIZE_16);

   if (index.type == AGX_INDEX_IMMEDIATE) {
      assert(index.value == 0);
      *flags = 2;
      return 0;
   } else if (index.type == AGX_INDEX_UNIFORM) {
      *flags = 1 | ((index.value >> 8) << 1);
      return index.value & BITFIELD_MASK(7);
   } else {
      assert_register_is_aligned(index);
      *flags = 0;
      return index.value;
   }
}

static uint16_t
agx_pack_local_index(agx_index index, bool *flag)
{
   assert(index.size == AGX_SIZE_16);

   if (index.type == AGX_INDEX_IMMEDIATE) {
      assert(index.value < 0x10000);
      *flag = 1;
      return index.value;
   } else {
      assert_register_is_aligned(index);
      *flag = 0;
      return index.value;
   }
}

static unsigned
agx_pack_atomic_source(agx_index index)
{
   assert(index.size == AGX_SIZE_32 && "no 64-bit atomics yet");
   assert_register_is_aligned(index);
   return index.value;
}

static unsigned
agx_pack_atomic_dest(agx_index index, bool *flag)
{
   assert(index.size == AGX_SIZE_32 && "no 64-bit atomics yet");

   /* Atomic destinstions are optional (e.g. for update with no return) */
   if (index.type == AGX_INDEX_NULL) {
      *flag = 0;
      return 0;
   }

   /* But are otherwise registers */
   assert_register_is_aligned(index);
   *flag = 1;
   return index.value;
}

/* ALU goes through a common path */

static unsigned
agx_pack_alu_dst(agx_index dest)
{
   assert_register_is_aligned(dest);
   unsigned reg = dest.value;
   enum agx_size size = dest.size;
   assert(reg < 0x100);

   return (dest.cache ? (1 << 0) : 0) | ((size >= AGX_SIZE_32) ? (1 << 1) : 0) |
          ((size == AGX_SIZE_64) ? (1 << 2) : 0) | ((reg << 2));
}

static unsigned
agx_pack_alu_src(agx_index src)
{
   unsigned value = src.value;
   enum agx_size size = src.size;

   if (src.type == AGX_INDEX_IMMEDIATE) {
      /* Flags 0 for an 8-bit immediate */
      assert(value < 0x100);

      return (value & BITFIELD_MASK(6)) | ((value >> 6) << 10);
   } else if (src.type == AGX_INDEX_UNIFORM) {
      assert(size == AGX_SIZE_16 || size == AGX_SIZE_32);
      assert(value < AGX_NUM_UNIFORMS);

      return (value & BITFIELD_MASK(6)) |
             ((value & BITFIELD_BIT(8)) ? (1 << 6) : 0) |
             ((size == AGX_SIZE_32) ? (1 << 7) : 0) | (0x1 << 8) |
             (((value >> 6) & BITFIELD_MASK(2)) << 10);
   } else {
      assert_register_is_aligned(src);
      assert(!(src.cache && src.discard));

      unsigned hint = src.discard ? 0x3 : src.cache ? 0x2 : 0x1;
      unsigned size_flag = (size == AGX_SIZE_64)   ? 0x3
                           : (size == AGX_SIZE_32) ? 0x2
                           : (size == AGX_SIZE_16) ? 0x0
                                                   : 0x0;

      return (value & BITFIELD_MASK(6)) | (hint << 6) | (size_flag << 8) |
             (((value >> 6) & BITFIELD_MASK(2)) << 10);
   }
}

static unsigned
agx_pack_cmpsel_src(agx_index src, enum agx_size dest_size)
{
   unsigned value = src.value;
   ASSERTED enum agx_size size = src.size;

   if (src.type == AGX_INDEX_IMMEDIATE) {
      /* Flags 0x4 for an 8-bit immediate */
      assert(value < 0x100);

      return (value & BITFIELD_MASK(6)) | (0x4 << 6) | ((value >> 6) << 10);
   } else if (src.type == AGX_INDEX_UNIFORM) {
      assert(size == AGX_SIZE_16 || size == AGX_SIZE_32);
      assert(size == dest_size);
      assert(value < 0x200);

      return (value & BITFIELD_MASK(6)) | ((value >> 8) << 6) | (0x3 << 7) |
             (((value >> 6) & BITFIELD_MASK(2)) << 10);
   } else {
      assert(src.type == AGX_INDEX_REGISTER);
      assert(!(src.cache && src.discard));
      assert(size == AGX_SIZE_16 || size == AGX_SIZE_32);
      assert(size == dest_size);
      assert_register_is_aligned(src);

      unsigned hint = src.discard ? 0x3 : src.cache ? 0x2 : 0x1;

      return (value & BITFIELD_MASK(6)) | (hint << 6) |
             (((value >> 6) & BITFIELD_MASK(2)) << 10);
   }
}

static unsigned
agx_pack_sample_mask_src(agx_index src)
{
   unsigned value = src.value;
   unsigned packed_value =
      (value & BITFIELD_MASK(6)) | (((value >> 6) & BITFIELD_MASK(2)) << 10);

   if (src.type == AGX_INDEX_IMMEDIATE) {
      assert(value < 0x100);
      return packed_value | (1 << 7);
   } else {
      assert(src.type == AGX_INDEX_REGISTER);
      assert_register_is_aligned(src);
      assert(!(src.cache && src.discard));

      return packed_value;
   }
}

static unsigned
agx_pack_float_mod(agx_index src)
{
   return (src.abs ? (1 << 0) : 0) | (src.neg ? (1 << 1) : 0);
}

static bool
agx_all_16(agx_instr *I)
{
   agx_foreach_dest(I, d) {
      if (!agx_is_null(I->dest[d]) && I->dest[d].size != AGX_SIZE_16)
         return false;
   }

   agx_foreach_src(I, s) {
      if (!agx_is_null(I->src[s]) && I->src[s].size != AGX_SIZE_16)
         return false;
   }

   return true;
}

/* Generic pack for ALU instructions, which are quite regular */

static void
agx_pack_alu(struct util_dynarray *emission, agx_instr *I)
{
   struct agx_opcode_info info = agx_opcodes_info[I->op];
   bool is_16 = agx_all_16(I) && info.encoding_16.exact;
   struct agx_encoding encoding = is_16 ? info.encoding_16 : info.encoding;

   assert(encoding.exact && "invalid encoding");

   uint64_t raw = encoding.exact;
   uint16_t extend = 0;

   // TODO: assert saturable
   if (I->saturate)
      raw |= (1 << 6);

   if (info.nr_dests) {
      assert(info.nr_dests == 1);
      unsigned D = agx_pack_alu_dst(I->dest[0]);
      unsigned extend_offset = (sizeof(extend) * 8) - 4;

      raw |= (D & BITFIELD_MASK(8)) << 7;
      extend |= ((D >> 8) << extend_offset);
   } else if (info.immediates & AGX_IMMEDIATE_NEST) {
      raw |= (I->invert_cond << 8);
      raw |= (I->nest << 11);
      raw |= (I->icond << 13);
   }

   for (unsigned s = 0; s < info.nr_srcs; ++s) {
      bool is_cmpsel = (s >= 2) && (I->op == AGX_OPCODE_ICMPSEL ||
                                    I->op == AGX_OPCODE_FCMPSEL);

      unsigned src = is_cmpsel ? agx_pack_cmpsel_src(I->src[s], I->dest[0].size)
                               : agx_pack_alu_src(I->src[s]);

      unsigned src_short = (src & BITFIELD_MASK(10));
      unsigned src_extend = (src >> 10);

      /* Size bit always zero and so omitted for 16-bit */
      if (is_16 && !is_cmpsel)
         assert((src_short & (1 << 9)) == 0);

      if (info.is_float || (I->op == AGX_OPCODE_FCMPSEL && !is_cmpsel)) {
         unsigned fmod = agx_pack_float_mod(I->src[s]);
         unsigned fmod_offset = is_16 ? 9 : 10;
         src_short |= (fmod << fmod_offset);
      } else if (I->op == AGX_OPCODE_IMAD || I->op == AGX_OPCODE_IADD) {
         bool zext = I->src[s].abs;
         bool extends = I->src[s].size < AGX_SIZE_64;

         unsigned sxt = (extends && !zext) ? (1 << 10) : 0;

         unsigned negate_src = (I->op == AGX_OPCODE_IMAD) ? 2 : 1;
         assert(!I->src[s].neg || s == negate_src);
         src_short |= sxt;
      }

      /* Sources come at predictable offsets */
      unsigned offset = 16 + (12 * s);
      raw |= (((uint64_t)src_short) << offset);

      /* Destination and each source get extended in reverse order */
      unsigned extend_offset = (sizeof(extend) * 8) - ((s + 3) * 2);
      extend |= (src_extend << extend_offset);
   }

   if ((I->op == AGX_OPCODE_IMAD && I->src[2].neg) ||
       (I->op == AGX_OPCODE_IADD && I->src[1].neg))
      raw |= (1 << 27);

   if (info.immediates & AGX_IMMEDIATE_TRUTH_TABLE) {
      raw |= (I->truth_table & 0x3) << 26;
      raw |= (uint64_t)(I->truth_table >> 2) << 38;
   } else if (info.immediates & AGX_IMMEDIATE_SHIFT) {
      assert(I->shift <= 4);
      raw |= (uint64_t)(I->shift & 1) << 39;
      raw |= (uint64_t)(I->shift >> 1) << 52;
   } else if (info.immediates & AGX_IMMEDIATE_BFI_MASK) {
      raw |= (uint64_t)(I->bfi_mask & 0x3) << 38;
      raw |= (uint64_t)((I->bfi_mask >> 2) & 0x3) << 50;
      raw |= (uint64_t)((I->bfi_mask >> 4) & 0x1) << 63;
   } else if (info.immediates & AGX_IMMEDIATE_SR) {
      raw |= (uint64_t)(I->sr & 0x3F) << 16;
      raw |= (uint64_t)(I->sr >> 6) << 26;
   } else if (info.immediates & AGX_IMMEDIATE_WRITEOUT)
      raw |= (uint64_t)(I->imm) << 8;
   else if (info.immediates & AGX_IMMEDIATE_IMM)
      raw |= (uint64_t)(I->imm) << 16;
   else if (info.immediates & AGX_IMMEDIATE_ROUND)
      raw |= (uint64_t)(I->imm) << 26;
   else if (info.immediates & (AGX_IMMEDIATE_FCOND | AGX_IMMEDIATE_ICOND))
      raw |= (uint64_t)(I->fcond) << 61;

   /* Determine length bit */
   unsigned length = encoding.length_short;
   unsigned short_mask = (1 << length) - 1;
   bool length_bit = (extend || (raw & ~short_mask));

   if (encoding.extensible && length_bit) {
      raw |= (1 << 15);
      length += (length > 8) ? 4 : 2;
   }

   /* Pack! */
   if (length <= sizeof(uint64_t)) {
      unsigned extend_offset = ((length - sizeof(extend)) * 8);

      /* XXX: This is a weird special case */
      if (I->op == AGX_OPCODE_IADD)
         extend_offset -= 16;

      raw |= (uint64_t)extend << extend_offset;
      memcpy(util_dynarray_grow_bytes(emission, 1, length), &raw, length);
   } else {
      /* So far, >8 byte ALU is only to store the extend bits */
      unsigned extend_offset = (((length - sizeof(extend)) * 8) - 64);
      unsigned hi = ((uint64_t)extend) << extend_offset;

      memcpy(util_dynarray_grow_bytes(emission, 1, 8), &raw, 8);
      memcpy(util_dynarray_grow_bytes(emission, 1, length - 8), &hi,
             length - 8);
   }
}

static void
agx_pack_instr(struct util_dynarray *emission, struct util_dynarray *fixups,
               agx_instr *I, bool needs_g13x_coherency)
{
   switch (I->op) {
   case AGX_OPCODE_LD_TILE:
   case AGX_OPCODE_ST_TILE: {
      bool load = (I->op == AGX_OPCODE_LD_TILE);
      unsigned D = agx_pack_alu_dst(load ? I->dest[0] : I->src[0]);
      assert(I->mask < 0x10);
      assert(I->pixel_offset < 0x200);

      agx_index sample_index = load ? I->src[0] : I->src[1];
      assert(sample_index.type == AGX_INDEX_REGISTER ||
             sample_index.type == AGX_INDEX_IMMEDIATE);
      assert(sample_index.size == AGX_SIZE_16);
      unsigned St = (sample_index.type == AGX_INDEX_REGISTER) ? 1 : 0;
      unsigned S = sample_index.value;
      assert(S < 0x100);

      uint64_t raw = agx_opcodes_info[I->op].encoding.exact |
                     ((uint64_t)(D & BITFIELD_MASK(8)) << 7) | (St << 22) |
                     ((uint64_t)(I->format) << 24) |
                     ((uint64_t)(I->pixel_offset & BITFIELD_MASK(7)) << 28) |
                     (load ? (1ull << 35) : 0) | ((uint64_t)(I->mask) << 36) |
                     ((uint64_t)(I->pixel_offset >> 7) << 40) |
                     ((uint64_t)(S & BITFIELD_MASK(6)) << 42) |
                     ((uint64_t)(S >> 6) << 56) | (((uint64_t)(D >> 8)) << 60);

      unsigned size = 8;
      memcpy(util_dynarray_grow_bytes(emission, 1, size), &raw, size);
      break;
   }

   case AGX_OPCODE_SAMPLE_MASK: {
      unsigned S = agx_pack_sample_mask_src(I->src[1]);
      unsigned T = I->src[0].value;
      bool Tt = I->src[0].type == AGX_INDEX_IMMEDIATE;
      assert(Tt || I->src[0].type == AGX_INDEX_REGISTER);
      uint32_t raw = 0xc1 | (Tt ? BITFIELD_BIT(8) : 0) |
                     ((T & BITFIELD_MASK(6)) << 9) | ((S & 0xff) << 16) |
                     ((T >> 6) << 24) | ((S >> 8) << 26);

      unsigned size = 4;
      memcpy(util_dynarray_grow_bytes(emission, 1, size), &raw, size);
      break;
   }

   case AGX_OPCODE_WAIT: {
      uint64_t raw =
         agx_opcodes_info[I->op].encoding.exact | (I->scoreboard << 8);

      unsigned size = 2;
      memcpy(util_dynarray_grow_bytes(emission, 1, size), &raw, size);
      break;
   }

   case AGX_OPCODE_ITER:
   case AGX_OPCODE_ITERPROJ:
   case AGX_OPCODE_LDCF: {
      bool flat = (I->op == AGX_OPCODE_LDCF);
      bool perspective = (I->op == AGX_OPCODE_ITERPROJ);
      unsigned D = agx_pack_alu_dst(I->dest[0]);
      unsigned channels = (I->channels & 0x3);

      agx_index src_I = I->src[0];
      assert(src_I.type == AGX_INDEX_IMMEDIATE);

      unsigned cf_I = src_I.value;
      unsigned cf_J = 0;

      if (perspective) {
         agx_index src_J = I->src[1];
         assert(src_J.type == AGX_INDEX_IMMEDIATE);
         cf_J = src_J.value;
      }

      assert(cf_I < 0x100);
      assert(cf_J < 0x100);

      enum agx_interpolation interp = I->interpolation;
      agx_index sample_index = flat ? agx_null() : I->src[perspective ? 2 : 1];

      /* Fix up the interpolation enum to distinguish the sample index source */
      if (interp == AGX_INTERPOLATION_SAMPLE) {
         if (sample_index.type == AGX_INDEX_REGISTER)
            interp = AGX_INTERPOLATION_SAMPLE_REGISTER;
         else
            assert(sample_index.type == AGX_INDEX_IMMEDIATE);
      } else {
         sample_index = agx_zero();
      }

      bool kill = false; // TODO: optimize

      uint64_t raw =
         0x21 | (flat ? (1 << 7) : 0) | (perspective ? (1 << 6) : 0) |
         ((D & 0xFF) << 7) | (1ull << 15) | /* XXX */
         ((cf_I & BITFIELD_MASK(6)) << 16) | ((cf_J & BITFIELD_MASK(6)) << 24) |
         (((uint64_t)channels) << 30) | (((uint64_t)sample_index.value) << 32) |
         (!flat ? (1ull << 46) : 0) |                             /* XXX */
         (((uint64_t)interp) << 48) | (kill ? (1ull << 52) : 0) | /* XXX */
         (((uint64_t)(D >> 8)) << 56) | ((uint64_t)(cf_I >> 6) << 58) |
         ((uint64_t)(cf_J >> 6) << 60);

      unsigned size = 8;
      memcpy(util_dynarray_grow_bytes(emission, 1, size), &raw, size);
      break;
   }

   case AGX_OPCODE_ST_VARY: {
      agx_index index_src = I->src[0];
      agx_index value = I->src[1];

      assert(index_src.type == AGX_INDEX_IMMEDIATE);
      assert(index_src.value < BITFIELD_MASK(8));
      assert(value.type == AGX_INDEX_REGISTER);
      assert(value.size == AGX_SIZE_32);

      uint64_t raw =
         0x11 | (I->last ? (1 << 7) : 0) | ((value.value & 0x3F) << 9) |
         (((uint64_t)(index_src.value & 0x3F)) << 16) | (0x80 << 16) | /* XXX */
         ((value.value >> 6) << 24) | ((index_src.value >> 6) << 26) |
         (0x8u << 28); /* XXX */

      unsigned size = 4;
      memcpy(util_dynarray_grow_bytes(emission, 1, size), &raw, size);
      break;
   }

   case AGX_OPCODE_DEVICE_LOAD:
   case AGX_OPCODE_DEVICE_STORE:
   case AGX_OPCODE_UNIFORM_STORE: {
      bool is_device_store = I->op == AGX_OPCODE_DEVICE_STORE;
      bool is_uniform_store = I->op == AGX_OPCODE_UNIFORM_STORE;
      bool is_store = is_device_store || is_uniform_store;
      bool has_base = !is_uniform_store;

      /* Uniform stores internally packed as 16-bit. Fix up the format, mask,
       * and size so we can use scalar 32-bit values in the IR and avoid
       * special casing earlier in the compiler.
       */
      enum agx_format format = is_uniform_store ? AGX_FORMAT_I16 : I->format;
      agx_index reg = is_store ? I->src[0] : I->dest[0];
      unsigned mask = I->mask;

      if (is_uniform_store) {
         mask = BITFIELD_MASK(agx_size_align_16(reg.size));
         reg.size = AGX_SIZE_16;
      }

      unsigned offset_src = (has_base ? 1 : 0) + (is_store ? 1 : 0);

      bool Rt, At = false, Ot;
      unsigned R = agx_pack_memory_reg(reg, &Rt);
      unsigned A =
         has_base ? agx_pack_memory_base(I->src[is_store ? 1 : 0], &At) : 0;
      unsigned O = agx_pack_memory_index(I->src[offset_src], &Ot);
      unsigned u1 = is_uniform_store ? 0 : 1; // XXX
      unsigned u3 = 0;
      unsigned u4 = is_uniform_store ? 0 : 4; // XXX
      unsigned u5 = 0;
      bool L = true; /* TODO: when would you want short? */

      assert(mask != 0);
      assert(format <= 0x10);

      uint64_t raw =
         agx_opcodes_info[I->op].encoding.exact |
         ((format & BITFIELD_MASK(3)) << 7) | ((R & BITFIELD_MASK(6)) << 10) |
         ((A & BITFIELD_MASK(4)) << 16) | ((O & BITFIELD_MASK(4)) << 20) |
         (Ot ? (1 << 24) : 0) | (I->src[offset_src].abs ? (1 << 25) : 0) |
         (is_uniform_store ? (2 << 25) : 0) | (u1 << 26) | (At << 27) |
         (u3 << 28) | (I->scoreboard << 30) |
         (((uint64_t)((O >> 4) & BITFIELD_MASK(4))) << 32) |
         (((uint64_t)((A >> 4) & BITFIELD_MASK(4))) << 36) |
         (((uint64_t)((R >> 6) & BITFIELD_MASK(2))) << 40) |
         (((uint64_t)I->shift) << 42) | (((uint64_t)u4) << 44) |
         (L ? (1ull << 47) : 0) | (((uint64_t)(format >> 3)) << 48) |
         (((uint64_t)Rt) << 49) | (((uint64_t)u5) << 50) |
         (((uint64_t)mask) << 52) | (((uint64_t)(O >> 8)) << 56);

      unsigned size = L ? 8 : 6;
      memcpy(util_dynarray_grow_bytes(emission, 1, size), &raw, size);
      break;
   }

   case AGX_OPCODE_LOCAL_LOAD:
   case AGX_OPCODE_LOCAL_STORE: {
      bool is_load = I->op == AGX_OPCODE_LOCAL_LOAD;
      bool L = true; /* TODO: when would you want short? */
      unsigned At;
      bool Rt, Ot;

      unsigned R = agx_pack_memory_reg(is_load ? I->dest[0] : I->src[0], &Rt);
      unsigned A = agx_pack_local_base(is_load ? I->src[0] : I->src[1], &At);
      unsigned O = agx_pack_local_index(is_load ? I->src[1] : I->src[2], &Ot);

      uint64_t raw =
         agx_opcodes_info[I->op].encoding.exact | (Rt ? BITFIELD64_BIT(8) : 0) |
         ((R & BITFIELD_MASK(6)) << 9) | (L ? BITFIELD64_BIT(15) : 0) |
         ((A & BITFIELD_MASK(6)) << 16) | (At << 22) | (I->format << 24) |
         ((O & BITFIELD64_MASK(6)) << 28) | (Ot ? BITFIELD64_BIT(34) : 0) |
         (((uint64_t)I->mask) << 36) | (((uint64_t)(O >> 6)) << 48) |
         (((uint64_t)(A >> 6)) << 58) | (((uint64_t)(R >> 6)) << 60);

      unsigned size = L ? 8 : 6;
      memcpy(util_dynarray_grow_bytes(emission, 1, size), &raw, size);
      break;
   }

   case AGX_OPCODE_ATOMIC: {
      bool At, Ot, Rt;
      unsigned A = agx_pack_memory_base(I->src[1], &At);
      unsigned O = agx_pack_memory_index(I->src[2], &Ot);
      unsigned R = agx_pack_atomic_dest(I->dest[0], &Rt);
      unsigned S = agx_pack_atomic_source(I->src[0]);

      uint64_t raw =
         agx_opcodes_info[I->op].encoding.exact |
         (((uint64_t)I->atomic_opc) << 6) | ((R & BITFIELD_MASK(6)) << 10) |
         ((A & BITFIELD_MASK(4)) << 16) | ((O & BITFIELD_MASK(4)) << 20) |
         (Ot ? (1 << 24) : 0) | (I->src[2].abs ? (1 << 25) : 0) | (At << 27) |
         (I->scoreboard << 30) |
         (((uint64_t)((O >> 4) & BITFIELD_MASK(4))) << 32) |
         (((uint64_t)((A >> 4) & BITFIELD_MASK(4))) << 36) |
         (((uint64_t)(R >> 6)) << 40) |
         (needs_g13x_coherency ? BITFIELD64_BIT(45) : 0) |
         (Rt ? BITFIELD64_BIT(47) : 0) | (((uint64_t)S) << 48) |
         (((uint64_t)(O >> 8)) << 56);

      memcpy(util_dynarray_grow_bytes(emission, 1, 8), &raw, 8);
      break;
   }

   case AGX_OPCODE_LOCAL_ATOMIC: {
      bool L = true; /* TODO: Don't force */

      unsigned At;
      bool Rt = false, Ot;

      bool Ra = I->dest[0].type != AGX_INDEX_NULL;
      unsigned R = Ra ? agx_pack_memory_reg(I->dest[0], &Rt) : 0;
      unsigned S = agx_pack_atomic_source(I->src[0]);
      unsigned A = agx_pack_local_base(I->src[1], &At);
      unsigned O = agx_pack_local_index(I->src[2], &Ot);

      uint64_t raw =
         agx_opcodes_info[I->op].encoding.exact | (Rt ? BITFIELD64_BIT(8) : 0) |
         ((R & BITFIELD_MASK(6)) << 9) | (L ? BITFIELD64_BIT(15) : 0) |
         ((A & BITFIELD_MASK(6)) << 16) | (At << 22) |
         (((uint64_t)I->atomic_opc) << 24) | ((O & BITFIELD64_MASK(6)) << 28) |
         (Ot ? BITFIELD64_BIT(34) : 0) | (Ra ? BITFIELD64_BIT(38) : 0) |
         (((uint64_t)(O >> 6)) << 48) | (((uint64_t)(A >> 6)) << 58) |
         (((uint64_t)(R >> 6)) << 60);

      uint64_t raw2 = S;

      memcpy(util_dynarray_grow_bytes(emission, 1, 8), &raw, 8);
      memcpy(util_dynarray_grow_bytes(emission, 1, 2), &raw2, 2);
      break;
   }

   case AGX_OPCODE_TEXTURE_LOAD:
   case AGX_OPCODE_IMAGE_LOAD:
   case AGX_OPCODE_TEXTURE_SAMPLE: {
      assert(I->mask != 0);
      assert(I->format <= 0x10);

      bool Rt, Ct, St, Cs;
      unsigned Tt;
      unsigned U;
      enum agx_lod_mode lod_mode = I->lod_mode;

      unsigned R = agx_pack_memory_reg(I->dest[0], &Rt);
      unsigned C = agx_pack_sample_coords(I->src[0], &Ct, &Cs);
      unsigned T = agx_pack_texture(I->src[2], I->src[3], &U, &Tt);
      unsigned S = agx_pack_sampler(I->src[4], &St);
      unsigned O = agx_pack_sample_compare_offset(I->src[5]);
      unsigned D = agx_pack_lod(I->src[1], &lod_mode);

      unsigned q1 = I->shadow;
      unsigned q2 = 0;   // XXX
      unsigned q3 = 12;  // XXX
      unsigned kill = 0; // helper invocation kill bit

      /* Set bit 43 for image loads. This seems to makes sure that image loads
       * get the value written by the latest image store, not some other image
       * store that was already in flight, fixing
       *
       *    KHR-GLES31.core.shader_image_load_store.basic-glsl-misc-fs
       *
       * Apple seems to set this bit unconditionally for read/write image loads
       * and never for readonly image loads. Some sort of cache control.
       */
      if (I->op == AGX_OPCODE_IMAGE_LOAD)
         q3 |= 1;

      uint32_t extend = ((U & BITFIELD_MASK(5)) << 0) | (kill << 5) |
                        ((I->dim >> 3) << 7) | ((R >> 6) << 8) |
                        ((C >> 6) << 10) | ((D >> 6) << 12) | ((T >> 6) << 14) |
                        ((O & BITFIELD_MASK(6)) << 16) | (I->gather << 23) |
                        (I->offset << 27) | ((S >> 6) << 28) | ((O >> 6) << 30);

      bool L = (extend != 0);

      uint64_t raw =
         0x31 | ((I->op != AGX_OPCODE_TEXTURE_SAMPLE) ? (1 << 6) : 0) |
         (Rt ? (1 << 8) : 0) | ((R & BITFIELD_MASK(6)) << 9) |
         (L ? (1 << 15) : 0) | ((C & BITFIELD_MASK(6)) << 16) |
         (Ct ? (1 << 22) : 0) | (q1 << 23) | ((D & BITFIELD_MASK(6)) << 24) |
         (q2 << 30) | (((uint64_t)(T & BITFIELD_MASK(6))) << 32) |
         (((uint64_t)Tt) << 38) |
         (((uint64_t)(I->dim & BITFIELD_MASK(3))) << 40) |
         (((uint64_t)q3) << 43) | (((uint64_t)I->mask) << 48) |
         (((uint64_t)lod_mode) << 52) |
         (((uint64_t)(S & BITFIELD_MASK(6))) << 56) | (((uint64_t)St) << 62) |
         (((uint64_t)I->scoreboard) << 63);

      memcpy(util_dynarray_grow_bytes(emission, 1, 8), &raw, 8);
      if (L)
         memcpy(util_dynarray_grow_bytes(emission, 1, 4), &extend, 4);

      break;
   }

   case AGX_OPCODE_IMAGE_WRITE: {
      bool Ct, Dt, Rt, Cs;
      unsigned Tt;
      unsigned U;

      unsigned R = agx_pack_pbe_source(I->src[0], &Rt);
      unsigned C = agx_pack_sample_coords(I->src[1], &Ct, &Cs);
      unsigned D = agx_pack_pbe_lod(I->src[2], &Dt);
      unsigned T = agx_pack_texture(I->src[3], I->src[4], &U, &Tt);
      bool rtz = false;

      assert(U < (1 << 5));
      assert(D < (1 << 8));
      assert(R < (1 << 8));
      assert(C < (1 << 8));
      assert(T < (1 << 8));
      assert(Tt < (1 << 2));

      uint64_t raw = agx_opcodes_info[I->op].encoding.exact |
                     (Rt ? (1 << 8) : 0) | ((R & BITFIELD_MASK(6)) << 9) |
                     ((C & BITFIELD_MASK(6)) << 16) | (Ct ? (1 << 22) : 0) |
                     ((D & BITFIELD_MASK(6)) << 24) | (Dt ? (1u << 31) : 0) |
                     (((uint64_t)(T & BITFIELD_MASK(6))) << 32) |
                     (((uint64_t)Tt) << 38) |
                     (((uint64_t)I->dim & BITFIELD_MASK(3)) << 40) |
                     (Cs ? (1ull << 47) : 0) | (((uint64_t)U) << 48) |
                     (rtz ? (1ull << 53) : 0) |
                     ((I->dim & BITFIELD_BIT(4)) ? (1ull << 55) : 0) |
                     (((uint64_t)R >> 6) << 56) | (((uint64_t)C >> 6) << 58) |
                     (((uint64_t)D >> 6) << 60) | (((uint64_t)T >> 6) << 62);

      if (raw >> 48) {
         raw |= BITFIELD_BIT(15);
         memcpy(util_dynarray_grow_bytes(emission, 1, 8), &raw, 8);
      } else {
         memcpy(util_dynarray_grow_bytes(emission, 1, 6), &raw, 6);
      }

      break;
   }

   case AGX_OPCODE_BLOCK_IMAGE_STORE: {
      enum agx_format F = I->format;
      assert(F < 0x10);

      unsigned Tt = 0;
      assert(Tt < 0x4);

      UNUSED unsigned U;
      unsigned T = agx_pack_texture(agx_zero(), I->src[0], &U, &Tt);
      assert(T < 0x100);

      agx_index offset = I->src[1];
      assert(offset.type == AGX_INDEX_REGISTER);
      assert(offset.size == AGX_SIZE_16);
      unsigned R = offset.value;

      assert(I->dim == AGX_DIM_2D || I->dim == AGX_DIM_2D_MS);
      bool msaa = (I->dim == AGX_DIM_2D_MS);

      bool unk1 = true;
      unsigned unk2 = msaa ? 38 : 37; /* XXX */
      unsigned unk3 = 1;

      uint32_t word0 = agx_opcodes_info[I->op].encoding.exact |
                       (1 << 15) /* we always set length bit for now */ |
                       ((F & 1) << 8) | ((R & BITFIELD_MASK(6)) << 9) |
                       (unk1 ? (1u << 31) : 0);

      uint32_t word1 =
         (T & BITFIELD_MASK(6)) | (Tt << 2) | (unk2 << 9) | ((R >> 6) << 24);

      uint32_t word2 = (F >> 1) | (unk3 ? (1 << 3) : 0) | ((T >> 6) << 14);

      memcpy(util_dynarray_grow_bytes(emission, 1, 4), &word0, 4);
      memcpy(util_dynarray_grow_bytes(emission, 1, 4), &word1, 4);
      memcpy(util_dynarray_grow_bytes(emission, 1, 2), &word2, 2);
      break;
   }

   case AGX_OPCODE_ZS_EMIT: {
      agx_index S = I->src[0];
      if (S.type == AGX_INDEX_IMMEDIATE)
         assert(S.value < BITFIELD_BIT(8));
      else
         assert_register_is_aligned(S);

      agx_index T = I->src[1];
      assert_register_is_aligned(T);

      assert(I->zs >= 1 && I->zs <= 3);

      uint32_t word0 = agx_opcodes_info[I->op].encoding.exact |
                       ((S.type == AGX_INDEX_IMMEDIATE) ? (1 << 8) : 0) |
                       ((S.value & BITFIELD_MASK(6)) << 9) |
                       ((T.value & BITFIELD_MASK(6)) << 16) |
                       ((T.value >> 6) << 26) | ((S.value >> 6) << 24) |
                       (I->zs << 29);

      memcpy(util_dynarray_grow_bytes(emission, 1, 4), &word0, 4);
      break;
   }

   case AGX_OPCODE_JMP_EXEC_ANY:
   case AGX_OPCODE_JMP_EXEC_NONE: {
      /* We don't implement indirect branches */
      assert(I->target != NULL);

      /* We'll fix the offset later. */
      struct agx_branch_fixup fixup = {.block = I->target,
                                       .offset = emission->size};

      util_dynarray_append(fixups, struct agx_branch_fixup, fixup);

      /* The rest of the instruction is fixed */
      struct agx_opcode_info info = agx_opcodes_info[I->op];
      uint64_t raw = info.encoding.exact;
      memcpy(util_dynarray_grow_bytes(emission, 1, 6), &raw, 6);
      break;
   }

   default:
      agx_pack_alu(emission, I);
      return;
   }
}

/* Relative branches may be emitted before their targets, so we patch the
 * binary to fix up the branch offsets after the main emit */

static void
agx_fixup_branch(struct util_dynarray *emission, struct agx_branch_fixup fix)
{
   /* Branch offset is 2 bytes into the jump instruction */
   uint8_t *location = ((uint8_t *)emission->data) + fix.offset + 2;

   /* Offsets are relative to the jump instruction */
   int32_t patch = (int32_t)fix.block->offset - (int32_t)fix.offset;

   /* Patch the binary */
   memcpy(location, &patch, sizeof(patch));
}

void
agx_pack_binary(agx_context *ctx, struct util_dynarray *emission)
{
   struct util_dynarray fixups;
   util_dynarray_init(&fixups, ctx);

   agx_foreach_instr_global_safe(ctx, I) {
      if (I->op == AGX_OPCODE_LOGICAL_END)
         agx_remove_instruction(I);
   }

   agx_foreach_block(ctx, block) {
      /* Relative to the start of the binary, the block begins at the current
       * number of bytes emitted */
      block->offset = emission->size;

      agx_foreach_instr_in_block(block, ins) {
         agx_pack_instr(emission, &fixups, ins, ctx->key->needs_g13x_coherency);
      }
   }

   util_dynarray_foreach(&fixups, struct agx_branch_fixup, fixup)
      agx_fixup_branch(emission, *fixup);

   /* Dougall calls the instruction in this footer "trap". Match the blob. */
   for (unsigned i = 0; i < 8; ++i) {
      uint16_t trap = agx_opcodes_info[AGX_OPCODE_TRAP].encoding.exact;
      util_dynarray_append(emission, uint16_t, trap);
   }

   util_dynarray_fini(&fixups);
}
