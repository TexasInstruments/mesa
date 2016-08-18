# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

inc_wayland_drm = include_directories('.')

wayland_drm_protocol_c = custom_target(
  'wayland-drm-protocol.c',
  input : 'wayland-drm.xml',
  output : 'wayland-drm-protocol.c',
  command : [prog_wl_scanner, wl_scanner_arg, '@INPUT@', '@OUTPUT@'],
)

wayland_drm_client_protocol_h = custom_target(
  'wayland-drm-client-protocol.h',
  input : 'wayland-drm.xml',
  output : 'wayland-drm-client-protocol.h',
  command : [prog_wl_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

wayland_drm_server_protocol_h = custom_target(
  'wayland-drm-server-protocol.h',
  input : 'wayland-drm.xml',
  output : 'wayland-drm-server-protocol.h',
  command : [prog_wl_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)

libwayland_drm = static_library(
  'wayland_drm',
  ['wayland-drm.c', wayland_drm_protocol_c, wayland_drm_server_protocol_h],
  gnu_symbol_visibility : 'hidden',
  dependencies : [dep_wayland_server],
  include_directories : inc_include,
  build_by_default : false,
)

install_data('wayland-drm.xml')

pkg.generate(
  filebase : 'wayland-drm',
  name : 'Mesa Wayland Protocols',
  description : 'Mesa Wayland protocol files',
  version : meson.project_version(),
  variables : [
     'datarootdir=${prefix}/' + get_option('datadir'),
     'pkgdatadir=${pc_sysrootdir}${datarootdir}/' + meson.project_name(),
  ],
  install_dir : '@0@/@1@/pkgconfig'.format(get_option('prefix'),
                get_option('datadir')),
)
