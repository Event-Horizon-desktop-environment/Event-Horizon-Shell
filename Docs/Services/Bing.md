# Bing Wallpaper Service

Located in `src/services/bing/`. Downloads daily Bing wallpapers for use as desktop backgrounds.

---

## File-by-File Detail

### `bing_wallpaper.cpp` / `bing_wallpaper.hpp` (709+ lines)

**Purpose:** Fetches, caches, and manages Bing daily wallpaper downloads.

**Dependencies:** libcurl for HTTP, nlohmann/json for JSON parsing, std::thread for background download.

**Anonymous namespace helpers:**
- `xdg_cache_home()` - resolves cache directory (`~/.cache/event-horizon/bingwall/`)
- `default_download_dir()` - default save directory (`~/Pictures/BingWallpaper`)
- `cache_file_path()` - path to `bing_wallpapers.json` metadata cache
- `curl_write_cb()` - libcurl write callback (appends data to string)
- `curl_fetch_text()` - fetches URL content via libcurl with timeout/error handling

**Key functions:**
- `bing_supported_resolutions()` - returns list of available resolution strings
- `bing_resolve_download_dir()` - resolves download directory from config or default
- `bing_fetch_catalog()` - fetches Bing wallpaper catalog JSON from remote API
- `bing_download_wallpaper()` - downloads a specific wallpaper image file
- `bing_get_current()` - returns current wallpaper metadata (title, copyright, URL)
- `bing_get_cached()` - reads cached wallpaper metadata from disk
- `bing_clear_cache()` - clears cached wallpaper metadata
- `bing_get_cached_wallpapers()` - returns list of all cached wallpaper entries
- `bing_get_wallpaper_state()` - returns current state (idle, fetching, downloaded)
- `bing_schedule_fetch()` - schedules periodic fetch (daily) using `poll_mux` timer
- `bing_start_background_fetch()` - launches background thread to download latest
- `bing_set_download_dir()` - sets custom download directory from config
- `bing_supported_markets()` - returns list of supported market codes (en-US, zh-CN, etc.)

**State management:**
- Uses eventfd to signal completion from background thread to main loop
- Caches downloaded wallpaper metadata in JSON file
- Supports multiple resolutions (1920x1080, 1920x1200, etc.)
- Tracks download progress state for UI display

**Background fetch flow:**
1. `bing_start_background_fetch()` called from main thread
2. Spawns std::thread that calls curl_fetch_text() on Bing API
3. Parses JSON response to extract wallpaper URLs and metadata
4. Downloads the image file to the configured download directory
5. Writes eventfd to signal main thread
6. Main thread processes completion, renders new wallpaper

### `bing_wallpaper_qml_bridge.hpp`

**Purpose:** Bridge between Bing wallpaper service and QML-based settings UI. Provides QObject-wrapped accessors for Bing wallpaper properties and download state for the settings tab.
