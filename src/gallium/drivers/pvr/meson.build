#  Copyright (c) Imagination Technologies Ltd.
#  SPDX-License-Identifier: MIT

libpvr_files = files(
  'pvr_context.c',
  'pvr_drawable.c',
  'pvr_dri_callbacks.c',
  'pvr_fence.c',
  'pvr_mesa_dispatch.c',
  'pvr_resource.c',
  'pvr_screen.c',
)

libpvr_includes = [
  inc_mapi,
  inc_mesa,
  inc_gallium,
  inc_gallium_aux,
  inc_gallium_winsys,
  inc_include,
  inc_src,
]

libpvr_compile_args = [
  '-DGALLIUM_PVR',
]

libpvr_dependencies = [
  dep_libdrm,
  idep_mesautil,
]

libpvr = static_library(
  'pvr',
  libpvr_files,
  include_directories: libpvr_includes,
  c_args: libpvr_compile_args,
  gnu_symbol_visibility : 'hidden',
  dependencies: libpvr_dependencies,
)

driver_pvr = declare_dependency(
  compile_args: '-DGALLIUM_PVR',
  link_with: [
    libpvrcommon,
    libpvrwinsys,
    libpvr,
  ],
)
