#include "wl/color/nightlight.hpp"
#include "wl/core/memfd.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <chrono>

namespace {

constexpr zwlr_gamma_control_v1_listener kGammaControlListener = {
    .gamma_size = &eh::wayland::GammaService::on_gamma_size,
    .failed = &eh::wayland::GammaService::on_gamma_failed,
};

uint64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

}

namespace eh::wayland {

GammaService::GammaService(zwlr_gamma_control_manager_v1* manager) : manager_(manager) {}

GammaService::~GammaService() {
   
  stop_transition();
  destroy_all_controls();
  outputs_.clear();
}

void GammaService::destroy_all_controls() {
   
  for (auto& og : outputs_) {
    destroy_control(og);
  }
}

void GammaService::set_enabled(bool enabled) {
   
  enabled_ = enabled;
  if (!manager_) return;
  if (enabled_) {
    const int target = (target_temperature_ >= 0) ? target_temperature_ : 4000;
    target_temperature_ = target;
    start_transition(6500, target);
  } else {
    destroy_all_controls();
    stop_transition();
    display_temperature_ = -1;
  }
}

void GammaService::set_temperature(int kelvin, bool animate) {
   
  const int clamped = std::clamp(kelvin, 1000, 10000);
  if (clamped == target_temperature_ && display_temperature_ >= 0) return;
  target_temperature_ = clamped;
  if (!enabled_ || !manager_) return;

  const int from = (display_temperature_ > 0) ? display_temperature_ : 6500;
  if (animate && from != target_temperature_) {
    start_transition(from, target_temperature_);
  } else {
    stop_transition();
    for (auto& og : outputs_) {
      if (!og.control) create_control(og.output);
    }
    display_temperature_ = target_temperature_;
    apply_current();
  }
}

void GammaService::start_transition(int from_k, int to_k) {
   
  stop_transition();
  transition_from_ = from_k;
  transition_to_ = to_k;
  transition_start_ms_ = now_ms();
  display_temperature_ = from_k;

  // Make sure gamma controls exist for every tracked output.
  for (auto& og : outputs_) {
    if (!og.control) create_control(og.output);
  }

  transition_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
  if (transition_fd_ < 0) {
    display_temperature_ = to_k;
    apply_current();
    return;
  }

  itimerspec spec{};
  spec.it_interval.tv_nsec = 100 * 1000 * 1000; // 100ms
  spec.it_value.tv_nsec = 100 * 1000 * 1000;
  timerfd_settime(transition_fd_, 0, &spec, nullptr);
}

void GammaService::stop_transition() {
   
  if (transition_fd_ >= 0) {
    ::close(transition_fd_);
    transition_fd_ = -1;
  }
}

void GammaService::on_transition_timer() {
   
  if (transition_fd_ < 0) return;

  uint64_t exp = 0;
  (void)::read(transition_fd_, &exp, sizeof(exp));

  const auto elapsed = now_ms() - transition_start_ms_;
  if (elapsed >= kTransitionDurationMs) {
    display_temperature_ = transition_to_;
    stop_transition();
    apply_current();
    return;
  }

  const double t = static_cast<double>(elapsed) / static_cast<double>(kTransitionDurationMs);
  const int kelvin = static_cast<int>(std::round(
      transition_from_ + (transition_to_ - transition_from_) * t));
  display_temperature_ = kelvin;
  apply_current();
}

void GammaService::add_output(wl_output* output) {
   
  for (auto& og : outputs_) {
    if (og.output == output) return;
  }
  outputs_.emplace_back(OutputGamma{.output = output});
  if (enabled_ && target_temperature_ >= 0) {
    create_control(output);
    apply_current();
  }
}

void GammaService::remove_output(wl_output* output) {
   
  for (auto it = outputs_.begin(); it != outputs_.end(); ++it) {
    if (it->output == output) {
      destroy_control(*it);
      outputs_.erase(it);
      return;
    }
  }
}

void GammaService::create_control(wl_output* output) {
   
  if (!manager_) return;
  auto* ctrl = zwlr_gamma_control_manager_v1_get_gamma_control(manager_, output);
  if (!ctrl) return;

  auto& og = outputs_.emplace_back(OutputGamma{
      .output = output,
      .control = ctrl,
      .gamma_size = 0,
      .ready = false,
      .owner = this,
  });
  zwlr_gamma_control_v1_add_listener(ctrl, &kGammaControlListener, &og);
}

void GammaService::destroy_control(OutputGamma& og) {
   
  if (og.control) {
    zwlr_gamma_control_v1_destroy(og.control);
    og.control = nullptr;
  }
  og.ready = false;
  og.gamma_size = 0;
}

void GammaService::apply_to_output(const OutputGamma& og) {
   
  if (!enabled_ || display_temperature_ < 0) return;
  if (!og.control || og.gamma_size == 0 || !og.ready) return;

  const size_t table_bytes = 3 * og.gamma_size * sizeof(uint16_t);
  int fd = memfd_create_compat("gamma", MFD_CLOEXEC);
  if (fd < 0) return;

  if (ftruncate(fd, static_cast<off_t>(table_bytes)) < 0) {
    ::close(fd);
    return;
  }

  auto* data = static_cast<uint16_t*>(mmap(nullptr, table_bytes, PROT_WRITE, MAP_SHARED, fd, 0));
  if (data == MAP_FAILED) {
    ::close(fd);
    return;
  }

  const auto mul = kelvin_to_rgb(display_temperature_);
  fill_ramp(data, og.gamma_size, mul);
  munmap(data, table_bytes);

  zwlr_gamma_control_v1_set_gamma(og.control, fd);
  ::close(fd);
}

void GammaService::apply_current() {
   
  for (auto& og : outputs_) {
    apply_to_output(og);
  }
}

void GammaService::on_gamma_size(void* data, zwlr_gamma_control_v1*, uint32_t size) {
   
  auto& og = *static_cast<OutputGamma*>(data);
  og.gamma_size = size;
  og.ready = true;
  if (og.owner && og.owner->enabled_ && og.owner->display_temperature_ >= 0) {
    og.owner->apply_to_output(og);
  }
}

void GammaService::on_gamma_failed(void* data, zwlr_gamma_control_v1*) {
   
  auto& og = *static_cast<OutputGamma*>(data);
  og.ready = false;
}

GammaService::RgbMultipliers GammaService::kelvin_to_rgb(int kelvin) {
   
  const double temp = std::clamp(kelvin, 1000, 10000) / 100.0;
  RgbMultipliers mul;

  if (temp <= 66.0) {
    mul.r = 1.0;
    mul.g = std::clamp((99.4708025861 * std::log(temp) - 161.1195681661) / 255.0, 0.0, 1.0);
    if (temp <= 19.0) {
      mul.b = 0.0;
    } else {
      mul.b = std::clamp((138.5177312231 * std::log(temp - 10.0) - 305.0447927307) / 255.0, 0.0, 1.0);
    }
  } else {
    mul.r = std::clamp(329.698727446 * std::pow(temp - 60.0, -0.1332047592) / 255.0, 0.0, 1.0);
    mul.g = std::clamp(288.1221695283 * std::pow(temp - 60.0, -0.0755148492) / 255.0, 0.0, 1.0);
    mul.b = 1.0;
  }

  return mul;
}

void GammaService::fill_ramp(uint16_t* ramp, uint32_t size, const RgbMultipliers& mul) {
   
  const double scale = 65535.0 / static_cast<double>(size - 1);
  for (uint32_t i = 0; i < size; ++i) {
    const double base = i * scale;
    ramp[i] = static_cast<uint16_t>(std::clamp(mul.r * base, 0.0, 65535.0));
    ramp[size + i] = static_cast<uint16_t>(std::clamp(mul.g * base, 0.0, 65535.0));
    ramp[2 * size + i] = static_cast<uint16_t>(std::clamp(mul.b * base, 0.0, 65535.0));
  }
}

}
