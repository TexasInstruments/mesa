#
# Copyright © 2021 Google, Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

from mako.template import Template
import sys
from enum import Enum

def max_bitfield_val(high, low, shift):
    return ((1 << (high - low)) - 1) << shift


class CHIP(Enum):
    A2XX = 2
    A3XX = 3
    A4XX = 4
    A5XX = 5
    A6XX = 6
    A7XX = 7

class CCUColorCacheFraction(Enum):
    FULL = 0
    HALF = 1
    QUARTER = 2
    EIGHTH = 3


class State(object):
    def __init__(self):
        # List of unique device-info structs, multiple different GPU ids
        # can map to a single info struct in cases where the differences
        # are not sw visible, or the only differences are parameters
        # queried from the kernel (like GMEM size)
        self.gpu_infos = []

        # Table mapping GPU id to device-info struct
        self.gpus = {}

    def info_index(self, gpu_info):
        i = 0
        for info in self.gpu_infos:
            if gpu_info == info:
                return i
            i += 1
        raise Error("invalid info")

s = State()

def add_gpus(ids, info):
    for id in ids:
        s.gpus[id] = info

class GPUId(object):
    def __init__(self, gpu_id = None, chip_id = None, name=None):
        if chip_id == None:
            assert(gpu_id != None)
            val = gpu_id
            core = int(val / 100)
            val -= (core * 100);
            major = int(val / 10);
            val -= (major * 10)
            minor = val
            chip_id = (core << 24) | (major << 16) | (minor << 8) | 0xff
        self.chip_id = chip_id
        if gpu_id == None:
            gpu_id = 0
        self.gpu_id = gpu_id
        if name == None:
            assert(gpu_id != 0)
            name = "FD%d" % gpu_id
        self.name = name

class Struct(object):
    """A helper class that stringifies itself to a 'C' struct initializer
    """
    def __str__(self):
        s = "{"
        for name, value in vars(self).items():
            s += "." + name + "=" + str(value) + ","
        return s + "}"

class GPUInfo(Struct):
    """Base class for any generation of adreno, consists of GMEM layout
       related parameters

       Note that tile_max_h is normally only constrained by corresponding
       bitfield size/shift (ie. VSC_BIN_SIZE, or similar), but tile_max_h
       tends to have lower limits, in which case a comment will describe
       the bitfield size/shift
    """
    def __init__(self, chip, gmem_align_w, gmem_align_h,
                 tile_align_w, tile_align_h,
                 tile_max_w, tile_max_h, num_vsc_pipes,
                 cs_shared_mem_size, wave_granularity):
        self.chip          = chip.value
        self.gmem_align_w  = gmem_align_w
        self.gmem_align_h  = gmem_align_h
        self.tile_align_w  = tile_align_w
        self.tile_align_h  = tile_align_h
        self.tile_max_w    = tile_max_w
        self.tile_max_h    = tile_max_h
        self.num_vsc_pipes = num_vsc_pipes
        self.cs_shared_mem_size = cs_shared_mem_size
        self.wave_granularity = wave_granularity

        s.gpu_infos.append(self)


class A6xxGPUInfo(GPUInfo):
    """The a6xx generation has a lot more parameters, and is broken down
       into distinct sub-generations.  The template parameter avoids
       duplication of parameters that are unique to the sub-generation.
    """
    def __init__(self, chip, template, num_ccu, tile_align_w, tile_align_h, num_vsc_pipes, cs_shared_mem_size, wave_granularity, magic_regs):
        super().__init__(chip, gmem_align_w = 16, gmem_align_h = 4,
                         tile_align_w = tile_align_w,
                         tile_align_h = tile_align_h,
                         tile_max_w   = 1024, # max_bitfield_val(5, 0, 5)
                         tile_max_h   = max_bitfield_val(14, 8, 4),
                         num_vsc_pipes = num_vsc_pipes,
                         cs_shared_mem_size = cs_shared_mem_size,
                         wave_granularity   = wave_granularity)
        # The # of SP cores seems to always match # of CCU
        self.num_sp_cores = num_ccu
        self.num_ccu = num_ccu

        self.a6xx = Struct()
        self.a6xx.magic = Struct()

        for name, val in magic_regs.items():
            setattr(self.a6xx.magic, name, val)

        # Things that earlier gens have and later gens remove, provide
        # defaults here and let them be overridden by sub-gen template:
        self.a6xx.has_cp_reg_write = True
        self.a6xx.has_8bpp_ubwc = True

        self.a6xx.has_gmem_fast_clear = True
        self.a6xx.has_hw_multiview = True
        self.a6xx.has_fs_tex_prefetch = True
        self.a6xx.has_sampler_minmax = True

        self.a6xx.sysmem_per_ccu_cache_size = 64 * 1024
        self.a6xx.gmem_ccu_color_cache_fraction = CCUColorCacheFraction.QUARTER.value

        self.a6xx.prim_alloc_threshold = 0x7

        self.a6xx.vs_max_inputs_count = 32

        for name, val in template.items():
            if name == "magic": # handled above
                continue
            setattr(self.a6xx, name, val)

