if not dep_archive.found()
  error('libarchive is required to build horizon-archive')
endif

gui_deps_ok = dep_wayland.found() and dep_cairo.found() and dep_xkbcommon.found()

if gui_deps_ok
  # Generate protocol code for xdg-shell
  xdg_shell_xml = join_paths(
    dependency('wayland-protocols').get_variable('pkgdatadir'),
    'stable', 'xdg-shell', 'xdg-shell.xml')

  xdg_shell_client_h = custom_target('xdg-shell-client-h',
    output: 'xdg-shell-client-protocol.h',
    input: xdg_shell_xml,
    command: ['wayland-scanner', 'client-header', '@INPUT@', '@OUTPUT@'])

  xdg_shell_client_c = custom_target('xdg-shell-client-c',
    output: 'xdg-shell-protocol.c',
    input: xdg_shell_xml,
    command: ['wayland-scanner', 'private-code', '@INPUT@', '@OUTPUT@'])

  archive_sources = files(
    'main.cpp',
    'core/archive.cpp',
    'ui/gui.cpp',
    'fb/fb_panel.cpp',
    'fb/fb_icons.cpp',
  )

  gui_cpp_args = ['-DHORIZON_HAS_GUI=1']

  archive_deps = [
    dep_archive,
    dep_openssl,
    dep_wayland,
    dep_cairo,
    dep_xkbcommon,
  ]

  extra_sources = [xdg_shell_client_h, xdg_shell_client_c]
else
  archive_sources = files(
    'main.cpp',
    'core/archive.cpp',
  )

  archive_deps = [
    dep_archive,
    dep_openssl,
  ]

  extra_sources = []
endif

all_cpp_args = cpp_args
if gui_deps_ok
  all_cpp_args += gui_cpp_args
endif

executable('horizon-archive',
  archive_sources, extra_sources,
  include_directories: inc,
  cpp_args: all_cpp_args,
  dependencies: archive_deps,
  install: true,
)

# Desktop entry
install_data('horizon-archive.desktop',
  install_dir: join_paths(get_option('datadir'), 'applications'))

# Icon
install_data(
  join_paths(meson.project_source_root(), 'assets', 'icons', 'Horizon Archive Manager.svg'),
  install_dir: join_paths(get_option('datadir'), 'icons', 'hicolor', 'scalable', 'apps'),
  rename: 'horizon-archive-manager.svg',
)
