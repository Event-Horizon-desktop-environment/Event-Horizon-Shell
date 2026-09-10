# ── Event Horizon Shell — Test & Benchmark Targets ────────────────────────────
#
# Integration tests: launch the shell, measure RSS / CPU / GPU / FDs / IPC.
# Stress tests:      hammer IPC, config reloads, window lifecycles.
# Unit benchmarks:   Cairo, config parse, damage regions, IPC, event loop.
#
# Run:  meson test -C build               (integration + stress)
#       ./build/tests/bench_cairo_render   (unit benchmarks, standalone)

test_inc = include_directories('.', 'helpers')

# ── Integration tests ────────────────────────────────────────────────────────

launch_harness_exe = executable('launch_harness',
  sources: ['integration/launch_harness.cpp'],
  include_directories: test_inc,
  install: false,
)
test('launch_harness', launch_harness_exe, args: [meson.current_build_dir() / '../EventHorizon'], timeout: 300, is_parallel: false)

executable('component_profiler',
  sources: ['integration/component_profiler.cpp'],
  include_directories: test_inc,
  install: false,
)

executable('memory_monitor',
  sources: ['integration/memory_monitor.cpp'],
  include_directories: test_inc,
  install: false,
)

# ── Stress tests ─────────────────────────────────────────────────────────────

executable('stress_ipc',
  sources: ['stress/stress_ipc.cpp'],
  include_directories: test_inc,
  install: false,
)

executable('stress_window_lifecycle',
  sources: ['stress/stress_window_lifecycle.cpp'],
  include_directories: test_inc,
  install: false,
)

# ── Unit benchmarks (standalone, no Google Benchmark dependency) ──────────────

executable('bench_cairo_render',
  sources: ['unit/bench_cairo_render.cpp'],
  include_directories: test_inc,
  dependencies: [dependency('cairo'), dependency('pango'), dependency('pangocairo')],
  install: false,
)

executable('bench_config_parse',
  sources: ['unit/bench_config_parse.cpp'],
  include_directories: test_inc,
  install: false,
)

executable('bench_damage_region',
  sources: ['unit/bench_damage_region.cpp'],
  include_directories: test_inc,
  install: false,
)

executable('bench_ipc_throughput',
  sources: ['unit/bench_ipc_throughput.cpp'],
  include_directories: test_inc,
  install: false,
)

executable('bench_event_loop',
  sources: ['unit/bench_event_loop.cpp'],
  include_directories: test_inc,
  install: false,
)

# ── Frame pacing / presentation latency benchmark ─────────────────────────────
# Standalone tool (not a meson test — needs a live compositor):
#   ./build-debug/tests/frame_bench --frames 600 --json /tmp/fb.json

frame_bench_exe = executable('frame_bench',
  sources: ['integration/frame_bench.cpp'] + protocol_headers + protocol_sources,
  include_directories: [test_inc, inc],
  dependencies: [dependency('wayland-client')],
  install: false,
)
