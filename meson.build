project(
  'event-horizon',
  ['c', 'cpp'],
  version: '0.9.5-beta1',
  meson_version: '>=1.8.5',
  default_options: [
    'prefix=/usr',
    'warning_level=3',
    'werror=true',
    'cpp_std=c++23',
    'b_ndebug=if-release',
  ],
)

cc = meson.get_compiler('cpp')

# Release: trim dead sections and avoid linking unused shared libs (smaller binary / fewer mapped deps).
if get_option('buildtype') == 'release'
  add_project_arguments(
    '-ffunction-sections',
    '-fdata-sections',
    language: ['c', 'cpp'],
  )
  add_project_link_arguments(
    '-Wl,--gc-sections',
    '-Wl,--as-needed',
    language: ['c', 'cpp'],
  )
  if get_option('release_omit_frame_pointer')
    if cc.has_argument('-fomit-frame-pointer')
      add_project_arguments('-fomit-frame-pointer', language: ['c', 'cpp'])
    endif
  endif
else
  # Non-release: split debug info into .dwo files so the linker doesn't touch debug data.
  # Saves ~40% linker RAM. GDB still reads .dwo automatically.
  add_project_arguments('-gsplit-dwarf', language: ['c', 'cpp'])

  if get_option('native_march')
    _native_ok = true
    foreach _a : ['-march=native', '-mtune=native']
      if not cc.has_argument(_a)
        _native_ok = false
endif

    endforeach
    if _native_ok
      add_project_arguments('-march=native', '-mtune=native', language: ['c', 'cpp'])
    else
      warning('native_march=true but compiler rejected -march=native or -mtune=native; skipping')
    endif
  endif
endif

sdbus_dep = dependency('sdbus-c++', required: false)
if not sdbus_dep.found()
  sdbus_dep = dependency('libsdbus-c++', required: true)
endif

wayland_scanner = find_program('wayland-scanner', required: true)

# ── Wayland protocol generation (scanner private-code + client-header) ───────
fs = import('fs')

# Standalone app drawer / spotlight entrypoints (`File` for `executable()` sources list).


# Prefer wayland-protocols pkgdatadir over vendored paths.
wayland_protos_dep = dependency('wayland-protocols', required: false)
if wayland_protos_dep.found()
  wayland_protos_dir = wayland_protos_dep.get_variable('pkgdatadir')
else
  wayland_protos_dir = meson.current_source_dir() / 'third_party' / 'wayland-protocols'
endif

protocol_headers = []
protocol_sources = []

