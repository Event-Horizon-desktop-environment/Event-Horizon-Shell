project('horizon-photo-viewer', 'c', 'cpp',
  version: '0.1.0',
  meson_version: '>=1.1',
  default_options: [
    'cpp_std=c++23',
    'warning_level=3',
    'buildtype=debugoptimized',
  ])

if meson.get_compiler('cpp').get_id() == 'gcc' and meson.get_compiler('cpp').version().version_compare('<13')
  error('requires GCC >= 13 for C++23')
endif

# --- Dependencies ---
wl_client = dependency('wayland-client', version: '>=1.20')
wl_protocols = dependency('wayland-protocols', version: '>=1.36')
xkbcommon = dependency('xkbcommon', version: '>=1.5')
dbus_1 = dependency('dbus-1', version: '>=1.12')
cairo = dependency('cairo', version: '>=1.16')
threads = dependency('threads')

# Find wayland-scanner
wl_scanner = find_program('wayland-scanner', required: true)

# --- Protocols ---
subdir('protocol')

# --- Source ---
subdir('src')

# --- Install data files ---
install_data('assets/com.horizon.photo-viewer.desktop',
  install_dir: get_option('datadir') / 'applications',
)

install_data('assets/com.horizon.photo-viewer.svg',
  install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps',
)

install_data('assets/crop.svg',
  install_dir: get_option('datadir') / 'horizon-photo-viewer',
)

install_data('assets/flip.svg',
  install_dir: get_option('datadir') / 'horizon-photo-viewer',
)

# Screenshot toolbar SVG icons
install_data('assets/screen.svg',
  install_dir: get_option('datadir') / 'horizon-photo-viewer',
)
install_data('assets/window.svg',
  install_dir: get_option('datadir') / 'horizon-photo-viewer',
)
install_data('assets/focused.svg',
  install_dir: get_option('datadir') / 'horizon-photo-viewer',
)
install_data('assets/selection.svg',
  install_dir: get_option('datadir') / 'horizon-photo-viewer',
)
install_data('assets/copy.svg',
  install_dir: get_option('datadir') / 'horizon-photo-viewer',
)
install_data('assets/panel.svg',
  install_dir: get_option('datadir') / 'horizon-photo-viewer',
)

install_data('assets/upload.svg',
  install_dir: get_option('datadir') / 'horizon-photo-viewer',
)

# Install icon font for toolbar buttons
icon_font_install_dir = get_option('datadir') / 'horizon-photo-viewer' / 'fonts'
install_data('assets/fonts/MF/MaterialSymbolsRounded.ttf',
  install_dir: icon_font_install_dir,
)
