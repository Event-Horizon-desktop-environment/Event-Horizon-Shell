#pragma once

namespace eh::config {
struct ShellConfig;
}

namespace eh::matugen {

void apply_external_matugen_templates(const eh::config::ShellConfig& config);

[[nodiscard]] bool matugen_external_bundle_available();

}
