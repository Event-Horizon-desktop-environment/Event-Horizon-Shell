#include "features/theme/theme.hpp"

namespace hpv {
namespace m3 {

double surface_r = 0.102;
double surface_g = 0.110;
double surface_b = 0.118;
double surface_container_r = 0.145;
double surface_container_g = 0.153;
double surface_container_b = 0.161;
double surface_container_high_r = 0.188;
double surface_container_high_g = 0.200;
double surface_container_high_b = 0.216;

double on_surface_r = 0.890;
double on_surface_g = 0.886;
double on_surface_b = 0.902;
double on_surface_variant_r = 0.769;
double on_surface_variant_g = 0.776;
double on_surface_variant_b = 0.816;

double primary_r = 0.565;
double primary_g = 0.792;
double primary_b = 0.976;
double primary_container_r = 0.102;
double primary_container_g = 0.333;
double primary_container_b = 0.549;
double on_primary_container_r = 0.820;
double on_primary_container_g = 0.894;
double on_primary_container_b = 1.000;

double outline_r = 0.557;
double outline_g = 0.565;
double outline_b = 0.600;
double outline_variant_r = 0.267;
double outline_variant_g = 0.278;
double outline_variant_b = 0.310;

double tonal_container_r = 0.173;
double tonal_container_g = 0.196;
double tonal_container_b = 0.224;

void apply_theme(bool light) {
    if (!light) {
        surface_r = 0.102; surface_g = 0.110; surface_b = 0.118;
        surface_container_r = 0.145; surface_container_g = 0.153; surface_container_b = 0.161;
        surface_container_high_r = 0.188; surface_container_high_g = 0.200; surface_container_high_b = 0.216;
        on_surface_r = 0.890; on_surface_g = 0.886; on_surface_b = 0.902;
        on_surface_variant_r = 0.769; on_surface_variant_g = 0.776; on_surface_variant_b = 0.816;
        primary_r = 0.565; primary_g = 0.792; primary_b = 0.976;
        primary_container_r = 0.102; primary_container_g = 0.333; primary_container_b = 0.549;
        on_primary_container_r = 0.820; on_primary_container_g = 0.894; on_primary_container_b = 1.000;
        outline_r = 0.557; outline_g = 0.565; outline_b = 0.600;
        outline_variant_r = 0.267; outline_variant_g = 0.278; outline_variant_b = 0.310;
        tonal_container_r = 0.173; tonal_container_g = 0.196; tonal_container_b = 0.224;
    } else {
        surface_r = 0.953; surface_g = 0.953; surface_b = 0.957;
        surface_container_r = 0.933; surface_container_g = 0.933; surface_container_b = 0.941;
        surface_container_high_r = 0.918; surface_container_high_g = 0.918; surface_container_high_b = 0.925;
        on_surface_r = 0.106; on_surface_g = 0.106; on_surface_b = 0.114;
        on_surface_variant_r = 0.290; on_surface_variant_g = 0.294; on_surface_variant_b = 0.325;
        primary_r = 0.282; primary_g = 0.553; primary_b = 0.800;
        primary_container_r = 0.835; primary_container_g = 0.922; primary_container_b = 1.000;
        on_primary_container_r = 0.004; on_primary_container_g = 0.122; on_primary_container_b = 0.251;
        outline_r = 0.459; outline_g = 0.459; outline_b = 0.506;
        outline_variant_r = 0.729; outline_variant_g = 0.729; outline_variant_b = 0.761;
        tonal_container_r = 0.886; tonal_container_g = 0.918; tonal_container_b = 0.957;
    }
}

} // namespace m3
} // namespace hpv
