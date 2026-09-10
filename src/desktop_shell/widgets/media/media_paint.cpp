#include "desktop_shell/widgets/media/media_paint.hpp"
#include "desktop_shell/widgets/shared/slot_pill_style.hpp"

#include "desktop_shell/widgets/shared/widget_settings.hpp"
#include "desktop_shell/widgets/shared/measure_scratch.hpp"
#include "configuration/shell_config.hpp"
#include "services/mpris/mpris_player.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>

#include <cairo.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>

namespace eh::widgets {
namespace {

constexpr double kSpacingS  = 6.0;
constexpr double kSpacingXS = 4.0;

constexpr double kBtnSmallD = 16.0;
constexpr double kBtnPlayD  = 19.0;

constexpr double kMediaOuterHPad = 4.0;

constexpr double kScrollThreshold4Words = 72.0;

constexpr double kPrimR = 0.90, kPrimG = 0.90, kPrimB = 0.90;
constexpr double kOnPrimR = 0.10, kOnPrimG = 0.10, kOnPrimB = 0.10;
constexpr double kSurfR = 1.0, kSurfG = 1.0, kSurfB = 1.0;

// Per-instance cache for title/artist natural widths.
// Invalidated automatically when track metadata changes.
struct MediaTextWidths {
    std::string title;
    std::string artist;
    int titleNatW = 0;
    int artistNatW = 0;
};
static std::unordered_map<std::string, MediaTextWidths> g_mediaTextWidths;

bool parse_bool(const std::string& v, bool def) {
   
    if (v.empty()) return def;
    if (v=="1"||v=="true"||v=="True"||v=="yes"||v=="Yes") return true;
    if (v=="0"||v=="false"||v=="False"||v=="no"||v=="No") return false;
    return def;
}

double parse_max_title_scale(const eh::config::ShellConfig& sc, std::string_view id) {
   
    const std::string raw = widget_setting(sc, id, "max_title_scale");
    if (raw.empty()) return 352.0;
    char* end = nullptr;
    const double v = std::strtod(raw.c_str(), &end);
    if (end == raw.c_str()) return 352.0;
    return std::clamp(v, 80.0, 660.0);
}

PangoLayout* make_layout(cairo_t* cr, const char* desc) {
   
    PangoLayout* l = pango_cairo_create_layout(cr);
    PangoFontDescription* d = pango_font_description_from_string(desc);
    pango_layout_set_font_description(l, d);
    pango_font_description_free(d);
    pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
    pango_layout_set_alignment(l, PANGO_ALIGN_LEFT);
    return l;
}

std::pair<int, int> font_sizes(double  ) {
   
  return {10, 9}; }

static void layout_set_natural_single_line(PangoLayout* layout) {
   
    pango_layout_set_width(layout, -1);
    pango_layout_set_wrap(layout, PANGO_WRAP_NONE);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
}

static int layout_natural_pixel_width(PangoLayout* layout) {
   
    layout_set_natural_single_line(layout);
    int w = 0;
    int h = 0;
    pango_layout_get_pixel_size(layout, &w, &h);
    return w;
}

static constexpr const char kTitleArtistDotUtf8[] = " \xe2\x80\xa2 ";

static double title_artist_sep_width_px(cairo_t* measure_cr, double s) {
    
    const double sep_min = std::max(6.0, 8.0 * s);
    if (!measure_cr) return sep_min;
    static int cached_dot_w = 0;
    if (cached_dot_w == 0) {
        PangoLayout* l = make_layout(measure_cr, "Inter SemiBold 9");
        pango_layout_set_text(l, kTitleArtistDotUtf8, -1);
        cached_dot_w = layout_natural_pixel_width(l);
        g_object_unref(l);
    }
    return std::max(sep_min, static_cast<double>(cached_dot_w));
}

static double marquee_triangle_offset(double range_px, double steady_sec, double period_sec) {
   
    if (range_px <= 0.5) return 0.0;
    const double u = std::fmod(steady_sec / period_sec, 1.0);
    const double tri = u < 0.5 ? (u * 2.0) : (2.0 - u * 2.0);
    return tri * range_px;
}

double ctrl_row_width(double s) {
   
    const double btnGap = std::max(4.0, 6.0 * s);
    const double bsr = kBtnSmallD * s * 0.5;
    const double bpr = kBtnPlayD * s * 0.5;
    return 4.0 * bsr + 2.0 * btnGap + 2.0 * bpr;
}

void fill_circle(cairo_t* cr, double cx, double cy, double r,
                 double rr, double gg, double bb, double aa) {
   
    cairo_arc(cr, cx, cy, r, 0, 2.0*M_PI);
    cairo_set_source_rgba(cr, rr, gg, bb, aa);
    cairo_fill(cr);
}

}

// Animations disabled — no per-widget crossfade state needed.

bool widget_list_contains_media(const eh::config::ShellConfig& sc,
                                 const std::vector<std::string>& widgets) {
   
    (void)sc;
    for (const auto& id : widgets)
        if (eh::config::widget_implementation_type(id) == "media") return true;
    return false;
}

double dock_media_slot_width(cairo_t* measure_cr,
                              const eh::config::ShellConfig& sc,
                              std::string_view instance_id,
                              double icon_ref_px, double bar_height,
                              const eh::mpris::PlayerSnapshot& snap) {
   
    const bool show_text = parse_bool(widget_setting(sc, instance_id, "show_text"), true);
    const double max_tw = parse_max_title_scale(sc, instance_id);
    const double s = icon_ref_px / 30.0;
    const double art_dia = icon_ref_px * 0.78;
    const double iGap = std::max(8.0, 10.0 * s);
    const double max_text = max_tw * s;

    const bool realActive = snap.active && (!snap.title.empty() || !snap.artist.empty());
    const std::string title_m =
        realActive ? (snap.title.empty() ? std::string("Unknown title") : snap.title) : std::string("No Media");
    const std::string artist_m = realActive ? snap.artist : std::string();

    double text_w = 0.0;
    if (show_text) {
        if (!measure_cr) {
            measure_cr = get_measure_cr();
        }
        if (measure_cr) {
            auto [tp, ap] = font_sizes(s);
            std::string fd1 = "Inter SemiBold " + std::to_string(tp);
            std::string fd2 = "Inter SemiBold " + std::to_string(ap);

            const std::string wkid(instance_id);
            auto it = g_mediaTextWidths.find(wkid);
            int tw_nat = 0;
            int aw_nat = 0;
            bool cacheHit = (it != g_mediaTextWidths.end() &&
                             it->second.title == title_m &&
                             it->second.artist == artist_m);
            if (cacheHit) {
                tw_nat = it->second.titleNatW;
                aw_nat = it->second.artistNatW;
            } else {
                PangoLayout* lt = make_layout(measure_cr, fd1.c_str());
                pango_layout_set_text(lt, title_m.c_str(), -1);
                tw_nat = layout_natural_pixel_width(lt);
                g_object_unref(lt);

                if (!artist_m.empty()) {
                    PangoLayout* la = make_layout(measure_cr, fd2.c_str());
                    pango_layout_set_text(la, artist_m.c_str(), -1);
                    aw_nat = layout_natural_pixel_width(la);
                    g_object_unref(la);
                }
                MediaTextWidths& entry = g_mediaTextWidths[wkid];
                entry.title = title_m;
                entry.artist = artist_m;
                entry.titleNatW = tw_nat;
                entry.artistNatW = aw_nat;
            }
            const double between_w = artist_m.empty() ? 0.0 : title_artist_sep_width_px(measure_cr, s);
            const double natural =
                artist_m.empty() ? static_cast<double>(tw_nat)
                                   : static_cast<double>(tw_nat) + between_w + static_cast<double>(aw_nat);

            text_w = std::min(max_text, natural);
        }
    }

    const double cw = realActive ? ctrl_row_width(s) : 0.0;
    const double pill_h = icon_ref_px * slot_pill_style::kPillHeightMul;
    const double cap0 = std::min(bar_height * 22.0, 920.0);
    double rad = slot_pill_style::corner_radius(pill_h, cap0);
    double inner = rad + kMediaOuterHPad;
    double w0 = inner + art_dia + iGap + text_w + iGap + cw + inner;
    rad = slot_pill_style::corner_radius(pill_h, w0);
    inner = rad + kMediaOuterHPad;
    w0 = inner + art_dia + iGap + text_w + iGap + cw + inner;
    rad = slot_pill_style::corner_radius(pill_h, w0);
    inner = rad + kMediaOuterHPad;
    const double min0 = inner + art_dia + iGap + iGap + cw + inner;
    return std::clamp(w0, min0, cap0);
}

bool paint_media_slot(cairo_t* cr,
                       const eh::config::ShellConfig& sc,
                       std::string_view instance_id,
                       double x, double y,
                       double slot_w, double slot_h,
                       double icon_ref_px,
                        const eh::mpris::PlayerSnapshot& snap,
                        [[maybe_unused]] bool hovered, [[maybe_unused]] bool pressed,
                        int hoverBtn,
                        double pointerLocalX, double pointerLocalY,
                        bool* progress_tick_wanted) {

    bool wants_marquee = false;
    if (progress_tick_wanted) *progress_tick_wanted = false;
    if (slot_w <= 1.0) return false;

    const bool show_text = parse_bool(widget_setting(sc, instance_id, "show_text"), true);
    const double max_tw = parse_max_title_scale(sc, instance_id);
    const double s = icon_ref_px / 30.0;
    const double art_dia = icon_ref_px * 0.78;
    const double art_r = art_dia * 0.5;
    const double iGap = std::max(8.0, 10.0 * s);
    const double btnGap = std::max(4.0, 6.0 * s);
    const double max_text_cfg = max_tw * s;
    const double pill_h = slot_h * slot_pill_style::kPillHeightMul;
    const double pill_y = y + (slot_h - pill_h) * 0.5;
    const double cy = pill_y + pill_h * 0.5;
    const double rad = slot_pill_style::corner_radius(pill_h, slot_w);
    const double innerH = rad + kMediaOuterHPad;

    const bool realActive = snap.active && (!snap.title.empty() || !snap.artist.empty());
    const double effective_cw = realActive ? ctrl_row_width(s) : 0.0;
    const double max_text =
        std::max(48.0, std::min(max_text_cfg,
                                slot_w - innerH - art_dia - iGap - iGap - effective_cw - innerH + 1.0));

    const std::string title_m =
        realActive ? (snap.title.empty() ? std::string("Unknown title") : snap.title) : std::string("No Media");
    const std::string artist_m = realActive ? snap.artist : std::string();

    const double tsec = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();

    cairo_save(cr);

    slot_pill_style::paint_pill(cr, x, pill_y, slot_w, pill_h);

    slot_pill_style::pill_clip_path(cr, x, pill_y, slot_w, pill_h);
    cairo_clip(cr);

    const double art_cx = x + innerH + art_r;

    cairo_arc(cr, art_cx, cy, art_r, 0, 2.0*M_PI);
    cairo_set_source_rgba(cr, 0.18, 0.20, 0.22, 1.0);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.12);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    const double text_x = x + innerH + art_dia + iGap;

