# On-Screen Display (OSD)

Located in `src/desktop_shell/osd/`. Provides overlay feedback for volume and brightness changes.

---

## File-by-File Detail

### `osd_host.cpp` / `osd_host.hpp` (428 lines)

**Purpose:** Manages OSD layer surfaces and rendering lifecycle.

**Struct `OsdOutputInst`:** Per-output OSD surface state (layer surface, buffer, configured geometry).

**Class `OsdHost::Impl`:**
- `DockApp* dock` - backreference to main app state
- `vector<unique_ptr<OsdOutputInst>> inst` - per-output instances
- `OsdContent content` - current OSD data to display
- `string position` - "top_right", "bottom_right", etc.
- `float ui_scale` - scale factor for DPI

**Animation state machine:**
- `kShowMs = 220ms` - fade in duration
- `kHideDelayMs = 880ms` - visible hold time after last update
- `kHideMs = 200ms` - fade out duration
- `kSlidePx = 24` - slide distance for entry/exit animation

**Functions:**
- `osd_env_disabled()` - checks `EH_OSD` env var (set to "0" or "off" to disable)
- `OsdHost::show(content)` - updates OSD content and starts show animation
- `OsdHost::hide()` - starts hide animation
- `OsdHost::set_position(pos)` - sets OSD position on screen
- `OsdHost::destroy()` - cleans up all surfaces
- `OsdHost::tick(frame_time)` - updates animation state, renders frame

**Rendering:**
1. Semi-transparent rounded rectangle background
2. Material icon glyph (volume_up, brightness_high, etc.)
3. Value text ("75%")
4. Progress bar (filled portion based on value)
5. Slide-in/fade-in animation on show
6. Delay then slide-out/fade-out on hide

**Position:** Configurable via `[appearance]` setting: top_right, top_center, bottom_right, bottom_center

---

### `osd_audio.cpp` / `osd_audio.hpp` (130 lines)

**Purpose:** Audio volume OSD. Called by PipeWire service when volume changes.

**Global state (anonymous namespace):**
- `g_dock` - pointer to DockApp
- `g_audio_osd_dirty` - atomic flag for pending update
- `g_sink_id`, `g_sink_pct`, `g_sink_mute` - current sink (output) state
- `g_src_id`, `g_src_pct`, `g_src_mute` - current source (input) state

**Functions:**
- `sink_glyph(pct, mute)` - returns Material icon: `volume_off` (mute or 0%), `volume_down` (1-33%), `volume_up` (34%+)
- `find_node(snapshot, id)` - finds audio device by node ID in PipeWire snapshot
- `prime_from_snapshot()` - reads current audio state from PipeWire service
- `emit_sink_osd(pct, mute)` - sends OSD content for sink volume change
- `emit_src_osd(pct, mute)` - sends OSD content for source volume change
- `osd_audio_sink_volume_changed(pct, mute)` - public entry point for sink changes
- `osd_audio_src_volume_changed(pct, mute)` - public entry point for source changes
- `osd_audio_poll_all()` - called periodically to sync audio state

**OSD display:** Icon (volume_up/down/off) + percentage text + progress bar.

---

### `osd_brightness.cpp` / `osd_brightness.hpp` (80 lines)

**Purpose:** Brightness OSD. Polls `/sys/class/backlight/` for changes.

**Global state:**
- `g_last_pct` - last displayed brightness percentage
- `g_brightness_primed` - whether initial poll has been done

**Functions:**
- `discover_backlight()` - finds backlight device in `/sys/class/backlight/`. Returns pair of (brightness path, max_brightness path)
- `read_int(path, out)` - reads integer from sysfs file
- `brightness_glyph(norm)` - returns Material icon: `brightness_low` (< 0.4) or `brightness_high`
- `osd_brightness_poll(app)` - **main entry point**: reads current brightness from sysfs, compares to last value, emits OSD if changed. Polls at max once per 350ms.

**OSD display:** Icon (brightness_low/high) + percentage text + progress bar.

**Why sysfs polling instead of D-Bus:** Backlight events don't have a standard D-Bus interface on most systems. Sysfs polling is the reliable cross-compositor approach.