# a2xx is really two sub-generations, a20x and a22x, but we don't currently
# capture that in the device-info tables
add_gpus([
        GPUId(200),
        GPUId(201),
        GPUId(205),
        GPUId(220),
    ], GPUInfo(
        CHIP.A2XX,
        gmem_align_w = 32,  gmem_align_h = 32,
        tile_align_w = 32,  tile_align_h = 32,
        tile_max_w   = 512,
        tile_max_h   = ~0, # TODO
        num_vsc_pipes = 8,
        cs_shared_mem_size = 0,
        wave_granularity = 2
    ))

add_gpus([
        GPUId(305),
        GPUId(307),
        GPUId(320),
        GPUId(330),
    ], GPUInfo(
        CHIP.A3XX,
        gmem_align_w = 32,  gmem_align_h = 32,
        tile_align_w = 32,  tile_align_h = 32,
        tile_max_w   = 992, # max_bitfield_val(4, 0, 5)
        tile_max_h   = max_bitfield_val(9, 5, 5),
        num_vsc_pipes = 8,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2
    ))

add_gpus([
        GPUId(405),
        GPUId(420),
        GPUId(430),
    ], GPUInfo(
        CHIP.A4XX,
        gmem_align_w = 32,  gmem_align_h = 32,
        tile_align_w = 32,  tile_align_h = 32,
        tile_max_w   = 1024, # max_bitfield_val(4, 0, 5)
        tile_max_h   = max_bitfield_val(9, 5, 5),
        num_vsc_pipes = 8,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2
    ))

add_gpus([
        GPUId(506),
        GPUId(508),
        GPUId(509),
        GPUId(510),
        GPUId(512),
        GPUId(530),
        GPUId(540),
    ], GPUInfo(
        CHIP.A5XX,
        gmem_align_w = 64,  gmem_align_h = 32,
        tile_align_w = 64,  tile_align_h = 32,
        tile_max_w   = 1024, # max_bitfield_val(7, 0, 5)
        tile_max_h   = max_bitfield_val(16, 9, 5),
        num_vsc_pipes = 16,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2
    ))

# a6xx can be divided into distinct sub-generations, where certain device-
# info parameters are keyed to the sub-generation.  These templates reduce
# the copypaste

# a615, a616, a618, a619, a620 and a630:
a6xx_gen1 = dict(
        fibers_per_sp = 128 * 16,
        reg_size_vec4 = 96,
        instr_cache_size = 64,
        concurrent_resolve = False,
        indirect_draw_wfm_quirk = True,
        depth_bounds_require_depth_test_quirk = True,
        supports_double_threadsize = True,
    )

# a605, a608, a610, 612
a6xx_gen1_low = {**a6xx_gen1, **dict(
        has_gmem_fast_clear = False,
        reg_size_vec4 = 48,
        has_hw_multiview = False,
        has_sampler_minmax = False,
        has_fs_tex_prefetch = False,
        sysmem_per_ccu_cache_size = 8 * 1024,
        gmem_ccu_color_cache_fraction = CCUColorCacheFraction.HALF.value,
        vs_max_inputs_count = 16,
        supports_double_threadsize = False,
)}

# a640, a680:
a6xx_gen2 = dict(
        fibers_per_sp = 128 * 4 * 16,
        reg_size_vec4 = 96,
        instr_cache_size = 64, # TODO
        supports_multiview_mask = True,
        has_z24uint_s8uint = True,
        indirect_draw_wfm_quirk = True,
        depth_bounds_require_depth_test_quirk = True, # TODO: check if true
        has_dp2acc = False, # TODO: check if true
        has_8bpp_ubwc = False,
        supports_double_threadsize = True,
    )

