#  Copyright (c) Imagination Technologies Ltd.
#  SPDX-License-Identifier: MIT

libpvrwinsys_c_args = []

if with_gallium_pvr_alias
  libpvrwinsys_c_args += '-DGALLIUM_PVR_ALIAS=@0@'.format(gallium_pvr_alias)
endif

libpvrwinsys = static_library(
  'pvrwinsys',
  [
    files(
      'pvr_ddk_winsys.c',
      'pvr_dri_compat.c',
      'pvr_driver_compat.c'
    ),
  ],
  include_directories : [
    inc_src, inc_include, inc_gallium, inc_gallium_aux, inc_gallium_drivers,
  ],
  c_args: libpvrwinsys_c_args,
  gnu_symbol_visibility : 'hidden',
  dependencies : [
    dep_libdrm,
    idep_mesautil,
  ],
)
