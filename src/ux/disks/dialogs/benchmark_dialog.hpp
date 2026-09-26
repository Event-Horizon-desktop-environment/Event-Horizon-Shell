#include <cairo/cairo.h>
#pragma once
#include <string>
#include <vector>
namespace eh::disks {
struct AppState;
inline constexpr int kBenchDlgW = 620;
inline constexpr int kBenchDlgH = 500;
struct BenchSample { double xfer = 0; };  // MB/s per sample
struct BenchmarkDialog {
  bool open = false;
  int drive_idx = -1;
  bool running = false;
  bool hover_close = false, hover_start = false, hover_stop = false;
  int samples = 100;
  std::vector<double> reads;  // MB/s history for graph (Disks 51 new graph)
  double avg_read = 0, avg_write = 0, access_ms = 0;
  bool has_result = false;
};
void draw_benchmark_dialog(struct AppState& app, cairo_t* cr);
bool benchmark_dialog_click(struct AppState& app, int x, int y);
void benchmark_dialog_move(struct AppState& app, int x, int y);
}  // namespace eh::disks