# a650:
a6xx_gen3 = dict(
        fibers_per_sp = 128 * 2 * 16,
        reg_size_vec4 = 64,
        # Blob limits it to 128 but we hang with 128
        instr_cache_size = 127,
        supports_multiview_mask = True,
        has_z24uint_s8uint = True,
        tess_use_shared = True,
        storage_16bit = True,
        has_tex_filter_cubic = True,
        has_separate_chroma_filter = True,
        has_sample_locations = True,
        has_8bpp_ubwc = False,
        has_dp2acc = True,
        has_lrz_dir_tracking = True,
        enable_lrz_fast_clear = True,
        lrz_track_quirk = True,
        has_per_view_viewport = True,
        supports_double_threadsize = True,
    )

# a635, a660:
a6xx_gen4 = dict(
        fibers_per_sp = 128 * 2 * 16,
        reg_size_vec4 = 64,
        # Blob limits it to 128 but we hang with 128
        instr_cache_size = 127,
        supports_multiview_mask = True,
        has_z24uint_s8uint = True,
        tess_use_shared = True,
        storage_16bit = True,
        has_tex_filter_cubic = True,
        has_separate_chroma_filter = True,
        has_sample_locations = True,
        has_cp_reg_write = False,
        has_8bpp_ubwc = False,
        has_lpac = True,
        has_shading_rate = True,
        has_getfiberid = True,
        has_dp2acc = True,
        has_dp4acc = True,
        enable_lrz_fast_clear = True,
        has_lrz_dir_tracking = True,
        has_per_view_viewport = True,
        supports_double_threadsize = True,
    )

add_gpus([
        GPUId(605), # TODO: Test it, based only on libwrapfake dumps
        GPUId(608), # TODO: Test it, based only on libwrapfake dumps
        GPUId(610),
        GPUId(612), # TODO: Test it, based only on libwrapfake dumps
    ], A6xxGPUInfo(
        CHIP.A6XX,
        a6xx_gen1_low,
        num_ccu = 1,
        tile_align_w = 32,
        tile_align_h = 16,
        num_vsc_pipes = 16,
        cs_shared_mem_size = 16 * 1024,
        wave_granularity = 1,
        magic_regs = dict(
            PC_POWER_CNTL = 0,
            TPL1_DBG_ECO_CNTL = 0,
            GRAS_DBG_ECO_CNTL = 0,
            SP_CHICKEN_BITS = 0,
            UCHE_CLIENT_PF = 0x00000004,
            PC_MODE_CNTL = 0xf,
            SP_DBG_ECO_CNTL = 0x0,
            RB_DBG_ECO_CNTL = 0x04100000,
            RB_DBG_ECO_CNTL_blit = 0x04100000,
            HLSQ_DBG_ECO_CNTL = 0,
            RB_UNKNOWN_8E01 = 0x00000001,
            VPC_DBG_ECO_CNTL = 0x0,
            UCHE_UNKNOWN_0E12 = 0x10000000,
        ),
    ))

add_gpus([
        GPUId(615),
        GPUId(616),
        GPUId(618),
        GPUId(619),
    ], A6xxGPUInfo(
        CHIP.A6XX,
        a6xx_gen1,
        num_ccu = 1,
        tile_align_w = 32,
        tile_align_h = 32,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        magic_regs = dict(
            PC_POWER_CNTL = 0,
            TPL1_DBG_ECO_CNTL = 0x00108000,
            GRAS_DBG_ECO_CNTL = 0x00000880,
            SP_CHICKEN_BITS = 0x00000430,
            UCHE_CLIENT_PF = 0x00000004,
            PC_MODE_CNTL = 0x1f,
            SP_DBG_ECO_CNTL = 0x0,
            RB_DBG_ECO_CNTL = 0x04100000,
            RB_DBG_ECO_CNTL_blit = 0x04100000,
            HLSQ_DBG_ECO_CNTL = 0x00080000,
            RB_UNKNOWN_8E01 = 0x00000001,
            VPC_DBG_ECO_CNTL = 0x0,
            UCHE_UNKNOWN_0E12 = 0x00000001
        )
    ))

add_gpus([
        GPUId(620),
    ], A6xxGPUInfo(
        CHIP.A6XX,
        a6xx_gen1,
        num_ccu = 1,
        tile_align_w = 32,
        tile_align_h = 16,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        magic_regs = dict(
            PC_POWER_CNTL = 0,
            TPL1_DBG_ECO_CNTL = 0x01008000,
            GRAS_DBG_ECO_CNTL = 0x0,
            SP_CHICKEN_BITS = 0x00000400,
            UCHE_CLIENT_PF = 0x00000004,
            PC_MODE_CNTL = 0x1f,
            SP_DBG_ECO_CNTL = 0x01000000,
            RB_DBG_ECO_CNTL = 0x04100000,
            RB_DBG_ECO_CNTL_blit = 0x04100000,
            HLSQ_DBG_ECO_CNTL = 0x0,
            RB_UNKNOWN_8E01 = 0x0,
            VPC_DBG_ECO_CNTL = 0x02000000,
            UCHE_UNKNOWN_0E12 = 0x00000001
        )
    ))

