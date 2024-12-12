#  Copyright (c) Imagination Technologies Ltd.
#  SPDX-License-Identifier: MIT

libpvrwinsys = static_library(
  'pvrwinsys',
  [
    files(
      'pvr_ddk_winsys.c',
      'pvr_dri_compat.c'
    ),
  ],
  include_directories : [
    inc_src, inc_include, inc_gallium, inc_gallium_aux, inc_gallium_drivers,
  ],
  gnu_symbol_visibility : 'hidden',
  dependencies : [
    dep_libdrm,
    idep_mesautil,
  ],
)