    constexpr double kMarqueePeriodSec = 12.0;

    auto paint_content = [&](const std::string& p_title, const std::string& p_artist,
                              bool p_active, cairo_surface_t* p_art, double alpha) {
        if (alpha <= 0.001) return;
        cairo_push_group(cr);

        bool drew_art = false;
        if (p_active && p_art && cairo_surface_status(p_art) == CAIRO_STATUS_SUCCESS) {
            const int iw = cairo_image_surface_get_width(p_art);
            const int ih = cairo_image_surface_get_height(p_art);
            if (iw > 0 && ih > 0) {
                const double sc2 = std::max(art_dia/iw, art_dia/ih);
                const double dw = iw*sc2, dh = ih*sc2;
                cairo_save(cr);
                cairo_new_path(cr);
                cairo_arc(cr, art_cx, cy, art_r, 0, 2.0*M_PI);
                cairo_clip(cr);
                cairo_translate(cr, art_cx - dw*0.5, cy - dh*0.5);
                cairo_scale(cr, sc2, sc2);
                cairo_set_source_surface(cr, p_art, 0, 0);
                cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
                cairo_paint(cr);
                cairo_restore(cr);
                drew_art = true;
            }
        }
        if (!drew_art) {
            cairo_set_source_rgba(cr, kSurfR, kSurfG, kSurfB, 1.0);
            const double nr = art_r * 0.28;
            cairo_arc(cr, art_cx - art_r*0.08, cy + art_r*0.06, nr, 0, 2.0*M_PI); cairo_fill(cr);
            cairo_arc(cr, art_cx + art_r*0.10, cy - art_r*0.02, nr*0.85, 0, 2.0*M_PI); cairo_fill(cr);
        }

        if (show_text) {
            auto [tp, ap] = font_sizes(s);
            std::string fd1 = "Inter SemiBold " + std::to_string(tp);
            std::string fd2 = "Inter SemiBold " + std::to_string(ap);

            const double between_w = p_artist.empty() ? 0.0 : title_artist_sep_width_px(cr, s);

            double title_alloc = 0.0;
            double artist_alloc = 0.0;
            int title_nat_w = 0;
            int artist_nat_w = 0;
            bool textCacheHit = false;
            {
                const std::string pwkid(instance_id);
                auto pit = g_mediaTextWidths.find(pwkid);
                textCacheHit = (pit != g_mediaTextWidths.end() &&
                                pit->second.title == p_title &&
                                pit->second.artist == p_artist);
                if (textCacheHit) {
                    title_nat_w = pit->second.titleNatW;
                    artist_nat_w = pit->second.artistNatW;
                }
            }

            PangoLayout* lt = make_layout(cr, fd1.c_str());
            pango_layout_set_text(lt, p_title.c_str(), -1);
            if (!textCacheHit) {
                title_nat_w = layout_natural_pixel_width(lt);
                if (!p_artist.empty()) {
                    PangoLayout* la_measure = make_layout(cr, fd2.c_str());
                    pango_layout_set_text(la_measure, p_artist.c_str(), -1);
                    artist_nat_w = layout_natural_pixel_width(la_measure);
                    g_object_unref(la_measure);
                }
                MediaTextWidths& mtw = g_mediaTextWidths[std::string(instance_id)];
                mtw.title = p_title;
                mtw.artist = p_artist;
                mtw.titleNatW = title_nat_w;
                mtw.artistNatW = artist_nat_w;
            }

            if (p_artist.empty()) {
                title_alloc = std::min(static_cast<double>(title_nat_w), max_text);
            } else {
                const double need = static_cast<double>(title_nat_w + artist_nat_w) + between_w;
                if (need <= max_text + 0.5) {
                    title_alloc = static_cast<double>(title_nat_w);
                    artist_alloc = static_cast<double>(artist_nat_w);
                } else {
                    const double inner = std::max(0.0, max_text - between_w);
                    const double sum = static_cast<double>(title_nat_w + artist_nat_w);
                    if (sum <= 0.5 || inner <= 0.5) {
                        title_alloc = inner * 0.5;
                        artist_alloc = inner - title_alloc;
                    } else {
                        title_alloc = std::floor(inner * static_cast<double>(title_nat_w) / sum);
                        artist_alloc = inner - title_alloc;
                        constexpr double kMinSeg = 8.0;
                        if (title_alloc < kMinSeg) {
                            title_alloc = kMinSeg;
                            artist_alloc = std::max(kMinSeg, inner - title_alloc);
                        } else if (artist_alloc < kMinSeg) {
                            artist_alloc = kMinSeg;
                            title_alloc = std::max(kMinSeg, inner - artist_alloc);
                        }
                    }
                }
            }

            int th = 0;
            pango_layout_set_width(lt, static_cast<int>(title_alloc * PANGO_SCALE));
            pango_layout_set_wrap(lt, PANGO_WRAP_WORD_CHAR);
            pango_layout_set_ellipsize(lt, PANGO_ELLIPSIZE_END);
            {
                int wtmp = 0;
                pango_layout_get_pixel_size(lt, &wtmp, &th);
            }

            const bool title_scroll = static_cast<double>(title_nat_w) > title_alloc + 0.5
                                       && static_cast<double>(title_nat_w) > kScrollThreshold4Words * s;
            if (title_scroll) wants_marquee = true;
            const bool artist_will_scroll =
                !p_artist.empty() && static_cast<double>(artist_nat_w) > artist_alloc + 0.5
                && static_cast<double>(artist_nat_w) > kScrollThreshold4Words * s;

            int ah = 0;
            PangoLayout* la = nullptr;
            if (!p_artist.empty()) {
                la = make_layout(cr, fd2.c_str());
                pango_layout_set_text(la, p_artist.c_str(), -1);
                pango_layout_set_width(la, static_cast<int>(artist_alloc * PANGO_SCALE));
                pango_layout_set_wrap(la, PANGO_WRAP_WORD_CHAR);
                pango_layout_set_ellipsize(la, PANGO_ELLIPSIZE_END);
                {
                    int wtmp = 0;
                    pango_layout_get_pixel_size(la, &wtmp, &ah);
                }
            }

            const double row_h = static_cast<double>(std::max(th, ah));
            const double row_top = cy - row_h * 0.5;
            const double title_y = row_top + (row_h - static_cast<double>(th)) * 0.5 + 1.0;
            const double artist_y = row_top + (row_h - static_cast<double>(ah)) * 0.5 + 1.0;
            const double artist_x = text_x + title_alloc + between_w;

            const double line_text_a = alpha;
            cairo_set_source_rgba(cr, kSurfR, kSurfG, kSurfB, line_text_a);
            if (title_scroll) {
                layout_set_natural_single_line(lt);
                pango_layout_set_text(lt, p_title.c_str(), -1);
                const double range = static_cast<double>(title_nat_w) - title_alloc;
                const double off = marquee_triangle_offset(range, tsec, kMarqueePeriodSec);
                const double clip_h = static_cast<double>(th) + 3.0;
                cairo_save(cr);
                cairo_rectangle(cr, text_x, row_top - 1.0, title_alloc, clip_h);
                cairo_clip(cr);
                cairo_move_to(cr, text_x - off, title_y);
                pango_cairo_show_layout(cr, lt);
                cairo_restore(cr);
                pango_layout_set_width(lt, static_cast<int>(title_alloc * PANGO_SCALE));
                pango_layout_set_wrap(lt, PANGO_WRAP_WORD_CHAR);
                pango_layout_set_ellipsize(lt, PANGO_ELLIPSIZE_END);
            } else {
                cairo_move_to(cr, text_x, title_y);
                pango_cairo_show_layout(cr, lt);
            }

            if (!p_artist.empty()) {
                PangoLayout* dot_layout = make_layout(cr, fd2.c_str());
                pango_layout_set_text(dot_layout, kTitleArtistDotUtf8, -1);
                layout_set_natural_single_line(dot_layout);
                int dot_w_px = 0;
                int dot_h_px = 0;
                pango_layout_get_pixel_size(dot_layout, &dot_w_px, &dot_h_px);
                const double dot_x = text_x + title_alloc + (between_w - static_cast<double>(dot_w_px)) * 0.5;
                const double dot_y = row_top + (row_h - static_cast<double>(dot_h_px)) * 0.5;
                cairo_set_source_rgba(cr, kSurfR, kSurfG, kSurfB, line_text_a);
                cairo_move_to(cr, dot_x, dot_y);
                pango_cairo_show_layout(cr, dot_layout);
                g_object_unref(dot_layout);
            }

            if (la) {
                const bool artist_scroll = artist_will_scroll;
                if (artist_scroll) wants_marquee = true;
                cairo_set_source_rgba(cr, kSurfR, kSurfG, kSurfB, line_text_a);
                if (artist_scroll) {
                    layout_set_natural_single_line(la);
                    pango_layout_set_text(la, p_artist.c_str(), -1);
                    const double range = static_cast<double>(artist_nat_w) - artist_alloc;
                    const double off = marquee_triangle_offset(range, tsec + 2.7, kMarqueePeriodSec);
                    const double clip_h = static_cast<double>(ah) + 3.0;
                    cairo_save(cr);
                    cairo_rectangle(cr, artist_x, row_top - 1.0, artist_alloc, clip_h);
                    cairo_clip(cr);
                    cairo_move_to(cr, artist_x - off, artist_y);
                    pango_cairo_show_layout(cr, la);
                    cairo_restore(cr);
                } else {
                    cairo_move_to(cr, artist_x, artist_y);
                    pango_cairo_show_layout(cr, la);
                }
                g_object_unref(la);
            }
            g_object_unref(lt);
        }

        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, alpha);
    };

    paint_content(title_m, artist_m, realActive, snap.art.get(), 1.0);

    if (pointerLocalX >= 0) {
        const double dx = pointerLocalX - art_cx;
        const double dy = pointerLocalY - cy;
        if (dx*dx + dy*dy <= art_r*art_r) {
            cairo_new_path(cr);
            cairo_arc(cr, art_cx, cy, art_r - 1.0, 0, 2.0 * M_PI);
            cairo_close_path(cr);
            cairo_set_source_rgba(cr, slot_pill_style::g_hoverAccentR, slot_pill_style::g_hoverAccentG, slot_pill_style::g_hoverAccentB, 0.7);
            cairo_set_line_width(cr, 2.5);
            cairo_stroke(cr);
        }
    }

    if (realActive) {
    const double bsr        = kBtnSmallD * s * 0.5;
    const double bpr        = kBtnPlayD  * s * 0.5;
    const double ctrl_right = x + slot_w - innerH;
    const double next_cx    = ctrl_right - bsr;
    const double play_cx    = next_cx - bsr - btnGap - bpr;
    const double prev_cx    = play_cx - bpr - btnGap - bsr;

    const bool playing = (snap.playback_status == "Playing");
    const double glyph_prev = std::clamp(13.0 * s, 12.0, 18.0);
    const double glyph_play = std::clamp(16.0 * s, 14.0, 21.0);
    const double glyph_next = glyph_prev;

    constexpr double kBtnBgA = 0.22, kBtnBgAOn = 0.55;

    auto paint_btn_ring = [&](double cx, double r) {
      cairo_new_path(cr);
      cairo_arc(cr, cx, cy, r - 1.0, 0, 2.0 * M_PI);
      cairo_close_path(cr);
      cairo_set_source_rgba(cr, slot_pill_style::g_hoverAccentR, slot_pill_style::g_hoverAccentG, slot_pill_style::g_hoverAccentB, 0.6);
      cairo_set_line_width(cr, 2.0);
      cairo_stroke(cr);
    };

    fill_circle(cr, prev_cx, cy, bsr, slot_pill_style::g_mediaBtnR, slot_pill_style::g_mediaBtnG, slot_pill_style::g_mediaBtnB, snap.can_go_previous ? kBtnBgAOn : kBtnBgA);
    if (hoverBtn == 0) paint_btn_ring(prev_cx, bsr);
    eh::shell::draw_material_glyph(cr, prev_cx, cy, glyph_prev, "skip_previous", slot_pill_style::g_mediaGlyphR, slot_pill_style::g_mediaGlyphG, slot_pill_style::g_mediaGlyphB, 1.0);

    fill_circle(cr, play_cx, cy, bpr, slot_pill_style::g_hoverAccentR, slot_pill_style::g_hoverAccentG, slot_pill_style::g_hoverAccentB, playing ? 0.95 : kBtnBgA);
    if (hoverBtn == 1) paint_btn_ring(play_cx, bpr);
    if (playing) {
        eh::shell::draw_material_glyph(cr, play_cx, cy, glyph_play, "pause", slot_pill_style::g_mediaOnAccentR, slot_pill_style::g_mediaOnAccentG, slot_pill_style::g_mediaOnAccentB, 1.0);
    } else {
        eh::shell::draw_material_glyph(cr, play_cx, cy, glyph_play, "play_arrow", slot_pill_style::g_mediaGlyphR, slot_pill_style::g_mediaGlyphG, slot_pill_style::g_mediaGlyphB, 1.0);
    }

    fill_circle(cr, next_cx, cy, bsr, slot_pill_style::g_mediaBtnR, slot_pill_style::g_mediaBtnG, slot_pill_style::g_mediaBtnB, snap.can_go_next ? kBtnBgAOn : kBtnBgA);
    if (hoverBtn == 2) paint_btn_ring(next_cx, bsr);
    eh::shell::draw_material_glyph(cr, next_cx, cy, glyph_next, "skip_next", slot_pill_style::g_mediaGlyphR, slot_pill_style::g_mediaGlyphG, slot_pill_style::g_mediaGlyphB, 1.0);
    }

    cairo_restore(cr);

    // ════════════════════════════════════════════════════════════════════
    // Snaking progress border around the pill (hidden for live streams)
    //
    // NOTE: this deliberately does NOT request continuous frames.  The dash
    // advances only a few px per second for a typical track, so redrawing at
    // vsync rate is wasted work; the dock arms a low-frequency timer tick
    // (see dock_bar.cpp media anim timer) to repaint the pill instead.
    const bool showProgress = realActive && snap.playback_status == "Playing" && snap.duration_us > 0 &&
                              snap.track_url_raw.find("twitch.tv") == std::string::npos;
    if (showProgress) {
        if (progress_tick_wanted) *progress_tick_wanted = true;

        // Smooth position tracking — always project forward from
        // steady_clock.  MPRIS snapshot is ONLY used to detect genuine
        // seeks (position jumps >2s between successive snapshots).
        // Snapshots reporting position 0 mid-track are discarded
        // (some players briefly emit 0 between track metadata updates).
        static std::string s_pos_track;
        static int64_t s_pos_us = 0;
        static int64_t s_last_snap = 0;
        static std::chrono::steady_clock::time_point s_pos_at;

        auto now = std::chrono::steady_clock::now();
        int64_t new_snap = snap.position_us;

        // Discard spurious 0-positions when we're clearly past the start
        if (new_snap == 0 && s_pos_us > 2000000) new_snap = s_last_snap;

        bool seek = false;
        if (s_last_snap > 0 && snap.track_id == s_pos_track) {
            int64_t delta = new_snap - s_last_snap;
            seek = (delta > 2000000 || delta < -2000000);
        }

        if (snap.track_id != s_pos_track || seek) {
            s_pos_track = snap.track_id;
            s_pos_us = new_snap;
            s_pos_at = now;
        } else {
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - s_pos_at).count();
            s_pos_us = s_pos_us + elapsed;
            s_pos_at = now;
        }

        s_last_snap = new_snap;
        int64_t pos_us = std::min(s_pos_us, snap.duration_us);

        const double frac = std::clamp(static_cast<double>(pos_us) /
                                        static_cast<double>(snap.duration_us), 0.0, 1.0);
        if (frac > 0.005) {
            const double ha = M_PI_2 * rad;
            const double sw = std::max(0.0, slot_w - 2.0 * rad);
            const double sh = std::max(0.0, pill_h - 2.0 * rad);
            const double peri = 2.0 * sw + 2.0 * sh + 4.0 * ha;
            const double dashes[2] = {peri * frac, peri * 1e6};

            cairo_save(cr);
            cairo_set_dash(cr, dashes, 2, 0.0);
            cairo_set_line_width(cr, 2.5);
            cairo_set_source_rgba(cr,
                slot_pill_style::g_hoverAccentR,
                slot_pill_style::g_hoverAccentG,
                slot_pill_style::g_hoverAccentB, 0.7);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

            cairo_new_path(cr);
            cairo_move_to(cr, x + rad, pill_y);
            cairo_line_to(cr, x + slot_w - rad, pill_y);
            cairo_arc(cr, x + slot_w - rad, pill_y + rad, rad, -M_PI_2, 0);
            cairo_line_to(cr, x + slot_w, pill_y + pill_h - rad);
            cairo_arc(cr, x + slot_w - rad, pill_y + pill_h - rad, rad, 0, M_PI_2);
            cairo_line_to(cr, x + rad, pill_y + pill_h);
            cairo_arc(cr, x + rad, pill_y + pill_h - rad, rad, M_PI_2, M_PI);
            cairo_line_to(cr, x, pill_y + rad);
            cairo_arc(cr, x + rad, pill_y + rad, rad, M_PI, 3 * M_PI_2);

            cairo_stroke(cr);
            cairo_set_dash(cr, nullptr, 0, 0.0);
            cairo_restore(cr);
        }
    }

    return wants_marquee;
}

}