add_gpus([
        GPUId(630),
    ], A6xxGPUInfo(
        CHIP.A6XX,
        a6xx_gen1,
        num_ccu = 2,
        tile_align_w = 32,
        tile_align_h = 16,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        magic_regs = dict(
            PC_POWER_CNTL = 1,
            TPL1_DBG_ECO_CNTL = 0x00108000,
            GRAS_DBG_ECO_CNTL = 0x00000880,
            SP_CHICKEN_BITS = 0x00001430,
            UCHE_CLIENT_PF = 0x00000004,
            PC_MODE_CNTL = 0x1f,
            SP_DBG_ECO_CNTL = 0x0,
            RB_DBG_ECO_CNTL = 0x04100000,
            RB_DBG_ECO_CNTL_blit = 0x05100000,
            HLSQ_DBG_ECO_CNTL = 0x00080000,
            RB_UNKNOWN_8E01 = 0x00000001,
            VPC_DBG_ECO_CNTL = 0x0,
            UCHE_UNKNOWN_0E12 = 0x10000001
        )
    ))

add_gpus([
        GPUId(640),
    ], A6xxGPUInfo(
        CHIP.A6XX,
        a6xx_gen2,
        num_ccu = 2,
        tile_align_w = 32,
        tile_align_h = 16,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        magic_regs = dict(
            PC_POWER_CNTL = 1,
            TPL1_DBG_ECO_CNTL = 0x00008000,
            GRAS_DBG_ECO_CNTL = 0x0,
            SP_CHICKEN_BITS = 0x00000420,
            UCHE_CLIENT_PF = 0x00000004,
            PC_MODE_CNTL = 0x1f,
            SP_DBG_ECO_CNTL = 0x0,
            RB_DBG_ECO_CNTL = 0x04100000,
            RB_DBG_ECO_CNTL_blit = 0x04100000,
            HLSQ_DBG_ECO_CNTL = 0x0,
            RB_UNKNOWN_8E01 = 0x00000001,
            VPC_DBG_ECO_CNTL = 0x02000000,
            UCHE_UNKNOWN_0E12 = 0x00000001
        )
    ))

add_gpus([
        GPUId(680),
    ], A6xxGPUInfo(
        CHIP.A6XX,
        a6xx_gen2,
        num_ccu = 4,
        tile_align_w = 64,
        tile_align_h = 32,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        magic_regs = dict(
            PC_POWER_CNTL = 3,
            TPL1_DBG_ECO_CNTL = 0x00108000,
            GRAS_DBG_ECO_CNTL = 0x0,
            SP_CHICKEN_BITS = 0x00001430,
            UCHE_CLIENT_PF = 0x00000004,
            PC_MODE_CNTL = 0x1f,
            SP_DBG_ECO_CNTL = 0x0,
            RB_DBG_ECO_CNTL = 0x04100000,
            RB_DBG_ECO_CNTL_blit = 0x04100000,
            HLSQ_DBG_ECO_CNTL = 0x0,
            RB_UNKNOWN_8E01 = 0x00000001,
            VPC_DBG_ECO_CNTL = 0x02000000,
            UCHE_UNKNOWN_0E12 = 0x00000001
        )
    ))

add_gpus([
        GPUId(650),
    ], A6xxGPUInfo(
        CHIP.A6XX,
        a6xx_gen3,
        num_ccu = 3,
        tile_align_w = 96,
        tile_align_h = 16,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        magic_regs = dict(
            PC_POWER_CNTL = 2,
            # this seems to be a chicken bit that fixes cubic filtering:
            TPL1_DBG_ECO_CNTL = 0x01008000,
            GRAS_DBG_ECO_CNTL = 0x0,
            SP_CHICKEN_BITS = 0x00001400,
            UCHE_CLIENT_PF = 0x00000004,
            PC_MODE_CNTL = 0x1f,
            SP_DBG_ECO_CNTL = 0x01000000,
            RB_DBG_ECO_CNTL = 0x04100000,
            RB_DBG_ECO_CNTL_blit = 0x04100000,
            HLSQ_DBG_ECO_CNTL = 0x0,
            RB_UNKNOWN_8E01 = 0x0,
            VPC_DBG_ECO_CNTL = 0x02000000,
            UCHE_UNKNOWN_0E12 = 0x00000001
        )
    ))

