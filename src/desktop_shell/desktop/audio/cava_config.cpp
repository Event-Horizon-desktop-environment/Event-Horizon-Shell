#include "desktop_shell/desktop/audio/cava_config.hpp"

#include <cstdlib>
#include <filesystem>
#include <sstream>

#define TOML_IMPLEMENTATION
#include <toml++/toml.hpp>

namespace eh::shell::desktop {

std::string CavaConfig::config_path() {
    const char* state = std::getenv("XDG_STATE_HOME");
    if (!state || !state[0]) {
        const char* home = std::getenv("HOME");
        if (home) return std::string(home) + "/.local/state/event-horizon/cava/cava.toml";
        return "/tmp/event-horizon/cava/cava.toml";
    }
    return std::string(state) + "/event-horizon/cava/cava.toml";
}

CavaConfig CavaConfig::parse_file(const std::string& path) {
    CavaConfig cfg;
    try {
        auto tbl = toml::parse_file(path);

        if (auto* g = tbl.get("general")) {
            if (auto* t = g->as_table()) {
                if (auto v = t->get("bars"))
                    cfg.bars = v->value_or(cfg.bars);
                if (auto v = t->get("framerate"))
                    cfg.framerate = v->value_or(cfg.framerate);
                if (auto v = t->get("autosens"))
                    cfg.autosens = v->value_or(cfg.autosens);
                if (auto v = t->get("sensitivity"))
                    cfg.sensitivity = v->value_or(cfg.sensitivity);
                if (auto v = t->get("lower_cutoff_freq"))
                    cfg.lower_cutoff_freq = v->value_or(cfg.lower_cutoff_freq);
                if (auto v = t->get("higher_cutoff_freq"))
                    cfg.higher_cutoff_freq = v->value_or(cfg.higher_cutoff_freq);
            }
        }

        if (auto* s = tbl.get("smoothing")) {
            if (auto* t = s->as_table()) {
                if (auto v = t->get("noise_reduction"))
                    cfg.noise_reduction = v->value_or(cfg.noise_reduction);
                if (auto v = t->get("integral"))
                    cfg.integral = v->value_or(cfg.integral);
                if (auto v = t->get("gravity"))
                    cfg.gravity = v->value_or(cfg.gravity);
                if (auto v = t->get("ignore"))
                    cfg.ignore = v->value_or(cfg.ignore);
                if (auto v = t->get("monstercat"))
                    cfg.monstercat = v->value_or(cfg.monstercat);
            }
        }

        if (auto* v = tbl.get("visualizer")) {
            if (auto* t = v->as_table()) {
                if (auto val = t->get("wave_height"))
                    cfg.wave_height = val->value_or(cfg.wave_height);
                if (auto val = t->get("attack_tau_ms"))
                    cfg.attack_tau_ms = val->value_or(cfg.attack_tau_ms);
                if (auto val = t->get("decay_tau_ms"))
                    cfg.decay_tau_ms = val->value_or(cfg.decay_tau_ms);
                if (auto val = t->get("fill_opacity"))
                    cfg.fill_opacity = val->value_or(cfg.fill_opacity);
                if (auto val = t->get("glow_intensity"))
                    cfg.glow_intensity = val->value_or(cfg.glow_intensity);
                if (auto val = t->get("peak_opacity"))
                    cfg.peak_opacity = val->value_or(cfg.peak_opacity);
                if (auto val = t->get("idle_breathe_amplitude"))
                    cfg.idle_breathe_amplitude = val->value_or(cfg.idle_breathe_amplitude);
                if (auto val = t->get("blob_tau_ms"))
                    cfg.blob_tau_ms = val->value_or(cfg.blob_tau_ms);
                if (auto val = t->get("blob_opacity"))
                    cfg.blob_opacity = val->value_or(cfg.blob_opacity);
            }
        }
    } catch (...) {
    }
    return cfg;
}

CavaConfig CavaConfig::load_or_default() {
    auto path = config_path();
    std::error_code ec;
    auto mtime = std::filesystem::last_write_time(path, ec);
    CavaConfig cfg;
    if (!ec) {
        cfg = parse_file(path);
        cfg.m_lastWriteTime = mtime;
    }
    return cfg;
}

ConfigChange CavaConfig::check_and_reload() {
    auto path = config_path();
    std::error_code ec;
    auto mtime = std::filesystem::last_write_time(path, ec);
    if (ec || mtime == m_lastWriteTime) return ConfigChange::None;

    CavaConfig fresh = parse_file(path);
    fresh.m_lastWriteTime = mtime;

    const bool same = cava_fields_match(*this, fresh);
    *this = fresh;

    return same ? ConfigChange::VisualizerOnly : ConfigChange::CavaChanged;
}

bool CavaConfig::cava_fields_match(const CavaConfig& a, const CavaConfig& b) {
    return a.bars == b.bars
        && a.framerate == b.framerate
        && a.autosens == b.autosens
        && a.sensitivity == b.sensitivity
        && a.lower_cutoff_freq == b.lower_cutoff_freq
        && a.higher_cutoff_freq == b.higher_cutoff_freq
        && a.noise_reduction == b.noise_reduction
        && a.integral == b.integral
        && a.gravity == b.gravity
        && a.ignore == b.ignore
        && a.monstercat == b.monstercat;
}

std::string CavaConfig::build_cava_config() const {
    std::ostringstream oss;
    oss << "[general]\n"
        << "framerate=" << framerate << "\n"
        << "bars=" << bars << "\n"
        << "autosens=" << (autosens ? "1" : "0") << "\n"
        << "sensitivity=" << sensitivity << "\n"
        << "lower_cutoff_freq=" << lower_cutoff_freq << "\n"
        << "higher_cutoff_freq=" << higher_cutoff_freq << "\n\n"
        << "[output]\n"
        << "method=raw\n"
        << "raw_target=/dev/stdout\n"
        << "data_format=ascii\n"
        << "channels=mono\n"
        << "mono_option=average\n\n"
        << "[smoothing]\n"
        << "noise_reduction=" << noise_reduction << "\n"
        << "integral=" << integral << "\n"
        << "gravity=" << gravity << "\n"
        << "ignore=" << ignore << "\n"
        << "monstercat=" << monstercat << "\n";
    return oss.str();
}

} // namespace eh::shell::desktop
