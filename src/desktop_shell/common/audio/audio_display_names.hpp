#pragma once

// Compact audio labels shared by the volume-mixer popup and Settings → Sound.
// Shortens common ALSA/Bluetooth verbosity before UI ellipsizers run, so
// entries fit narrow dropdowns instead of showing as
// "Family 17h/19h HD Audio Cont...".

#include <string>

namespace eh::shell::audio {

inline std::string shorten_device_name(const std::string& name) {
  std::string s = name;
  auto replace_all = [&](const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
      s.replace(pos, from.size(), to);
      pos += to.size();
    }
  };
  replace_all("High Definition Audio Controller", "HD Audio");
  replace_all("High Definition Audio", "HD Audio");
  replace_all(" Audio Controller", "");
  // Trailing monitor tags: "… (HDMI) [MAG 272Q X24]" -> "… (HDMI)".
  if (s.size() > 2 && s.back() == ']') {
    const auto open = s.find_last_of('[');
    if (open != std::string::npos && open > 0) {
      s.resize(open);
      while (!s.empty() && s.back() == ' ') s.pop_back();
    }
  }
  // "GB203 HD Audio Digital Stereo (HDMI)" -> "GB203 HD Audio HDMI".
  {
    const auto dig = s.find("Digital Stereo (");
    if (dig != std::string::npos) {
      const auto close = s.find(')', dig);
      if (close != std::string::npos) {
        const std::string port = s.substr(dig + 16, close - dig - 16);
        s = s.substr(0, dig) + port + s.substr(close + 1);
      }
    }
  }
  // AMD "Family 17h/19h ..." PCI prefixes carry no switching info.
  if (s.compare(0, 7, "Family ") == 0) {
    const auto sp = s.find(' ');
    const auto sp2 = s.find(' ', sp + 1);
    if (sp2 != std::string::npos) s = s.substr(sp2 + 1);
  }
  replace_all("  ", " ");
  while (!s.empty() && s.front() == ' ') s.erase(s.begin());
  while (!s.empty() && s.back() == ' ') s.pop_back();
  return s.empty() ? name : s;
}

// "High Fidelity Playback (A2DP Sink, codec aptX)" -> "A2DP · aptX",
// "Digital Stereo (HDMI 2) Output" -> "HDMI 2 Out", "Off" stays "Off".
inline std::string shorten_profile_label(const std::string& key, const std::string& desc) {
  if (key == "off" || desc == "Off") return "Off";
  std::string d = desc;
  // Extract "codec X".
  std::string codec;
  const auto ck = d.find("codec ");
  if (ck != std::string::npos) {
    size_t beg = ck + 6;
    size_t end = d.find_first_of(",)", beg);
    codec = d.substr(beg, end == std::string::npos ? end : end - beg);
    while (!codec.empty() && codec.front() == ' ') codec.erase(codec.begin());
    while (!codec.empty() && codec.back() == ' ') codec.pop_back();
  }
  // Base = text before any parenthetical.
  std::string base = d;
  if (const auto par = base.find('('); par != std::string::npos) base = base.substr(0, par);
  while (!base.empty() && base.back() == ' ') base.pop_back();
  auto replace_all = [&](std::string& str, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
      str.replace(pos, from.size(), to);
      pos += to.size();
    }
  };
  replace_all(base, "High Fidelity Playback", "A2DP");
  replace_all(base, "Headset Head Unit", "Headset");
  replace_all(base, "Analog Stereo Duplex", "Analog Duplex");
  replace_all(base, "Analog Stereo Output", "Analog Out");
  replace_all(base, "Analog Stereo Input", "Analog In");
  replace_all(base, "Analog Surround", "Surround");
  // "Digital Stereo (HDMI 2) Output" -> "HDMI 2 Out",
  // "Digital Surround 5.1 (HDMI) Output" -> "HDMI Surround 5.1 Out".
  if (base.compare(0, 7, "Digital") == 0) {
    std::string port = "Digital";
    if (const auto hdmi = d.find("HDMI"); hdmi != std::string::npos) {
      const size_t end = d.find_first_of(" )", hdmi);
      port = d.substr(hdmi, end == std::string::npos ? end : end - hdmi);
    }
    std::string fmt = base.size() > 8 ? base.substr(8) : std::string{};
    if (fmt == "Stereo") fmt.clear();
    base = port + (fmt.empty() ? "" : " " + fmt) + (d.find("Input") != std::string::npos ? " In" : " Out");
  }
  if (!codec.empty() && base.find(codec) == std::string::npos) base += " · " + codec;
  while (!base.empty() && base.front() == ' ') base.erase(base.begin());
  return base.empty() ? desc : base;
}

}  // namespace eh::shell::audio