add_gpus([
        GPUId(chip_id=0x00be06030500, name="Adreno 8c Gen 3"),
        GPUId(chip_id=0x007506030500, name="Adreno 7c+ Gen 3"),
        GPUId(chip_id=0x006006030500, name="Adreno 7c+ Gen 3 Lite"),
        # fallback wildcard entry should be last:
        GPUId(chip_id=0xffff06030500, name="Adreno 7c+ Gen 3"),
    ], A6xxGPUInfo(
        CHIP.A6XX,
        a6xx_gen4,
        num_ccu = 2,
        tile_align_w = 32,
        tile_align_h = 16,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        magic_regs = dict(
            PC_POWER_CNTL = 1,
            TPL1_DBG_ECO_CNTL = 0x05008000,
            GRAS_DBG_ECO_CNTL = 0x0,
            SP_CHICKEN_BITS = 0x00001400,
            UCHE_CLIENT_PF = 0x00000084,
            PC_MODE_CNTL = 0x1f,
            SP_DBG_ECO_CNTL = 0x00000006,
            RB_DBG_ECO_CNTL = 0x04100000,
            RB_DBG_ECO_CNTL_blit = 0x04100000,
            HLSQ_DBG_ECO_CNTL = 0x0,
            RB_UNKNOWN_8E01 = 0x0,
            VPC_DBG_ECO_CNTL = 0x02000000,
            UCHE_UNKNOWN_0E12 = 0x00000001
        )
    ))

add_gpus([
        GPUId(660),
    ], A6xxGPUInfo(
        CHIP.A6XX,
        a6xx_gen4,
        num_ccu = 3,
        tile_align_w = 96,
        tile_align_h = 16,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        magic_regs = dict(
            PC_POWER_CNTL = 2,
            TPL1_DBG_ECO_CNTL = 0x05008000,
            GRAS_DBG_ECO_CNTL = 0x0,
            SP_CHICKEN_BITS = 0x00001400,
            UCHE_CLIENT_PF = 0x00000084,
            PC_MODE_CNTL = 0x1f,
            SP_DBG_ECO_CNTL = 0x01000000,
            RB_DBG_ECO_CNTL = 0x04100000,
            RB_DBG_ECO_CNTL_blit = 0x04100000,
            HLSQ_DBG_ECO_CNTL = 0x0,
            RB_UNKNOWN_8E01 = 0x0,
            VPC_DBG_ECO_CNTL = 0x02000000,
            UCHE_UNKNOWN_0E12 = 0x00000001
        )
    ))

add_gpus([
        GPUId(690),
    ], A6xxGPUInfo(
        CHIP.A6XX,
        a6xx_gen4,
        num_ccu = 8,
        tile_align_w = 64,
        tile_align_h = 32,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        magic_regs = dict(
            PC_POWER_CNTL = 7,
            TPL1_DBG_ECO_CNTL = 0x01008000,
            GRAS_DBG_ECO_CNTL = 0x0,
            SP_CHICKEN_BITS = 0x00001400,
            UCHE_CLIENT_PF = 0x00000084,
            PC_MODE_CNTL = 0x1f,
            SP_DBG_ECO_CNTL = 0x00000000,
            RB_DBG_ECO_CNTL = 0x00100000,
            RB_DBG_ECO_CNTL_blit = 0x00100000,  # ???
            HLSQ_DBG_ECO_CNTL = 0x0,
            RB_UNKNOWN_8E01 = 0x0,
            VPC_DBG_ECO_CNTL = 0x02000000,
            UCHE_UNKNOWN_0E12 = 0x00000001
        )
    ))

# Minimal definition needed for ir3 assembler/disassembler
add_gpus([
        GPUId(730),
        GPUId(740),
    ], A6xxGPUInfo(
        CHIP.A7XX,
        a6xx_gen4,
        num_ccu = 4,
        tile_align_w = 64,
        tile_align_h = 32,
        num_vsc_pipes = 32,
        cs_shared_mem_size = 32 * 1024,
        wave_granularity = 2,
        magic_regs = dict()
    ))

template = """\
/* Copyright (C) 2021 Google, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "freedreno_dev_info.h"

/* Map python to C: */
#define True true
#define False false

%for info in s.gpu_infos:
static const struct fd_dev_info __info${s.info_index(info)} = ${str(info)};
%endfor

static const struct fd_dev_rec fd_dev_recs[] = {
%for id, info in s.gpus.items():
   { {${id.gpu_id}, ${hex(id.chip_id)}}, "${id.name}", &__info${s.info_index(info)} },
%endfor
};
"""

print(Template(template).render(s=s))

