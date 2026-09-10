#include "core/viewer/app.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sys/stat.h>
#include <time.h>

namespace fs = std::filesystem;

namespace hpv {

void App::open_file_dialog() {
    std::string parent_handle = "wayland:wl_surface@";
    uint32_t surface_id = wl_proxy_get_id((struct wl_proxy*)surface_);
    parent_handle += std::to_string(surface_id);

    std::vector<std::string> image_mime_types = {
        "image/png",
        "image/jpeg",
        "image/gif",
        "image/bmp",
        "image/webp",
        "image/tiff",
        "image/heic",
        "image/heif",
        "image/avif",
        "image/jxl",
        "image/svg+xml",
    };

    portal_dialog_.open_file(parent_handle, [this](const std::string& path) {
        on_file_dialog_result(path);
    }, image_mime_types);
}

void App::on_file_dialog_result(const std::string& path) {
    if (path.empty()) return;
    open_file(path);
}

void App::open_file(std::string path) {
    std::cerr << "[nav] open_file: " << path << "\n";
    fs::path fp(path);
    if (!fs::exists(fp)) { std::cerr << "[nav] open_file: path not found\n"; return; }

    // If the path is a directory, open it instead
    if (fs::is_directory(fp)) {
        open_directory(fp.string());
        return;
    }

    current_path_ = path;
    decoded_image_ = DecodedImage{};
    dragging_ = false;
    zoom_ = config_.default_zoom;
    pan_x_ = 0.0f;
    pan_y_ = 0.0f;
    update_title();

    auto dir = fp.parent_path().string();
    load_directory(dir);
    load_image(path);

    // Track recent files
    {
        auto& recent = config_.recent_files;
        auto it = std::find(recent.begin(), recent.end(), path);
        if (it != recent.end()) {
            std::rotate(recent.begin(), it, it + 1);
        } else {
            recent.insert(recent.begin(), path);
            if (recent.size() > Config::kMaxRecentFiles) {
                recent.pop_back();
            }
        }
        Config::save(config_);
    }

    render();
}

void App::open_directory(const std::string& path) {
    load_directory(path);
    if (!dir_images_.empty()) {
        open_file(dir_images_[0]);
    }
}

void App::load_directory(const std::string& dir) {
    current_dir_ = dir;
    dir_images_ = image_files_in_dir(dir);
    dir_image_index_ = -1;
    thumb_cache_.clear();
    thumb_cache_w_.clear();
    thumb_cache_h_.clear();
    thumb_cache_bytes_ = 0;
    thumb_cache_order_.clear();
    thumb_scroll_ = 0;

    auto it = std::find(dir_images_.begin(), dir_images_.end(), current_path_);
    if (it != dir_images_.end()) {
        dir_image_index_ = (int)std::distance(dir_images_.begin(), it);
    }

    // Queue all thumbnails for background preloading, ordered by distance
    // from the current image so nearest ones appear first.
    thumb_pending_.clear();
    int n = (int)dir_images_.size();
    for (int d = 1; d < n; d++) {
        for (int sign : {-1, 1}) {
            int idx = dir_image_index_ + sign * d;
            if (idx >= 0 && idx < n && idx != dir_image_index_) {
                thumb_pending_.push_back(idx);
            }
        }
    }
    // Ensure current image is cached first (it will be by load_image)
    if (dir_image_index_ >= 0) {
        thumb_cache_[dir_image_index_] = {};
        thumb_cache_w_[dir_image_index_] = 0;
        thumb_cache_h_[dir_image_index_] = 0;
    }
    invalidate_thumb_strip();
}

void App::next_image() {
    if (dir_images_.empty()) return;
    dir_image_index_ = (dir_image_index_ + 1) % (int)dir_images_.size();
    open_file(dir_images_[dir_image_index_]);
}

void App::prev_image() {
    if (dir_images_.empty()) return;
    dir_image_index_ = (dir_image_index_ - 1 + (int)dir_images_.size()) % (int)dir_images_.size();
    open_file(dir_images_[dir_image_index_]);
}

void App::first_image() {
    if (dir_images_.empty()) return;
    dir_image_index_ = 0;
    open_file(dir_images_[0]);
}

void App::last_image() {
    if (dir_images_.empty()) return;
    dir_image_index_ = (int)dir_images_.size() - 1;
    open_file(dir_images_[dir_image_index_]);
}

void App::delete_image() {
    if (current_path_.empty()) return;

    // Advance to next/previous before trashing
    int next_idx = -1;
    if (!dir_images_.empty()) {
        // If this image is in the directory listing, find its index
        auto it = std::find(dir_images_.begin(), dir_images_.end(), current_path_);
        if (it != dir_images_.end()) {
            int idx = (int)(it - dir_images_.begin());
            if (idx + 1 < (int)dir_images_.size())
                next_idx = idx + 1;
            else if (idx > 0)
                next_idx = idx - 1;
        }
    }

    if (!trash_file(current_path_)) return;

    // Remove from current directory listing
    if (!dir_images_.empty()) {
        auto it = std::find(dir_images_.begin(), dir_images_.end(), current_path_);
        if (it != dir_images_.end()) {
            dir_images_.erase(it);
        }
        if (!dir_images_.empty()) {
            // Navigate to next available image
            if (next_idx >= 0 && next_idx < (int)dir_images_.size()) {
                dir_image_index_ = next_idx;
                open_file(dir_images_[dir_image_index_]);
                return;
            }
            // Fallback: first image or last
            dir_image_index_ = 0;
            open_file(dir_images_[0]);
            return;
        }
    }

    // No more images — clear state
    current_path_.clear();
    decoded_image_ = {};
    exif_info_ = {};
    render();
}

void App::load_image(std::string path) {
    if (loading_) return;
    loading_ = true;

    DecodeResult result;
    std::vector<uint8_t> file_buf;

    // Check prefetch pool first
    auto prefetched = decode_pool_.try_claim(path);
    if (prefetched.has_value()) {
        result = std::move(*prefetched);
        std::cout << "Prefetch hit: ";
    } else {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) {
            std::cerr << "Cannot open: " << path << "\n";
            loading_ = false; return;
        }
        fseek(f, 0, SEEK_END);
        size_t size = ftell(f);
        fseek(f, 0, SEEK_SET);
        file_buf.resize(size);
        if (fread(file_buf.data(), 1, size, f) != size) {
            std::cerr << "Failed to read: " << path << "\n";
            fclose(f);
            loading_ = false; return;
        }
        fclose(f);

        result = decoders_.decode(file_buf.data(), file_buf.size(),
                                   window_width_, window_height_);
        if (result.pixels.empty()) {
            std::cerr << "Failed to decode: " << path << "\n";
            loading_ = false; return;
        }
    }

