# Project Map

```
horizon-photo-viewer/
├── assets/                  .desktop file, app icon, fonts
├── Docs/                    you are here
├── protocol/                Wayland protocol XMLs + generated code
├── src/
│   ├── main.cpp             entry point, event loop
│   ├── core/
│   │   ├── app.cpp/hpp      main window, key/mouse handling, render loop
│   │   ├── config.cpp/hpp   TOML config read/write
│   │   ├── decode_pool.cpp  background decode queue
│   │   ├── thumb_cache.cpp  generates & caches thumbnail images
│   │   └── trash.cpp        moves files to XDG trash
│   ├── dbus/
│   │   └── portal_file_dialog.cpp  portal-based "Open File" dialog
│   ├── decode/
│   │   ├── decoder.cpp/hpp  base class + registry for image decoders
│   │   ├── wuffs_decoder.cpp  built-in decoder (PNG, GIF, BMP, TIFF, JPEG)
│   │   ├── jpeg_decoder.cpp   optional libjpeg
│   │   ├── webp_decoder.cpp   optional libwebp
│   │   ├── heif_decoder.cpp   optional libheif
│   │   ├── avif_decoder.cpp   optional libavif
│   │   ├── raw_decoder.cpp    optional libraw
│   │   ├── jxl_decoder.cpp    optional libjxl
│   │   ├── exif.cpp/hpp       EXIF parsing (optional, needs exiv2)
│   │   └── color.cpp          color management (optional, needs lcms2)
│   ├── render/
│   │   ├── text_renderer.cpp/hpp  text layout via stb_truetype
│   │   ├── vulkan_context.cpp/hpp   Vulkan instance, device, swapchain
│   │   └── vulkan_surface.cpp/hpp   paints decoded image via Vulkan
│   ├── ui/
│   │   ├── overlay.cpp/hpp   toolbar, settings popup, info overlay, sidebar
│   │   └── thumbnail_strip.cpp/hpp  scrollable thumbnail bar
│   └── wayland/
│       ├── connection.cpp/hpp   wl_display, registry, globals
│       ├── seat.cpp/hpp         keyboard + pointer input
│       ├── shm_buffer.cpp/hpp   shared memory buffer pool for rendering
│       └── surface_extensions.cpp/hpp  xdg-shell, layer-shell, etc
├── subprojects/              meson subproject wrap files (empty, but ready)
├── third_party/              vendored: stb_truetype
├── meson.build               top-level build config
└── justfile                  build recipes
```

## Key flow

1. `main.cpp` creates `App`, enters `wl_display` event loop
2. `App` connects to Wayland, sets up Vulkan, loads config
3. On file open, decoder reads the image → RGBA pixels → Vulkan upload → rendered
4. Overlay + thumbnail strip are drawn with Cairo on top in a second pass
5. Input (keys, mouse, scroll) is handled in `App::on_key` / `on_pointer` / `on_motion` / `on_scroll`
6. Decoding runs in a thread pool (`DecodePool`) so the UI stays responsive

## Adding a new decoder

Drop a file in `src/decode/`, implement `Decoder` interface (decode from path → `DecodedImage`), register in `app.cpp`. Add the meson dep in `src/meson.build`.
