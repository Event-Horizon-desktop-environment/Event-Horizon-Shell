#pragma once

#include <filesystem>
#include <string>

namespace eh::shell::desktop {

enum class ConfigChange {
    None,
    VisualizerOnly,
    CavaChanged,
};

struct CavaConfig {
    // Cava [general]
    int bars = 24;
    int framerate = 60;
    bool autosens = false;
    int sensitivity = 50;
    int lower_cutoff_freq = 50;
    int higher_cutoff_freq = 12000;

    // Cava [smoothing]
    int noise_reduction = 0;
    int integral = 50;
    int gravity = 100;
    int ignore = 0;
    double monstercat = 2.0;

    // Visualizer — MediaCompactWidget
    double wave_height = 0.58;
    double attack_tau_ms = 20.0;
    double decay_tau_ms = 60.0;
    double fill_opacity = 0.20;
    double glow_intensity = 0.06;
    double peak_opacity = 0.65;
    double idle_breathe_amplitude = 0.035;

    // Visualizer — MediaPlayerWidget blob
    double blob_tau_ms = 60.0;
    double blob_opacity = 0.35;

    std::string build_cava_config() const;

    // Check the file on disk and reload if its mtime changed.
    // Returns what kind of change was detected (or None).
    ConfigChange check_and_reload();

    static CavaConfig load_or_default();
    static std::string config_path();

private:
    static bool cava_fields_match(const CavaConfig& a, const CavaConfig& b);
    static CavaConfig parse_file(const std::string& path);

    std::filesystem::file_time_type m_lastWriteTime{};
};

} // namespace eh::shell::desktop
