#pragma once

#include <memory>
#include <string>

namespace eh::shell::stage { struct StageApp; }

namespace eh::shell::osd {

struct OsdContent {

  std::string icon_ligature;
  std::string value_text;
  float progress = 0.f;
};

class OsdHost {
public:
  OsdHost();
  ~OsdHost();
  OsdHost(OsdHost&&) noexcept = default;
  OsdHost& operator=(OsdHost&&) noexcept = default;
  OsdHost(const OsdHost&) = delete;
  OsdHost& operator=(const OsdHost&) = delete;

  void init(eh::shell::stage::StageApp& stage);
  void shutdown();

  void show(const OsdContent& content);

  struct Impl;

private:
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] bool osd_env_disabled() noexcept;

}
