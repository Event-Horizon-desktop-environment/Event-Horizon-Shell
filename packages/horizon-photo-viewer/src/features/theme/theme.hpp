#pragma once

namespace hpv {
namespace m3 {

extern double surface_r, surface_g, surface_b;
extern double surface_container_r, surface_container_g, surface_container_b;
extern double surface_container_high_r, surface_container_high_g, surface_container_high_b;
extern double on_surface_r, on_surface_g, on_surface_b;
extern double on_surface_variant_r, on_surface_variant_g, on_surface_variant_b;
extern double primary_r, primary_g, primary_b;
extern double primary_container_r, primary_container_g, primary_container_b;
extern double on_primary_container_r, on_primary_container_g, on_primary_container_b;
extern double outline_r, outline_g, outline_b;
extern double outline_variant_r, outline_variant_g, outline_variant_b;
extern double tonal_container_r, tonal_container_g, tonal_container_b;
void apply_theme(bool light);

} // namespace m3
} // namespace hpv
