#pragma once

#include "wl/core/protocols.hpp"

#include <cstdint>
#include <list>

struct wl_output;

namespace eh::wayland {

class GammaService {
public:
  explicit GammaService(zwlr_gamma_control_manager_v1* manager = nullptr);
  ~GammaService();

  GammaService(const GammaService&) = delete;
  GammaService& operator=(const GammaService&) = delete;
  GammaService(GammaService&&) = delete;
  GammaService& operator=(GammaService&&) = delete;

  void set_temperature(int kelvin, bool animate = true);
  void set_enabled(bool enabled);

  [[nodiscard]] bool enabled() const { return enabled_; }
  [[nodiscard]] int temperature() const { return target_temperature_; }
  [[nodiscard]] bool has_gamma_control() const { return manager_ != nullptr; }
  [[nodiscard]] bool transitioning() const { return transition_fd_ >= 0; }
  [[nodiscard]] int transition_fd() const { return transition_fd_; }

  void add_output(wl_output* output);
  void remove_output(wl_output* output);

  // Poll handler, invoked when transition_fd() becomes readable.
  void on_transition_timer();

  static void on_gamma_size(void* data, zwlr_gamma_control_v1* ctrl, uint32_t size);
  static void on_gamma_failed(void* data, zwlr_gamma_control_v1* ctrl);

private:
  struct OutputGamma {
    wl_output* output = nullptr;
    zwlr_gamma_control_v1* control = nullptr;
    uint32_t gamma_size = 0;
    bool ready = false;
    GammaService* owner = nullptr;
  };

  void create_control(wl_output* output);
  void destroy_control(OutputGamma& og);
  void destroy_all_controls();
  void apply_current();
  void apply_to_output(const OutputGamma& og);
  void start_transition(int from_k, int to_k);
  void stop_transition();

  struct RgbMultipliers {
    double r = 1.0;
    double g = 1.0;
    double b = 1.0;
  };

  static RgbMultipliers kelvin_to_rgb(int kelvin);
  static void fill_ramp(uint16_t* ramp, uint32_t size, const RgbMultipliers& mul);

  zwlr_gamma_control_manager_v1* manager_ = nullptr;
  std::list<OutputGamma> outputs_;
  int target_temperature_ = -1;
  int display_temperature_ = -1;
  bool enabled_ = false;

  // The ongoing color transition, if any.
  int transition_fd_ = -1;
  int transition_from_ = -1;
  int transition_to_ = -1;
  uint64_t transition_start_ms_ = 0;
  static constexpr uint64_t kTransitionDurationMs = 1500;
};

}
