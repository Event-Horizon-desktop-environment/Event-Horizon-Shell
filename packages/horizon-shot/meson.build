project('horizon-shot', 'c', 'cpp',
  default_options: ['c_std=c17', 'cpp_std=c++23', 'warning_level=2'],
  version: '0.1.0',
)

wayland_client = dependency('wayland-client')
wayland_scanner = find_program('wayland-scanner', required: true)
cairo = dependency('cairo')
xkbcommon = dependency('xkbcommon')
libdrm = dependency('libdrm')

libpng = dependency('libpng', required: false)
libjxl = dependency('libjxl', required: false)
librsvg = dependency('librsvg-2.0', required: false)

fs = import('fs')

protocol_headers = []
protocol_sources = []

foreach p : [
  'color-management-v1',
  'ext-data-control-v1',
  'ext-foreign-toplevel-list-v1',
  'ext-image-capture-source-v1',
  'ext-image-copy-capture-v1',
  'fractional-scale-v1',
  'linux-dmabuf-v1',
  'single-pixel-buffer-v1',
  'viewporter',
  'wlr-data-control-unstable-v1',
  'wlr-foreign-toplevel-management-unstable-v1',
  'wlr-screencopy-unstable-v1',
  'xdg-output-unstable-v1',
  'xdg-shell',
]
  xml = 'protocols' / (p + '.xml')
  h = custom_target(p + '-client-header',
    input: xml,
    output: p + '-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
  c = custom_target(p + '-client-source',
    input: xml,
    output: p + '-client-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
    depends: [h],
  )
  protocol_headers += h
  protocol_sources += c
endforeach

wlr_ls_xml = 'protocols' / 'wlr-layer-shell-unstable-v1.xml'
wlr_ls_h = custom_target('wlr-layer-shell-unstable-v1-client-header',
  input: wlr_ls_xml,
  output: 'wlr-layer-shell-unstable-v1-client-protocol.h',
  command: [
    'sh', '-c',
    '"$1" client-header "$2" "$3" && sed s/namespace/name_space/g "$3" > "$3.tmp" && mv "$3.tmp" "$3"',
    '_', wayland_scanner.full_path(), '@INPUT@', '@OUTPUT@',
  ],
)
wlr_ls_c = custom_target('wlr-layer-shell-unstable-v1-client-source',
  input: wlr_ls_xml,
  output: 'wlr-layer-shell-unstable-v1-client-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  depends: [wlr_ls_h],
)
protocol_headers += wlr_ls_h
protocol_sources += wlr_ls_c

inc = include_directories('src', 'third_party/stb')

core_sources = files(
  'src/core/memfd.cpp',
  'src/core/logging.cpp',
  'src/core/shm_buffer.cpp',
  'src/core/wayland_seat.cpp',
  'src/core/clipboard.cpp',
  'src/core/foreign_toplevels.cpp',
  'src/core/wlr_foreign_toplevels.cpp',
  'src/core/wayland_connection.cpp',
  'src/core/color_info.cpp',
  'src/core/icon_cache.cpp',
  'src/core/screencopy.cpp',
)

screenshot_sources = files(
  'src/main.cpp',
  'src/screenshot/app.cpp',
  'src/screenshot/ui/paint.cpp',
  'src/screenshot/ui/layout.cpp',
  'src/screenshot/ui/input.cpp',
  'src/screenshot/ui/button/button.cpp',
  'src/screenshot/capture/capture.cpp',
  'src/screenshot/capture/drm_kms.cpp',
)

cpp_args = []
if libpng.found()
  cpp_args += '-DEH_HAVE_LIBPNG'
endif
if libjxl.found()
  cpp_args += '-DEH_HAVE_LIBJXL'
endif
if librsvg.found()
  cpp_args += '-DEH_HAVE_RSVG'
endif

all_deps = [wayland_client, cairo, xkbcommon, libdrm]
if libpng.found()
  all_deps += libpng
endif
if libjxl.found()
  all_deps += libjxl
endif
if librsvg.found()
  all_deps += librsvg
endif

executable('horizon-shot',
  core_sources + screenshot_sources + protocol_headers + protocol_sources,
  include_directories: inc,
  dependencies: all_deps,
  cpp_args: cpp_args,
  install: true,
)

install_data('data/horizon-shot.desktop',
  install_dir: get_option('datadir') / 'applications',
)

install_data('data/Horizon-Shot.svg',
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps',
  rename: 'horizon-shot.svg',
)
