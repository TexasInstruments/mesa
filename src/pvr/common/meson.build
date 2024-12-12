#  Copyright (c) Imagination Technologies Ltd.
#  SPDX-License-Identifier: MIT

libpvrcommon = static_library(
  'pvrcommon',
  [
    files(
      'pvr_common_display.c'
    ),
  ],
  include_directories : [
    inc_src,
  ],
  gnu_symbol_visibility : 'hidden',
  dependencies : [
    dep_libdrm,
    idep_mesautil,
  ],
)
