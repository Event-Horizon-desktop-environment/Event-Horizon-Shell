# Wallpaper

Located in `src/wallpaper/`. Handles wallpaper rendering, thumbnailing, and application. 5 `.cpp` / 5 `.hpp` files.

---

## File-by-File Detail

### `wallpaper.cpp` / `wallpaper.hpp`

**Purpose:** Main wallpaper rendering engine.

**`WallpaperRenderer` class:**
- `create(wl_compositor*, wl_shm*, zwlr_layer_shell_v1*, wl_output*)` - creates layer-shell background surface per output
- `set_wallpaper(cairo_surface_t*, scale_mode)` - sets and renders wallpaper image
- `set_background_color(r, g, b)` - sets fallback background color
- `set_transition(duration_ms)` - configures crossfade transition time
- `render()` - renders current frame (with optional transition animation)
- `destroy()` - destroys layer surface and frees resources

**Rendering:**
1. Fill surface with background color
2. Render wallpaper image with chosen scale mode (fill, fit, stretch, center, tile)
3. If transitioning: render both old and new wallpaper at interpolated alpha

**Scale modes:**
- `Fill` - cover entire surface (may crop)
- `Fit` - fit within surface (may letterbox)
- `Stretch` - stretch to fill (may distort)
- `Center` - center at native size
- `Tile` - repeat to fill

### `wallpaper_apply.cpp` / `wallpaper_apply.hpp`

**Purpose:** Coordinates wallpaper loading from config across all outputs.

**Functions:**
- `wallpaper_apply_init(app)` - reads `[wallpaper]` config section, resolves image path
- `wallpaper_apply_to_all_outputs()` - triggers `WallpaperRenderer::set_wallpaper()` on all outputs
- `wallpaper_apply_on_config_change()` - re-applies wallpaper when config changes
- `resolve_wallpaper_path(path)` - resolves relative/absolute/bundled asset paths

### `wallpaper_thumbnail.cpp` / `wallpaper_thumbnail.hpp`

**Purpose:** Generates scaled-down thumbnail images for settings UI.

**Functions:**
- `wallpaper_thumbnail_generate(image_path, max_w, max_h)` - loads and scales down image
- `wallpaper_thumbnail_cache_get(key)` - reads cached thumbnail from disk
- `wallpaper_thumbnail_cache_put(key, surface)` - writes thumbnail to `~/.cache/event-horizon/wallpaper-thumbs/`
- `wallpaper_thumbnail_cache_clear()` - clears cached thumbnails

### `wallpaper_thumbnail_service.cpp` / `wallpaper_thumbnail_service.hpp`

**Purpose:** Background thumbnail generation service.

**`WallpaperThumbnailService` class:**
- `request_thumbnail(image_path, callback)` - queues thumbnail generation
- `cancel_request(image_path)` - cancels pending request
- `process_queue()` - generates thumbnails in background using `poll_mux` idle callback
- Returns thumbnails as shared Cairo surfaces via callback

### `wallpaper_raster_decode.cpp` / `wallpaper_raster_decode.hpp`

**Purpose:** Image decoding utilities.

**Functions:**
- `raster_decode_image(path)` - loads image from disk into Cairo surface
- `raster_decode_svg(path, target_w, target_h)` - renders SVG at target size
- `raster_decode_from_memory(data, size)` - loads image from memory buffer
- Supports: PNG (native Cairo), JPEG, BMP, GIF, TIFF (via Cairo image backends), SVG (via librsvg/nanosvg)