    current_path_ = path;

#ifdef HAVE_LCMS2
    if (!result.icc_profile.empty()) {
        const auto& disp = conn_.display_icc_profile();
        if (!disp.empty() && config_.enable_color_management) {
            apply_color_management(result, disp);
        } else {
            apply_color_management(result);
        }
    }
#endif

#ifdef HAVE_METADATA
    if (!file_buf.empty()) {
        exif_info_ = parse_exif(file_buf.data(), file_buf.size());
    } else {
        exif_info_ = ExifInfo{};
    }
#else
    exif_info_ = ExifInfo{};
#endif

    // Log current state before SVG handling
    std::cerr << "[load_image] format=" << result.format_name
              << " svg_source_bytes=" << svg_source_data_.size()
              << " doc=" << (void*)svg_doc_
              << " cache=" << (void*)svg_vector_cache_
              << "\n";

    // File-level metadata
    {
        struct stat st;
        if (stat(path.c_str(), &st) == 0) {
            exif_info_.file_size = (uint64_t)st.st_size;
            char time_buf[32];
            struct tm* tm = localtime(&st.st_mtime);
            if (tm) {
                strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm);
                exif_info_.file_modified = time_buf;
            }
        }
    }

    std::cout << "Loaded: " << path << " (" << result.width << "x" << result.height
              << ", " << result.format_name << ")\n";

    // Clean up any previous SVG state
    delete svg_doc_; svg_doc_ = nullptr;
    svg_source_data_.clear();
    if (svg_vector_cache_) { cairo_surface_destroy(svg_vector_cache_); svg_vector_cache_ = nullptr; }
    svg_vector_w_ = 0; svg_vector_h_ = 0;
    orig_img_w_ = (float)result.width;
    orig_img_h_ = (float)result.height;
    if (result.format_name == "SVG") {
        if (!file_buf.empty()) {
            svg_source_data_ = file_buf;
        } else {
            // Prefetch hit — re-read the file for source data
            FILE* f = fopen(path.c_str(), "rb");
            if (f) {
                fseek(f, 0, SEEK_END);
                svg_source_data_.resize(ftell(f));
                fseek(f, 0, SEEK_SET);
                if (fread(svg_source_data_.data(), 1, svg_source_data_.size(), f) != svg_source_data_.size())
                    svg_source_data_.clear();
                fclose(f);
            }
        }
        // Parse and cache SVG for vector rendering
        if (!svg_source_data_.empty()) {
            struct timespec tp0, tp1;
            clock_gettime(CLOCK_MONOTONIC, &tp0);
            svg_doc_ = svg_doc_parse(svg_source_data_.data(), svg_source_data_.size());
            clock_gettime(CLOCK_MONOTONIC, &tp1);
            int64_t parse_us = (tp1.tv_sec - tp0.tv_sec) * 1000000 +
                               (tp1.tv_nsec - tp0.tv_nsec) / 1000;
            std::cerr << "[load_image] svg parse: doc=" << (void*)svg_doc_
                      << " " << parse_us << " us\n";
            if (svg_doc_) {
                orig_img_w_ = svg_doc_->width;
                orig_img_h_ = svg_doc_->height;
                result.width = (int)svg_doc_->width;
                result.height = (int)svg_doc_->height;
                std::cerr << "[load_image] svg doc: w=" << orig_img_w_
                          << " h=" << orig_img_h_
                          << " librsvg=" << svg_doc_->from_librsvg << "\n";

                // Try loading a previously-cached native-resolution render
                int nw = (int)orig_img_w_;
                int nh = (int)orig_img_h_;
                if (nw > 0 && nh > 0) {
                    cairo_surface_t* cached = ThumbCache::load_svg_surface(path);
                    if (cached) {
                        int cw = cairo_image_surface_get_width(cached);
                        int ch = cairo_image_surface_get_height(cached);
                        if (cairo_surface_status(cached) == CAIRO_STATUS_SUCCESS &&
                            cw > 0 && ch > 0)
                        {
                            svg_vector_cache_ = cached;
                            svg_vector_w_ = cw;
                            svg_vector_h_ = ch;
                            std::cerr << "[svg] disk cache hit: "
                                      << cw << "x" << ch << "\n";
                        } else {
                            cairo_surface_destroy(cached);
                            std::cerr << "[svg] disk cache ignored "
                                      << "(status=" << cairo_surface_status(cached)
                                      << " dims=" << cw << "x" << ch << ")\n";
                        }
                    }
                }
            }
            result.pixels = std::vector<uint8_t>(4, 0);
            std::cerr << "[svg] parse: " << parse_us << " us, viewbox "
                      << orig_img_w_ << "x" << orig_img_h_ << "\n";
        } else {
            std::cerr << "[load_image] svg_source_data_ empty, svg_doc_ stays "
                      << (void*)svg_doc_ << "\n";
        }
    }

    decoded_image_ = DecodedImage{
        .rgba = std::move(result.pixels),
        .width = result.width,
        .height = result.height,
        .stride = result.width * 4,
    };

    // Cache thumbnail for the current image (before BGRA swizzle — thumbnail gen needs RGBA)
    if (dir_image_index_ >= 0 && svg_source_data_.empty()) {
        gen_thumb_bgra(decoded_image_.rgba, decoded_image_.width, decoded_image_.height,
                       thumb_cache_[dir_image_index_],
                       thumb_cache_w_[dir_image_index_],
                       thumb_cache_h_[dir_image_index_]);
        thumb_cache_bytes_ += thumb_cache_[dir_image_index_].size();
        thumb_cache_order_.push_front(dir_image_index_);
        evict_thumb_cache();
        std::cerr << "[thumb] raster thumb cached: idx=" << dir_image_index_ << "\n";
    } else if (dir_image_index_ >= 0 && svg_doc_) {
        // SVG: render thumbnail at low resolution using Cairo
        int tw = 80, th = 60;
        float scale = std::min((float)tw / orig_img_w_, (float)th / orig_img_h_);
        int rw = std::max(1, (int)(orig_img_w_ * scale));
        int rh = std::max(1, (int)(orig_img_h_ * scale));
        cairo_surface_t* ts = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, rw, rh);
        cairo_t* tc = cairo_create(ts);
        svg_doc_render(tc, svg_doc_, (float)rw, (float)rh);
        cairo_destroy(tc);
        uint8_t* td = cairo_image_surface_get_data(ts);
        int tstride = cairo_image_surface_get_stride(ts);
        thumb_cache_w_[dir_image_index_] = rw;
        thumb_cache_h_[dir_image_index_] = rh;
        thumb_cache_[dir_image_index_].resize((size_t)rh * tstride);
        std::memcpy(thumb_cache_[dir_image_index_].data(), td, (size_t)rh * tstride);
        thumb_cache_bytes_ += thumb_cache_[dir_image_index_].size();
        thumb_cache_order_.push_front(dir_image_index_);
        evict_thumb_cache();
        cairo_surface_destroy(ts);
    }

    // Swizzle decoded_image_ RGBA -> BGRA in-place for direct Cairo display
    // (skipped for SVGs — rendered as vectors, not pixel data)
    if (svg_source_data_.empty()) {
        size_t npix = (size_t)decoded_image_.width * decoded_image_.height;
        for (size_t i = 0; i < npix; i++) {
            std::swap(decoded_image_.rgba[i * 4 + 0], decoded_image_.rgba[i * 4 + 2]);
        }
    }

    invalidate_thumb_strip();

    // Clear markup, crop, and rotation state for new image
    markup_elements_.clear();
    markup_current_.reset();
    markup_drawing_ = false;
    markup_active_ = false;
    crop_active_ = false;
    crop_dragging_ = false;
    rotation_ = 0;
    flip_h_ = false;
    flip_v_ = false;

    present();

    // Prefetch the next image
    if (dir_image_index_ >= 0 && dir_images_.size() > 1) {
        int next = (dir_image_index_ + 1) % (int)dir_images_.size();
        decode_pool_.prefetch(dir_images_[next], decoders_);
    }
    loading_ = false;
}

void App::update_title() {
    if (!xdg_toplevel_) return;
    fs::path p(current_path_);
    auto title = p.filename().string() + " - Horizon Photo Viewer";
    xdg_toplevel_set_title(xdg_toplevel_, title.c_str());
}

std::vector<std::string> App::image_files_in_dir(const std::string& dir) {
    std::vector<std::string> result;
    if (!fs::is_directory(dir)) return result;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return std::tolower(c); });
        if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" ||
            ext == ".gif" || ext == ".bmp" || ext == ".webp" ||
            ext == ".tiff" || ext == ".tif" || ext == ".heic" ||
            ext == ".heif" || ext == ".avif" || ext == ".jxl" ||
            ext == ".svg") {
            result.push_back(entry.path().string());
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

}