foreach _p : [
  # ── system wayland-protocols ────────────────────────────────────────────────
  ['xdg-shell',
    wayland_protos_dir / 'stable/xdg-shell/xdg-shell.xml'],
  ['xdg-output-unstable-v1',
    wayland_protos_dir / 'unstable/xdg-output/xdg-output-unstable-v1.xml'],
  ['xdg-activation-v1',
    wayland_protos_dir / 'staging/xdg-activation/xdg-activation-v1.xml'],
  ['idle-inhibit-unstable-v1',
    wayland_protos_dir / 'unstable/idle-inhibit/idle-inhibit-unstable-v1.xml'],
  ['ext-session-lock-v1',
    wayland_protos_dir / 'staging/ext-session-lock/ext-session-lock-v1.xml'],
  ['ext-workspace-v1',
    wayland_protos_dir / 'staging/ext-workspace/ext-workspace-v1.xml'],
  ['ext-idle-notify-v1',
    wayland_protos_dir / 'staging/ext-idle-notify/ext-idle-notify-v1.xml'],
  ['ext-background-effect-v1',
    wayland_protos_dir / 'staging/ext-background-effect/ext-background-effect-v1.xml'],
  ['ext-data-control-v1',
    wayland_protos_dir / 'staging/ext-data-control/ext-data-control-v1.xml'],
  ['ext-foreign-toplevel-list-v1',
    wayland_protos_dir / 'staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml'],
  ['fractional-scale-v1',
    wayland_protos_dir / 'staging/fractional-scale/fractional-scale-v1.xml'],
  ['viewporter',
    wayland_protos_dir / 'stable/viewporter/viewporter.xml'],
  ['presentation-time',
    wayland_protos_dir / 'stable/presentation-time/presentation-time.xml'],
  ['single-pixel-buffer-v1',
    wayland_protos_dir / 'staging/single-pixel-buffer/single-pixel-buffer-v1.xml'],
  ['xdg-dialog-v1',
    wayland_protos_dir / 'staging/xdg-dialog/xdg-dialog-v1.xml'],
  ['pointer-constraints-unstable-v1',
    wayland_protos_dir / 'unstable/pointer-constraints/pointer-constraints-unstable-v1.xml'],
  ['relative-pointer-unstable-v1',
    wayland_protos_dir / 'unstable/relative-pointer/relative-pointer-unstable-v1.xml'],
  ['keyboard-shortcuts-inhibit-unstable-v1',
    wayland_protos_dir / 'unstable/keyboard-shortcuts-inhibit/keyboard-shortcuts-inhibit-unstable-v1.xml'],
  ['linux-dmabuf-v1',
    wayland_protos_dir / 'stable/linux-dmabuf/linux-dmabuf-v1.xml'],
  ['ext-image-capture-source-v1',
    wayland_protos_dir / 'staging/ext-image-capture-source/ext-image-capture-source-v1.xml'],
  ['ext-image-copy-capture-v1',
    wayland_protos_dir / 'staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml'],
  ['color-management-v1',
    wayland_protos_dir / 'staging/color-management/color-management-v1.xml'],

  # ── vendored XMLs in this repo ─────────────────────────────────────────────
  ['wlr-foreign-toplevel-management-unstable-v1',
    'protocols/wlr-foreign-toplevel-management-unstable-v1.xml'],
  ['wlr-data-control-unstable-v1',
    'protocols/wlr-data-control-unstable-v1.xml'],
  ['virtual-keyboard-unstable-v1',
    'protocols/virtual-keyboard-unstable-v1.xml'],
  ['dwl-ipc-unstable-v2',
    'protocols/dwl-ipc-unstable-v2.xml'],
  ['wlr-screencopy-unstable-v1',
    'protocols/wlr-screencopy-unstable-v1.xml'],
  ['wlr-gamma-control-unstable-v1',
    'protocols/wlr-gamma-control-unstable-v1.xml'],
  ['hyprland-toplevel-mapping-v1',
    'protocols/hyprland-toplevel-mapping-v1.xml'],
  ['hyprland-toplevel-export-v1',
    'protocols/hyprland-toplevel-export-v1.xml'],
  ['tearing-control-v1',
    wayland_protos_dir / 'staging/tearing-control/tearing-control-v1.xml'],
]
  _name = _p[0]
  _xml  = _p[1]

  if not fs.exists(_xml)
    message('wayland protocol xml missing; skipping: ' + _name + ' (' + _xml + ')')
    continue
  endif

  _h = custom_target(_name + '-client-header',
    input:   _xml,
    output:  _name + '-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
  _c = custom_target(_name + '-client-source',
    input:   _xml,
    output:  _name + '-client-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
    depends: _h,
  )

  protocol_headers += _h
  protocol_sources += _c
endforeach

# wlr-layer-shell: 'namespace' is a C++ keyword; rename it in the generated
# header only (private-code is compiled as C so it's fine as-is).
_wlr_ls_xml = 'protocols/wlr-layer-shell-unstable-v1.xml'
if fs.exists(_wlr_ls_xml)
  _wlr_ls_h = custom_target('wlr-layer-shell-unstable-v1-client-header',
    input:   _wlr_ls_xml,
    output:  'wlr-layer-shell-unstable-v1-client-protocol.h',
    command: [
      'sh', '-c',
      '"$1" client-header "$2" "$3" && sed s/namespace/name_space/g "$3" > "$3.tmp" && mv "$3.tmp" "$3"',
      '_', wayland_scanner.full_path(), '@INPUT@', '@OUTPUT@',
    ],
  )
  _wlr_ls_c = custom_target('wlr-layer-shell-unstable-v1-client-source',
    input:   _wlr_ls_xml,
    output:  'wlr-layer-shell-unstable-v1-client-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
    depends: _wlr_ls_h,
  )
  protocol_headers += _wlr_ls_h
  protocol_sources += _wlr_ls_c
else
  message('wayland protocol xml missing; skipping: wlr-layer-shell-unstable-v1 (' + _wlr_ls_xml + ')')
endif

tomlplusplus_dep = declare_dependency(include_directories: include_directories('third_party/tomlplusplus/include'))

ffmpegthumbnailer_dep = dependency('libffmpegthumbnailer')

lz4_dep = dependency('liblz4', required: false)
if not lz4_dep.found()
  lz4_proj = subproject('lz4')
  lz4_dep = lz4_proj.get_variable('lz4_dep')
endif

libwebp_dep = dependency('libwebp', required: false)
if not libwebp_dep.found()
  libwebp_proj = subproject('libwebp')
  libwebp_dep = libwebp_proj.get_variable('libwebp_dep')
endif

deps = [
  dependency('wayland-client', required: true),
  wayland_protos_dep,
  dependency('cairo', required: true),
  dependency('pango', required: true),
  dependency('pangocairo', required: true),
  dependency('pangoft2', required: true),
  dependency('freetype2', required: true),
  dependency('fontconfig', required: true),
  dependency('xkbcommon', required: true),
  sdbus_dep,
  dependency('libpipewire-0.3', required: true),
  dependency('pam', required: true),
  dependency('libcurl', required: true),
  libwebp_dep,
  dependency('libcrypto', required: true),
  lz4_dep,
]

# ── jemalloc allocator (optional, glibc only) ─────────────────────────────────
# Reduces memory fragmentation in long-running sessions. On glibc without
# jemalloc we fall back to mallopt(M_ARENA_MAX, 2) at startup.
jemalloc_opt = get_option('jemalloc')
is_glibc = cc.get_define('__GLIBC__', prefix: '#include <features.h>') != ''
if jemalloc_opt.enabled() and not is_glibc
  error('jemalloc support requires glibc')
endif
jemalloc_dep = dependency('jemalloc', required: false)
if not jemalloc_dep.found() and jemalloc_opt.enabled()
  error('jemalloc was enabled via -Djemalloc=enabled but neither system nor subproject jemalloc was found')
endif
jemalloc_cpp_args = []
if jemalloc_dep.found()
  deps += jemalloc_dep
  jemalloc_cpp_args += '-DEH_USE_JEMALLOC=1'
endif
if is_glibc
  jemalloc_cpp_args += '-DEH_USE_GLIBC=1'
endif

# Vulkan WSI (required): Vulkan loader + headers + Wayland presentation support.
vulkan_dep = dependency('vulkan', required: true)
deps += vulkan_dep

# Fedora/RHEL: libjpeg-turbo-devel provides `libjpeg.pc` (IJG API), not `libjpeg-turbo.pc` — try that first
# to avoid Meson probing `libjpeg-turbo` (CMake noise + misleading "not found" before fallback).
jpeg_dep = disabler()
if get_option('jpeg')
  jpeg_dep = dependency('libjpeg', required: false)
  if not jpeg_dep.found()
    jpeg_dep = dependency('libjpeg-turbo', modules: ['libjpeg-turbo::jpeg'], required: false)
  endif
  if not jpeg_dep.found()
    jpeg_lib = cc.find_library('jpeg', required: false)
    if jpeg_lib.found() and cc.has_header('jpeglib.h')
      jpeg_dep = declare_dependency(dependencies: jpeg_lib)
    endif
  endif
  if not jpeg_dep.found()
    jpeg_proj = subproject('libjpeg-turbo')
    jpeg_dep = jpeg_proj.get_variable('jpeg_dep')
  endif
endif

rsvg_dep = dependency('librsvg-2.0', required: false)

libdrm_dep = dependency('libdrm', required: false)

# src: sources live under src/shell and src/ui
inc = include_directories(
  '.',
  'third_party',
  'third_party/wuffs/release/c',
  'src',
  'src/configuration',
  'src/desktop_shell',
  'src/desktop_shell/launchpad',
  'src/desktop_shell/Overview',
  'src/desktop_shell/unified',
  'src/desktop_shell/widgets/workspaces',
  'src/services/global_keyboard',
  'src/desktop_shell/widgets/popup',
  'src/desktop_shell/controlcenter',
  'src/desktop_shell/notifications',
  'src/desktop_shell/dock',
  'src/desktop_shell/spotlight',
  'src/desktop_shell/widgets/app_drawer',
  'src/desktop_shell/widgets/start_menu',
  'src/desktop_shell/widgets/shared',
  'src/services',
  'src/services/mpris',
  'src/services/tray',
  'src/services/notifications',
  'src/services/network',
  'src/services/bluetooth',
  'src/platform',
  'src/services/keyring',
  'src/services/udisks2',
  'src/services/autostart',
  'src/wl',
  'src/backends',
  'src/backends/hyprland',
  'src/backends/mango',
  'src/backends/niri',
  'src/backends/sway',
  'src/backends/ext_workspace',
  'src/ux',
  'src/ux/settings',
  'src/ux/settings/settings_tab_autostart',
  'src/ux/settings/settings_tab_wallpaper',
  'src/ux/disks',
)

# Optional: embed logo PNG + Material Symbols TTF into `libeh_bundled.a` (linked by shell).
bundled_sources = files('src/desktop_shell/common/glyph/bundled_assets.cpp')
bundled_cpp_args = []
if get_option('embed_assets')
  assert(
    fs.exists('src/Resources/Event-Horizon-logo.png'),
    'embed_assets=true requires src/Resources/Event-Horizon-logo.png',
  )
  assert(
    fs.exists('src/assets/fonts/MF/MaterialSymbolsRounded.ttf'),
    'embed_assets=true requires src/assets/fonts/MF/MaterialSymbolsRounded.ttf',
  )
  bundled_cpp_args += ['-DEH_EMBEDDED_ASSETS=1']
  py3 = find_program('python3', required: true)
  embedded_bundled_data = custom_target(
    'embedded_bundled_data',
    input: files(
      'src/Resources/Event-Horizon-logo.png',
      'src/assets/fonts/MF/MaterialSymbolsRounded.ttf',
    ),
    output: ['embedded_bundled_data.cpp', 'embedded_bundled_data.hpp'],
    command: [
      py3,
      meson.current_source_dir() / 'tools' / 'gen_embedded_assets.py',
      '@OUTPUT0@',
      '@OUTPUT1@',
      '@INPUT0@',
      '@INPUT1@',
    ],
  )
  bundled_sources += embedded_bundled_data
endif

# Generated `embedded_bundled_data.hpp` lives in the build root; Meson adds that include path for this library.
bundled_inc = inc
eh_bundled_lib = static_library(
  'eh_bundled',
  bundled_sources,
  include_directories: bundled_inc,
  cpp_args: bundled_cpp_args,
  dependencies: [dependency('cairo'), dependency('fontconfig')],
  override_options: ['optimization=1'],
  pic: true,
)
eh_bundled_dep = declare_dependency(link_with: eh_bundled_lib, include_directories: bundled_inc)

# ── Horizon Colors: Google material-color-utilities (C++, Apache-2.0) ────────
material_colors_sources = files(
  'third_party/material-color-utilities/cpp/blend/blend.cc',
  'third_party/material-color-utilities/cpp/cam/cam.cc',
  'third_party/material-color-utilities/cpp/cam/hct.cc',
  'third_party/material-color-utilities/cpp/cam/hct_solver.cc',
  'third_party/material-color-utilities/cpp/cam/viewing_conditions.cc',
  'third_party/material-color-utilities/cpp/contrast/contrast.cc',
  'third_party/material-color-utilities/cpp/dislike/dislike.cc',
  'third_party/material-color-utilities/cpp/dynamiccolor/dynamic_color.cc',
  'third_party/material-color-utilities/cpp/dynamiccolor/dynamic_scheme.cc',
  'third_party/material-color-utilities/cpp/dynamiccolor/material_dynamic_colors.cc',
  'third_party/material-color-utilities/cpp/palettes/tones.cc',
  'third_party/material-color-utilities/cpp/quantize/celebi.cc',
  'third_party/material-color-utilities/cpp/quantize/lab.cc',
  'third_party/material-color-utilities/cpp/quantize/wsmeans.cc',
  'third_party/material-color-utilities/cpp/quantize/wu.cc',
  'third_party/material-color-utilities/cpp/scheme/scheme_content.cc',
  'third_party/material-color-utilities/cpp/scheme/scheme_expressive.cc',
  'third_party/material-color-utilities/cpp/scheme/scheme_fidelity.cc',
  'third_party/material-color-utilities/cpp/scheme/scheme_fruit_salad.cc',
  'third_party/material-color-utilities/cpp/scheme/scheme_monochrome.cc',
  'third_party/material-color-utilities/cpp/scheme/scheme_neutral.cc',
  'third_party/material-color-utilities/cpp/scheme/scheme_rainbow.cc',
  'third_party/material-color-utilities/cpp/scheme/scheme_tonal_spot.cc',
  'third_party/material-color-utilities/cpp/scheme/scheme_vibrant.cc',
  'third_party/material-color-utilities/cpp/score/score.cc',
  'third_party/material-color-utilities/cpp/temperature/temperature_cache.cc',
  'third_party/material-color-utilities/cpp/utils/utils.cc',
)

material_colors_inc = include_directories('third_party/material-color-utilities')

# Vendored material-color-utilities references Abseil for exactly two things:
# `absl::flat_hash_map` (wsmeans quantizer) and `StrCat(absl::Hex(x))` (utils).
# Instead of linking any Abseil build — system or vendored, either way the
# binary ends up coupled to a libabsl SONAME that differs per distro and broke
# every packaged build — compile-time shims provide those two names over the
# C++ standard library. The submodule stays pristine; no libabsl at runtime.
mcu_absl_shim_inc = include_directories('third_party/absl-shim')

eh_material_colors_lib = static_library(
  'eh_material_colors',
  sources: material_colors_sources,
  include_directories: [material_colors_inc, mcu_absl_shim_inc],
  # Upstream uses C-style compound literals; suppress pedantic warnings for vendored code.
  cpp_args: ['-Wno-unused-parameter', '-Wno-extra-semi', '-Wno-pedantic', '-Wno-extra-semi-braces'],
  override_options: ['optimization=2', 'werror=false'],
  # Vendored headers are missing #include <optional>; force-include it via our
  # own precompiled header so the submodule stays pristine.
  cpp_pch: 'src/bootstrap/pch_material_colors.hpp',
  pic: true,
)

eh_material_colors_dep = declare_dependency(
  link_with: eh_material_colors_lib,
  include_directories: [material_colors_inc, mcu_absl_shim_inc],
)

shell_cpp_sources = [
  'src/color/horizon_colors.cpp',
  'src/configuration/shell_config.cpp',
  'src/desktop_shell/common/asset/asset_loader.cpp',
  'src/desktop_shell/common/palette/horizon_colors_palette.cpp',
  'src/desktop_shell/common/palette/horizon_colors_templates.cpp',
  'src/desktop_shell/common/palette/hyprland_border_matugen.cpp',
  'src/desktop_shell/common/palette/matugen_external_templates.cpp',
  'src/desktop_shell/common/palette/matugen_palette.cpp',
  'src/bootstrap/entry/app_bootstrap.cpp',
  'src/bootstrap/session/unified_shell.cpp',
  'src/bootstrap/session/detail/unified_shell_session.cpp',
  'src/bootstrap/entry/screenshot_mode.cpp',
  'src/bootstrap/entry/region_select.cpp',
  'src/bootstrap/thread/thread_dispatch.cpp',
  'src/bootstrap/thread/thread_pool.cpp',
  'src/bootstrap/loop/main_loop.cpp',
  'src/bootstrap/loop/poll_mux.cpp',
  'src/wl/core/memfd.cpp',
  'src/wl/core/icd_select.cpp',
  'src/wl/core/gpu_page_trim.cpp',
  'src/wl/buffer/cairo_cpu_buffer.cpp',
  'src/wl/buffer/damage_region.cpp',
   'src/wl/surface/vulkan_wayland.cpp',
   'src/wl/surface/vulkan_destruction_queue.cpp',
   'src/wl/buffer/shm_buffer.cpp',
  'src/wl/surface/surface_extensions.cpp',
  'src/wl/input/pointer_constraints.cpp',
  'src/wl/buffer/single_pixel_buffer.cpp',
  'src/wl/surface/tearing_control.cpp',
  'src/wl/input/keyboard_shortcuts_inhibit.cpp',
  'src/wl/capture/image_copy_capture.cpp',
  'src/wl/color/color_management.cpp',
  'src/wl/input/clipboard.cpp',
  'src/wl/input/virtual_keyboard.cpp',
  'src/wl/session/idle_inhibitor.cpp',
  'src/wl/toplevel/activation_service.cpp',
  'src/wl/session/session_lock_service.cpp',
  'src/wl/color/nightlight.cpp',
  'src/wl/session/idle_service.cpp',
  'src/wl/core/connection.cpp',
  'src/wl/toplevel/workspaces.cpp',
  'src/backends/interfaces/compositor_ipc.cpp',
  'src/backends/interfaces/power_actions.cpp',
  'src/backends/hyprland/hyprland_run_ctx.cpp',
  'src/backends/hyprland/hyprland_ev_handler.cpp',
  'src/backends/hyprland/hyprland_workspace_manager.cpp',
  'src/backends/mango/mango_run_ctx.cpp',
  'src/backends/mango/mango_kbd_backend.cpp',
  'src/backends/mango/mango_out_backend.cpp',
  'src/backends/mango/mango_workspace_manager.cpp',
  'src/backends/ext_workspace/ext_workspace_manager.cpp',
  'src/backends/niri/niri_run_ctx.cpp',
  'src/backends/niri/niri_workspace_manager.cpp',
  'src/backends/niri/niri_out_backend.cpp',
  'src/backends/niri/niri_kbd_backend.cpp',
  'src/backends/sway/sway_run_ctx.cpp',
  'src/backends/sway/sway_workspace_manager.cpp',
  'src/backends/sway/sway_out_backend.cpp',
  'src/backends/sway/sway_kbd_backend.cpp',
  'src/backends/hyprland/hyprland_out_impl.cpp',
  'src/backends/hyprland/hyprland_kbd_impl.cpp',
  'src/backends/hyprland/hyprland_win_id.cpp',
  'src/backends/hyprland/hyprland_window_tracker.cpp',
  'src/backends/triad/triad_run_ctx.cpp',
  'src/backends/triad/triad_workspace_manager.cpp',
  'src/backends/triad/triad_out_backend.cpp',
  'src/backends/triad/triad_kbd_backend.cpp',
  'src/wl/capture/screencopy_png.cpp',
  'src/wl/capture/toplevel_stream.cpp',
  'src/wl/core/seat.cpp',
  'src/wl/toplevel/foreign_toplevels.cpp',
  'src/wl/toplevel/ext_foreign_toplevels.cpp',
  'src/desktop_shell/unified/unified_layer_outputs.cpp',
  'src/desktop_shell/unified/unified_wayland_registry.cpp',
  'src/desktop_shell/shared/popup/popup_position.cpp',
  'src/desktop_shell/shared/popup/buffer/buffer.cpp',
  'src/desktop_shell/shared/popup/caret/caret.cpp',
  'src/desktop_shell/shared/popup/paint/finish.cpp',
  'src/desktop_shell/shared/popup/geometry/layout.cpp',
  'src/desktop_shell/shared/popup/geometry/margins.cpp',
  'src/desktop_shell/shared/popup/session/session.cpp',
  'src/desktop_shell/shared/popup/paint/paint_context_menu.cpp',
  'src/desktop_shell/shared/popup/dispatch/popup_dispatch.cpp',
  'src/desktop_shell/shared/popup/dispatch/popup_items.cpp',
  'src/desktop_shell/shared/settings/settings_io.cpp',
  'src/desktop_shell/shared/settings/settings_watch.cpp',
  'src/desktop_shell/shared/pins/pin_identity.cpp',
  'src/desktop_shell/shared/widgets/workspace_strip.cpp',
  'src/desktop_shell/dock/input/dock_position.cpp',
  'src/desktop_shell/dock/core/dock_bar.cpp',
  'src/desktop_shell/taskbar/layout/taskbar_position.cpp',
  'src/desktop_shell/taskbar/core/taskbar.cpp',
  'src/desktop_shell/taskbar/paint/taskbar_paint.cpp',
  'src/desktop_shell/taskbar/Features/taskbar_pinned.cpp',
  'src/desktop_shell/taskbar/Features/taskbar_pin_drag.cpp',
  'src/desktop_shell/common/fs/string_util.cpp',
  'src/desktop_shell/common/glyph/material_glyph.cpp',
  'src/desktop_shell/common/glyph/bundled_fonts.cpp',
  'src/desktop_shell/common/fs/shell_paths.cpp',
  'src/desktop_shell/common/mem/periodic_trim.cpp',
  'src/desktop_shell/common/time/mono_time.cpp',
  'src/desktop_shell/common/bench/shell_bench.cpp',
  'src/desktop_shell/unified/compositor_kind.cpp',
  'src/desktop_shell/desktop/entries/desktop_entries.cpp',
  'src/desktop_shell/dock/pinned/dock_pin_identity.cpp',
  'src/desktop_shell/dock/widgets/dock_widget_tokens.cpp',
  'src/desktop_shell/notifications/core/notifications.cpp',
  'src/services/notifications/notification_dbus_service.cpp',
  'src/services/notifications/notifications_spawn.cpp',
  'src/desktop_shell/notifications/host/notification_toast_host.cpp',
  'src/notifications/standalone/notifications_standalone.cpp',
  'src/desktop_shell/desktop/standalone/desktop_standalone.cpp',
  'src/desktop_shell/desktop/spawn/desktop_spawn.cpp',
  'src/desktop_shell/dock/standalone/dock_standalone.cpp',
  'src/desktop_shell/dock/spawn/dock_spawn.cpp',
  'src/desktop_shell/taskbar/standalone/taskbar_standalone.cpp',
  'src/desktop_shell/taskbar/spawn/taskbar_spawn.cpp',
  'src/desktop_shell/shared/toplevel/toplevel_tracker.cpp',

  'src/desktop_shell/controlcenter/input/control_center_hit.cpp',
  'src/desktop_shell/controlcenter/input/control_center_dispatch.cpp',
  'src/desktop_shell/controlcenter/persist/control_center_persist.cpp',
  'src/desktop_shell/controlcenter/paint/control_center_notifications_strip.cpp',
  'src/desktop_shell/controlcenter/paint/control_center_paint.cpp',
  'src/desktop_shell/controlcenter/layout/control_center_panel_geometry.cpp',
  'src/desktop_shell/controlcenter/paint/control_center_popup_paint.cpp',
  'src/desktop_shell/controlcenter/mixer/mixer_stream_icon_resolve.cpp',
  'src/desktop_shell/controlcenter/widgets/network/control_center_network_widget.cpp',
  'src/desktop_shell/controlcenter/widgets/bluetooth/control_center_bluetooth_widget.cpp',
  'src/desktop_shell/controlcenter/widgets/audio/control_center_audio_widget.cpp',
  'src/desktop_shell/controlcenter/widgets/mixer/control_center_mixer_widget.cpp',
  'src/desktop_shell/controlcenter/widgets/weather/control_center_weather_widget.cpp',

  'src/desktop_shell/dock/paint/dock_paint_widget_bar.cpp',
  'src/desktop_shell/dock/input/dock_pick.cpp',
  'src/desktop_shell/dock/launch/dock_launch_feedback.cpp',
  'src/desktop_shell/dock/input/dock_slot_dispatch.cpp',
  'src/desktop_shell/shared/system/input/shell_input.cpp',
  'src/desktop_shell/dock/tooltip/dock_tooltip.cpp',
  'src/desktop_shell/common/fs/trash_state.cpp',
  'src/desktop_shell/desktop/core/desktop_layer.cpp',
  'src/desktop_shell/desktop/core/desktop_app.cpp',
  'src/desktop_shell/desktop/core/desktop_marquee.cpp',
  'src/desktop_shell/desktop/icons/desktop_icons.cpp',
  'src/desktop_shell/desktop/icons/desktop_menu.cpp',
  'src/desktop_shell/desktop/icons/desktop_icon_menu.cpp',
  'src/desktop_shell/desktop/entries/desktop_open_with.cpp',
  'src/desktop_shell/desktop/entries/desktop_archive.cpp',
  'src/desktop_shell/desktop/widgets/shared/desktop_widget_host.cpp',
  'src/desktop_shell/desktop/widgets/shared/desktop_widget_layer.cpp',
  'src/desktop_shell/desktop/widgets/shared/desktop_widgets_preferences.cpp',
  'src/desktop_shell/desktop/widgets/clock/desktop_widget_clock.cpp',
  'src/desktop_shell/desktop/widgets/weather/desktop_widget_weather.cpp',
  'src/desktop_shell/desktop/widgets/weather_fancy/desktop_widget_weather_fancy.cpp',
  'src/desktop_shell/desktop/widgets/calendar/desktop_widget_calendar.cpp',
  'src/desktop_shell/desktop/widgets/system_monitor/desktop_widget_system_monitor.cpp',
  'src/desktop_shell/desktop/widgets/media_player/desktop_widget_media_player.cpp',
  'src/desktop_shell/desktop/widgets/media_compact/desktop_widget_media_compact.cpp',
  'src/desktop_shell/desktop/widgets/world_clock/desktop_widget_world_clock.cpp',
  'src/desktop_shell/desktop/widgets/world_clock/desktop_world_clock_settings.cpp',
  'src/desktop_shell/desktop/audio/cava_reader.cpp',
  'src/desktop_shell/desktop/audio/cava_config.cpp',
  'src/desktop_shell/desktop/entries/desktop_xdg_ops.cpp',
  'src/desktop_shell/desktop/core/desktop_pointer.cpp',
  'src/desktop_shell/desktop/core/desktop_preferences.cpp',
  'src/desktop_shell/shared/toplevel/toplevel_hooks.cpp',
  'src/desktop_shell/dock/paint/dock_raster_backend.cpp',
  'src/desktop_shell/widgets/clock/clock_paint.cpp',
  'src/desktop_shell/widgets/world_clock/world_clock_paint.cpp',
  'src/desktop_shell/widgets/weather/weather_paint.cpp',
  'src/desktop_shell/widgets/media/media_paint.cpp',
  'src/desktop_shell/widgets/battery/battery_paint.cpp',
  'src/desktop_shell/widgets/bluetooth/bluetooth_paint.cpp',
  'src/desktop_shell/widgets/popup/calendar/calendar_popup.cpp',
  'src/desktop_shell/widgets/popup/weather/weather_popup.cpp',
  'src/desktop_shell/widgets/popup/volume_mixer/volume_mixer_popup.cpp',
  'src/desktop_shell/widgets/popup/vpn/vpn_popup.cpp',
  'src/desktop_shell/widgets/popup/media_player/media_player_popup.cpp',
  'src/desktop_shell/widgets/workspaces/workspaces_paint.cpp',

  'src/desktop_shell/widgets/app_drawer/overlay/app_drawer_overlay.cpp',
  'src/desktop_shell/widgets/app_drawer/power/app_drawer_power_exec.cpp',
  'src/desktop_shell/widgets/app_drawer/power/app_drawer_power_modal.cpp',
  'src/desktop_shell/widgets/app_drawer/trace/app_drawer_trace.cpp',
  'src/desktop_shell/widgets/app_drawer/input/app_drawer_keyboard.cpp',
  'src/desktop_shell/widgets/app_drawer/input/app_drawer_dispatch.cpp',
  'src/desktop_shell/widgets/app_drawer/list/desktop_list.cpp',
  'src/desktop_shell/widgets/start_menu/start_menu.cpp',
  'src/desktop_shell/widgets/start_menu/popup_paint_start_menu.cpp',

  'src/platform/rfkill_helper.cpp',
  'src/services/network/core/network_manager_service.cpp',
  'src/services/bluetooth/bluez_service.cpp',
  'src/services/bluetooth/bluetooth_agent.cpp',
  'src/services/audio/pipewire_service.cpp',
  'src/services/network/types/network_secret_agent.cpp',
  'src/services/keyring/secret_service_daemon.cpp',
  'src/services/global_keyboard/global_keyboard_handler.cpp',
  'src/dialog/dialog_base.cpp',
  'src/dialog/file_chooser_dialog.cpp',
  'src/services/udisks2/udisks2_drive_service.cpp',
  'src/services/autostart/autostart_service.cpp',
  'src/services/mpris/mpris_player.cpp',
  'src/services/mpris/mpris_album_art.cpp',
  'src/desktop_shell/common/icon_cache/icon_cache.cpp',
  'src/desktop_shell/common/system_theming/system_theming_utils.cpp',
  'src/desktop_shell/common/system_theming/gtk_theming.cpp',
  'src/desktop_shell/common/system_theming/qt_theming.cpp',
  'src/desktop_shell/common/system_theming/plasma_theming.cpp',
  'src/desktop_shell/common/system_theming/cursor_theming.cpp',
  'src/desktop_shell/dock/pinned/dock_pinned.cpp',

  'src/desktop_shell/spotlight/search/spotlight_query.cpp',
  'src/desktop_shell/spotlight/search/spotlight_keyboard.cpp',
  'src/desktop_shell/spotlight/paint/popup_paint_spotlight.cpp',
  'src/desktop_shell/power_confirm/power_confirm.cpp',

  'src/desktop_shell/keyboard/keyboard_settings_apply.cpp',

  'src/desktop_shell/lockscreen/session_lock.cpp',

  'src/desktop_shell/launchpad/layer/launchpad_layer.cpp',
  'src/desktop_shell/launchpad/host/launchpad_host.cpp',
  'src/desktop_shell/launchpad/search/launchpad_search.cpp',
  'src/desktop_shell/Overview/overview_host.cpp',
  'src/desktop_shell/Overview/overview_painter.cpp',
  'src/desktop_shell/Overview/overview_input.cpp',
  'src/desktop_shell/Overview/overview_capture.cpp',
  'src/desktop_shell/Overview/overview_actions.cpp',
  'src/desktop_shell/Overview/overview_reveal.cpp',
  'src/services/tray/manager/tray.cpp',
  'src/services/tray/filter/tray_env_filter.cpp',
  'src/services/tray/dbus/tray_context_menu.cpp',
  'src/services/tray/dbus/tray_sni_icons.cpp',
  'src/services/tray/icon/tray_stream_icon.cpp',
  'src/services/tray/manager/tray_manager.cpp',
  'src/desktop_shell/common/registry/widget_registry.cpp',
  'src/desktop_shell/common/os_logo/os_logo.cpp',
  'src/desktop_shell/common/animation/animations.cpp',
  'src/desktop_shell/common/animation/pointer_velocity.cpp',
  'src/desktop_shell/osd/host/osd_host.cpp',
  'src/desktop_shell/osd/audio/osd_audio.cpp',
  'src/desktop_shell/osd/brightness/osd_brightness.cpp',
  'src/wallpaper/apply/wallpaper_apply.cpp',
  'src/wallpaper/raster/wallpaper_raster_decode.cpp',
  'src/wallpaper/core/wallpaper.cpp',
  'src/wallpaper/thumbnail/wallpaper_thumbnail.cpp',
  'src/wallpaper/thumbnail/wallpaper_thumbnail_service.cpp',
  'src/wallpaper/standalone/wallpaper_standalone.cpp',
  'src/ux/settings/utils/widget_picker/widget_picker.cpp',
  'src/ux/settings/utils/helpers/material_glyphs.cpp',
  'src/ux/settings/data/default_apps/settings_default_apps.cpp',
  'src/ux/settings/data/default_apps/settings_default_apps_apply.cpp',
  'src/ux/settings/data/monitors/settings_monitors.cpp',
  'src/ux/settings/data/mango/settings_mango_data.cpp',
  'src/ux/settings/data/hyprland/settings_hyprland_data.cpp',
  'src/ux/settings/utils/monitors/settings_monitors_drm_probe.cpp',
  'src/ux/settings/utils/monitors/settings_monitors_tab.cpp',
  'src/ux/settings/settings_app.cpp',
  'src/ux/settings/common/logo/settings_logo.cpp',
  'src/ux/settings/common/embed/settings_embed_lifecycle.cpp',
  'src/ux/settings/utils/gpu/settings_gpu.cpp',
  'src/ux/settings/utils/wallpaper/settings_wallpaper_thumbs.cpp',
  'src/ux/settings/utils/widget_picker/settings_widget_drag.cpp',
  'src/ux/settings/utils/sound/settings_sound_cache.cpp',
  'src/ux/settings/utils/helpers/settings_slider_appliers.cpp',
  'src/ux/settings/utils/scroll/settings_scroll.cpp',
  'src/ux/settings/utils/dropdown/settings_dropdown.cpp',
  'src/ux/settings/utils/events/settings_event_handlers.cpp',
  'src/ux/settings/settings_serialize.cpp',
  'src/ux/settings/settings_tab_bing/settings_tab_bing.cpp',
  'src/ux/settings/settings_tab_sound/settings_tab_sound.cpp',
  'src/ux/settings/settings_tab_launcher/settings_tab_launcher.cpp',
  'src/ux/settings/settings_tab_workspaces/settings_tab_workspaces.cpp',
  'src/ux/settings/settings_tab_dock/settings_tab_dock.cpp',
  'src/ux/settings/settings_tab_dock_appearance/settings_tab_dock_appearance.cpp',
  'src/ux/settings/settings_tab_taskbar/settings_tab_taskbar.cpp',
  'src/ux/settings/settings_tab_notifications/settings_tab_notifications.cpp',
  'src/ux/settings/settings_tab_appearance/settings_tab_appearance.cpp',
  'src/ux/settings/settings_tab_icons/settings_tab_icons.cpp',
  'src/ux/settings/settings_tab_themes/settings_tab_themes.cpp',
  'src/ux/settings/settings_tab_monitors/settings_tab_monitors.cpp',

  'src/ux/settings/settings_tab_default_apps/settings_tab_default_apps.cpp',
  'src/ux/settings/settings_tab_network/settings_tab_network.cpp',
  'src/ux/settings/settings_tab_network/tabs/settings_tab_wifi.cpp',
  'src/ux/settings/settings_tab_network/tabs/settings_tab_wired.cpp',
  'src/ux/settings/settings_tab_network/dialogs/wifi_password_prompt.cpp',
  'src/ux/settings/settings_tab_network/dialogs/keyring_password_prompt.cpp',
  'src/ux/settings/settings_tab_network/dialogs/vpn_add_dialog.cpp',
  'src/ux/settings/settings_tab_ui_layout/settings_tab_layout.cpp',
  'src/ux/settings/settings_tab_mango/settings_tab_mango.cpp',
  'src/ux/settings/settings_tab_hyprland/settings_tab_hyprland.cpp',
  'src/ux/settings/settings_tab_hyprland/widgets/hyprland_anim_popup.cpp',
  'src/ux/settings/settings_tab_hyprland/widgets/hyprland_bezier.cpp',
  'src/ux/settings/settings_tab_nightlight/settings_tab_nightlight.cpp',
  'src/ux/settings/settings_tab_desktop_widgets/settings_tab_desktop_widgets.cpp',
  'src/ux/settings/settings_tab_desktop_widgets/world_clock_popup.cpp',
  'src/ux/settings/settings_tab_desktop/settings_tab_desktop.cpp',
  'src/ux/settings/settings_tab_time/settings_tab_time.cpp',
  'src/ux/settings/settings_tab_keyboard/settings_tab_keyboard.cpp',
  'src/ux/settings/settings_tab_power/settings_tab_power.cpp',
  'src/ux/settings/settings_tab_bluetooth/settings_tab_bluetooth.cpp',
  'src/ux/settings/settings_tab_accounts/settings_tab_accounts.cpp',
  'src/ux/settings/settings_tab_autostart/settings_tab_autostart.cpp',
  'src/ux/settings/settings_tab_wallpaper/settings_tab_wallpaper.cpp',
  'src/ux/settings/settings_tab_color_themes/settings_tab_color_themes.cpp',
  'src/ux/settings/theme_manager.cpp',

  'src/services/bing/bing_wallpaper.cpp',
  'src/services/ipc/ipc_server.cpp',
  'src/services/ipc/ipc_protocol.cpp',
  'src/services/ipc/client.cpp',

  'src/services/windows/toplevel_types.cpp',
  'src/services/windows/toplevel_service.cpp',
  'src/services/windows/toplevel_bridge.cpp',
  'src/services/windows/toplevel_client.cpp',

  'src/services/vram_boost/vram_boost_detail.cpp',
  'src/services/vram_boost/vram_boost_manager.cpp',

  'src/desktop_shell/ui/theme.cpp',

  'src/desktop_shell/ui/button/ui_button.cpp',

  'src/desktop_shell/ui/slider/ui_slider.cpp',

  'src/desktop_shell/ui/dropdown/dropdown.cpp',

  'src/desktop_shell/ui/toggle/ui_toggle.cpp',

  'src/ux/Powermenu/power_menu.cpp',

  # ── Disk Utility (Horizon Disks) ─────────────────────────────────────
  'src/ux/disks/manager.cpp',
  'src/ux/disks/drive.cpp',
  'src/ux/disks/block.cpp',
]

# Shared objects for `EventHorizon`.
shell_core_sources = shell_cpp_sources + protocol_headers + protocol_sources

# Header-only JSON for VS Code User/settings.json merge (matugen → Material Code).
nlohmann_json_dep = declare_dependency(include_directories: include_directories('third_party/nlohmann/single_include'))

# Horizon Colors native color engine — already linked via eh_material_colors_dep above.

dl_dep = cc.find_library('dl', required: true)

pdf_dep = dependency('poppler-glib', required: false)
archive_dep = dependency('libarchive', required: false)

shell_deps = deps + [rsvg_dep, tomlplusplus_dep, eh_bundled_dep, nlohmann_json_dep, eh_material_colors_dep, dl_dep] + (pdf_dep.found() ? [pdf_dep] : []) + (archive_dep.found() ? [archive_dep] : [])
shell_cpp_args = []
shell_c_args = []

if cc.has_member('wl_pointer_listener', 'warp', prefix: '#include <wayland-client.h>')
  shell_cpp_args += ['-DEH_HAVE_POINTER_WARP=1']
endif

if pdf_dep.found()
  shell_cpp_args += ['-DEH_HAVE_POPPLER=1']
endif
if archive_dep.found()
  shell_cpp_args += ['-DEH_HAVE_LIBARCHIVE=1']
endif

if get_option('disable_mangowm_log')
  shell_cpp_args += ['-DEH_DISABLE_MANGOWM_LOG=1']
endif

polkit_agent_opt = get_option('polkit_agent')
if polkit_agent_opt
  polkit_dep = dependency('polkit-agent-1', required: true)
  shell_deps += [
    polkit_dep,
    dependency('polkit-gobject-1', required: true),
    dependency('gio-2.0', required: true),
    dependency('glib-2.0', required: true),
    dependency('gobject-2.0', required: true),
  ]
  shell_core_sources += files(
    'src/services/polkit/listener/eh_polkit_listener.c',
    'src/services/polkit/listener/eh_polkit_listener.h',
    'src/services/polkit/listener/polkit_bridge_capi.h',
    'src/services/polkit/service/polkit_auth_service.cpp',
    'src/services/polkit/dialog/polkit_auth_dialog.cpp',
  )
  shell_cpp_args += ['-DEH_HAVE_POLKIT_AGENT=1']
  shell_c_args += ['-DEH_HAVE_POLKIT_AGENT=1']
endif

if rsvg_dep.found()
  shell_cpp_args += ['-DEH_HAVE_RSVG=1']
endif
if libdrm_dep.found()
  shell_deps += [libdrm_dep]
  shell_cpp_args += ['-DEH_HAVE_LIBDRM=1']
endif
if jpeg_dep.found()
  shell_deps += [jpeg_dep]
  shell_cpp_args += ['-DEH_HAVE_LIBJPEG=1']
endif

# Little CMS 2 — ICC color-profile transforms for accurate wallpaper palette extraction.
lcms2_dep = dependency('lcms2', required: false)
if not lcms2_dep.found()
  lcms2_dep = dependency('lcms2', modules: ['lcms2::lcms2'], required: false)
endif
if lcms2_dep.found()
  shell_deps += [lcms2_dep]
  shell_cpp_args += ['-DEH_HAVE_LCMS2=1']
endif

jxl_dep = disabler()
if get_option('jxl')
  jxl_dep = dependency('libjxl', required: false)
  if jxl_dep.found()
    shell_deps += [jxl_dep]
    shell_cpp_args += ['-DEH_HAVE_LIBJXL=1']
  endif
endif
png_dep = dependency('libpng', required: false)
if not png_dep.found()
  png_proj = subproject('libpng')
  png_dep = png_proj.get_variable('png_dep')
endif
if png_dep.found()
  shell_deps += [png_dep]
  shell_cpp_args += ['-DEH_HAVE_LIBPNG=1']
endif

shell_cpp_args += ['-DEH_SHELL_VERSION="' + meson.project_version() + '"']
shell_cpp_args += ['-Wno-deprecated-literal-operator']

# Matugen bundle (configs + templates) for `apply_external_matugen_templates` — prefer over ~/.config/quickshell.
eh_matugen_bundle_install = get_option('prefix') / get_option('datadir') / 'event-horizon'
if fs.is_dir('matugen')
  install_subdir('matugen', install_dir: eh_matugen_bundle_install)
endif
shell_cpp_args += ['-DEH_SYSTEM_MATUGEN_BUNDLE="' + eh_matugen_bundle_install + '"']

# Bundled adw-gtk3 / adw-gtk3-dark GTK themes: unmodified vendored copy of upstream
# lassekongo83/adw-gtk3 (LGPL-2.1), attribution in third_party/adw-gtk3/. Prefer the
# distro package (Fedora: adw-gtk3-theme) and remove third_party/adw-gtk3 to skip install.
_adw_dir = 'third_party/adw-gtk3'
if fs.is_dir(_adw_dir / 'adw-gtk3')
  install_subdir(_adw_dir / 'adw-gtk3', install_dir: get_option('datadir') / 'themes' / 'adw-gtk3')
endif
if fs.is_dir(_adw_dir / 'adw-gtk3-dark')
  install_subdir(_adw_dir / 'adw-gtk3-dark', install_dir: get_option('datadir') / 'themes' / 'adw-gtk3-dark')
endif

# Shared library so that the supervisor + every split-out child process share one
# copy of the code + embedded assets in physical memory (see shell.md, Phase 0).
eh_shell_core_lib = shared_library(
  'eh_shell_core',
  sources: shell_core_sources,
  dependencies: shell_deps,
  include_directories: inc,
  cpp_args: shell_cpp_args,
  c_args: shell_c_args,
  cpp_pch: 'src/bootstrap/pch.hpp',
  install: true,
)

eh_shell_core_dep = declare_dependency(
  link_with: eh_shell_core_lib,
  dependencies: shell_deps,
  include_directories: inc,
)

executable(
  'EventHorizon',
  sources: 'src/bootstrap/entry/entry_point.cpp',
  dependencies: eh_shell_core_dep,
  cpp_args: shell_cpp_args + jemalloc_cpp_args,
  c_args: shell_c_args,
  cpp_pch: 'src/bootstrap/pch.hpp',
  install: true,
  install_rpath: '$ORIGIN/../' + get_option('libdir'),
  install_mode: 'rwxr-xr-x'
)

executable(
  'eh-ipc',
  sources: 'src/ipc_client/main.cpp',
  install: true,
  install_mode: 'rwxr-xr-x'
)

executable(
  'eh-startup-probe',
  sources: 'src/diagnostics/startup_probe.cpp',
  install: true,
  install_mode: 'rwxr-xr-x'
)

executable(
  'horizon-live-wallpaper',
  sources: [
    'src/ux/Live_Wallpaper/standalone_main.cpp',
    'src/ux/Live_Wallpaper/app.cpp',
    'src/ux/Live_Wallpaper/settings.cpp',
    'src/ux/Live_Wallpaper/embed/embed.cpp',
    'src/ux/Live_Wallpaper/input/events.cpp',
    'src/ux/Live_Wallpaper/gallery.cpp',  # ported gallery helpers - live entirely inside the standalone now
    protocol_headers,
    protocol_sources,
  ],
  dependencies: [eh_shell_core_dep, ffmpegthumbnailer_dep],
  cpp_args: shell_cpp_args + jemalloc_cpp_args,
  cpp_pch: 'src/bootstrap/pch.hpp',
  install: true,
  install_rpath: '$ORIGIN/../' + get_option('libdir'),
)

executable(
  'horizon-disks',
  sources: [
    'src/ux/disks/main.cpp',
    'src/ux/disks/app.cpp',
    'src/ux/disks/ui/draw.cpp',
    'src/ux/disks/input/events.cpp',
    'src/ux/disks/embed/embed.cpp',
    'src/ux/disks/dialogs/create_partition_dialog.cpp',
    'src/ux/disks/dialogs/format_disk_dialog.cpp',
    'src/ux/disks/dialogs/format_volume_dialog.cpp',
    protocol_headers,
    protocol_sources,
  ],
  dependencies: eh_shell_core_dep,
  cpp_args: shell_cpp_args + jemalloc_cpp_args,
  cpp_pch: 'src/bootstrap/pch.hpp',
  install: true,
  install_rpath: '$ORIGIN/../' + get_option('libdir'),
)

executable(
  'horizonClipboardDaemon',
  sources: [
    'src/clipboard_daemon/main.cpp',
    'src/wl/input/clipboard.cpp',
    protocol_headers,
    protocol_sources,
  ],
  dependencies: [
    dependency('wayland-client'),
  ],
  include_directories: inc,
  cpp_pch: 'src/bootstrap/pch.hpp',
  install: true,
  install_mode: 'rwxr-xr-x',
)

# Split-out settings child (Phase 2): run_standalone lives in eh_shell_core, this
# executable is the thin entrypoint. Spawned by the supervisor via posix_spawn.
executable(
  'horizon-settings',
  sources: 'src/ux/settings/common/settings_standalone_main.cpp',
  dependencies: eh_shell_core_dep,
  cpp_args: shell_cpp_args + jemalloc_cpp_args,
  cpp_pch: 'src/bootstrap/pch.hpp',
  install: true,
  install_rpath: '$ORIGIN/../' + get_option('libdir'),
  install_mode: 'rwxr-xr-x',
)

# Split-out wallpaper child (Phase 2): the native renderer + decode loop live in
# eh_shell_core; this thin entrypoint (jemalloc pin mirrors entry_point.cpp) runs
# them in their own process, spawned by the supervisor via posix_spawn.
executable(
  'horizon-wallpaper',
  sources: 'src/wallpaper/standalone_main.cpp',
  dependencies: eh_shell_core_dep,
  cpp_args: shell_cpp_args + jemalloc_cpp_args,
  cpp_pch: 'src/bootstrap/pch.hpp',
  install: true,
  install_rpath: '$ORIGIN/../' + get_option('libdir'),
  install_mode: 'rwxr-xr-x',
)

# Split-out notifications child (Phase 2): the NotificationManager + D-Bus
# service + toast host live in eh_shell_core; this thin entrypoint (jemalloc pin
# mirrors entry_point.cpp) runs them in their own process, spawned by the
# supervisor via posix_spawn.
executable(
  'horizon-notifications',
  sources: 'src/notifications/standalone_main.cpp',
  dependencies: eh_shell_core_dep,
  cpp_args: shell_cpp_args + jemalloc_cpp_args,
  cpp_pch: 'src/bootstrap/pch.hpp',
  install: true,
  install_rpath: '$ORIGIN/../' + get_option('libdir'),
  install_mode: 'rwxr-xr-x',
)

# Split-out desktop child (Phase 3 item 3): the desktop icon layer + widgets +
# menus run in their own process on their own WaylandConnection, timer fds, and
# MPRIS listener. Spawned by the supervisor via posix_spawn.
executable(
  'horizon-desktop',
  sources: 'src/desktop_shell/desktop/standalone_main.cpp',
  dependencies: eh_shell_core_dep,
  cpp_args: shell_cpp_args + jemalloc_cpp_args,
  cpp_pch: 'src/bootstrap/pch.hpp',
  install: true,
  install_rpath: '$ORIGIN/../' + get_option('libdir'),
  install_mode: 'rwxr-xr-x',
)

# Split-out dock child (Phase 3 item 4): the dock + popups + launchpad/overview
# run in their own process on their own WaylandConnection, timer fds, settings
# inotify, and MPRIS listener. Spawned by the supervisor via posix_spawn.
executable(
  'horizon-dock',
  sources: 'src/desktop_shell/dock/standalone_main.cpp',
  dependencies: eh_shell_core_dep,
  cpp_args: shell_cpp_args + jemalloc_cpp_args,
  cpp_pch: 'src/bootstrap/pch.hpp',
  install: true,
  install_rpath: '$ORIGIN/../' + get_option('libdir'),
  install_mode: 'rwxr-xr-x',
)

# Split-out taskbar child (Phase 3 item 4 / §0.11): the taskbar bar + app
# drawer + popups run in their own process on their own WaylandConnection (with
# their own toplevel tracking, mirroring horizon-dock), timer fds, settings
# inotify, and MPRIS listener. Spawned by the supervisor via posix_spawn.
executable(
  'horizon-taskbar',
  sources: 'src/desktop_shell/taskbar/standalone_main.cpp',
  dependencies: eh_shell_core_dep,
  cpp_args: shell_cpp_args + jemalloc_cpp_args,
  cpp_pch: 'src/bootstrap/pch.hpp',
  install: true,
  install_rpath: '$ORIGIN/../' + get_option('libdir'),
  install_mode: 'rwxr-xr-x',
)

_shell_gpu_vk_txt = vulkan_dep.found() ? 'linked' : 'not linked (install vulkan-loader-devel)'
summary(
  {
    'GPU in binary': 'Vulkan WSI',
    'Vulkan WSI': _shell_gpu_vk_txt,
    'Default [shell] renderer': 'vulkan',
  },
  section: 'Shell renderer',
)

# Runtime-loaded asset images (trash, launchpad, etc.) — always installed.
install_data(
  'src/assets/Dark_Launchpad.png',
  'src/assets/Dark_Launchpad.svg',
  'src/assets/Dark_Launchpad.webp',
  'src/assets/Light_Launchpad.png',
  'src/assets/Light_Launchpad.svg',
  'src/assets/Light_Launchpad.webp',
  'src/assets/MacOS-Trash-Empty.png',
  'src/assets/MacOS-Trash-Empty.webp',
  'src/assets/MacOS-Trash-Full.png',
  'src/assets/MacOS-Trash-Full.webp',
  install_dir: get_option('datadir') / 'event-horizon' / 'assets',
)

# File browser UI icons (arrow-left/right/up for back/forward/navigate-up, Mounted.svg for drives, search.svg for search button)
install_data(
  'assets/UI/arrow-left.svg',
  'assets/UI/arrow-right.svg',
  'assets/UI/arrow-up.svg',
  'assets/UI/Mounted.svg',
  'assets/UI/search.svg',
  'assets/UI/folder-search.svg',
  install_dir: get_option('datadir') / 'event-horizon' / 'assets' / 'UI',
)

# .desktop file + icon for the wallpaper pickers
install_data(
  'src/ux/Live_Wallpaper/horizon-live-wallpaper.desktop',
  install_dir: get_option('datadir') / 'applications',
)

install_data(
  'src/assets/Horizon-wallpaper.svg',
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps',
  rename: 'horizon-wallpaper.svg',
)

# .desktop file + icon for the disk utility
install_data(
  'src/ux/disks/horizon-disks.desktop',
  install_dir: get_option('datadir') / 'applications',
)

install_data(
  'src/assets/Horizon-Disk-Utility',
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps',
  rename: 'horizon-disks.svg',
)

# .desktop file + icon for the settings app
install_data(
  'src/ux/settings/event-horizon-settings.desktop',
  install_dir: get_option('datadir') / 'applications',
)

install_data(
  'src/assets/Horizon-settings.svg',
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps',
  rename: 'event-horizon-settings.svg',
)

# Bundled fonts used at runtime. Inter ships via the `assets/fonts/inter`
# git submodule (rsms/inter @ v4.0); Material Symbols Rounded is vendored
# in-tree. Fira Code and SF Pro are not used by the shell and are not shipped.
install_data(
  'assets/fonts/inter/docs/font-files/InterVariable.ttf',
  install_dir: get_option('datadir') / 'event-horizon' / 'assets' / 'fonts' / 'inter',
)
# Logo + Material Symbols are in the binary when `embed_assets=true`;
# default (false) installs them under share/ instead.
if not get_option('embed_assets')
  install_data(
    'src/Resources/Event-Horizon-logo.png',
    install_dir: get_option('datadir') / 'event-horizon' / 'assets',
  )
  install_data(
    'src/assets/fonts/MF/MaterialSymbolsRounded.ttf',
    install_dir: get_option('datadir') / 'event-horizon' / 'assets' / 'fonts' / 'MF',
  )
endif

# ── Horizon Colors engine test ──────────────────────────────────────────────
test_inc = include_directories('.', 'src', 'third_party', 'third_party/material-color-utilities')
test_deps = [eh_material_colors_dep]
test_cpp_args = ['-Wno-missing-field-initializers', '-Wno-error=missing-field-initializers']
if jpeg_dep.found()
  test_deps += [jpeg_dep]
  test_cpp_args += ['-DEH_HAVE_LIBJPEG=1']
endif
if lcms2_dep.found()
  test_deps += [lcms2_dep]
  test_cpp_args += ['-DEH_HAVE_LCMS2=1']
endif
test_horizon_colors = executable('test_horizon_colors',
  ['test/test_horizon_colors.cpp', 'test/stb_impl.cpp', 'src/color/horizon_colors.cpp'],
  include_directories: test_inc,
  dependencies: test_deps,
  cpp_args: test_cpp_args,
  install: false,
)
test('horizon_colors_engine', test_horizon_colors)

# ── IPC fabric contract test (Phase 1) ──────────────────────────────────────
test_contract_ipc = executable('test_contract_ipc',
  [
    'test/test_contract_ipc.cpp',
    'src/services/ipc/ipc_protocol.cpp',
    'src/services/ipc/ipc_server.cpp',
    'src/services/ipc/client.cpp',
    'src/services/windows/toplevel_types.cpp',
    'src/services/windows/toplevel_service.cpp',
    'src/services/windows/toplevel_client.cpp',
  ],
  include_directories: test_inc,
  dependencies: [],
  cpp_args: test_cpp_args,
  install: false,
)
test('ipc_fabric', test_contract_ipc)

# ── VRAM boost pure logic test ───────────────────────────────────────────────
test_vram_boost = executable('test_vram_boost',
  [
    'test/test_vram_boost.cpp',
    'src/services/vram_boost/vram_boost_detail.cpp',
  ],
  include_directories: test_inc,
  dependencies: [],
  cpp_args: test_cpp_args,
  install: false,
)
test('vram_boost', test_vram_boost)

# Live dmem cgroup round-trip (skips with exit 77 when unprivileged / no dmem).
test_vram_boost_live = executable('test_vram_boost_live',
  [
    'test/test_vram_boost_live.cpp',
    'src/services/vram_boost/vram_boost_detail.cpp',
  ],
  include_directories: test_inc,
  dependencies: [],
  cpp_args: test_cpp_args,
  install: false,
)
test('vram_boost_live', test_vram_boost_live, should_fail: false)

# ── Icon cache engine test (ported from Horizon-File-Manager) ───────────────
test_icon_cache = executable('test_icon_cache',
  ['test/test_icon_cache.cpp'],
  include_directories: test_inc,
  dependencies: [eh_shell_core_dep],
  cpp_args: test_cpp_args,
  install: false,
)
test('icon_cache', test_icon_cache)

# ── Damage region utility test ───────────────────────────────────────────────
test_damage_region = executable('test_damage_region',
  ['test/test_damage_region.cpp', 'src/wl/buffer/damage_region.cpp'],
  include_directories: test_inc,
  dependencies: [],
  cpp_args: test_cpp_args,
  install: false,
)
test('damage_region', test_damage_region)

# ── Test & Benchmark Targets ─────────────────────────────────────────────────
subdir('tests')

