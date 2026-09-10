# Audio Service

Located in `src/services/audio/`. Integrates with PipeWire for audio device management and volume control.

---

## PipeWire Service

`pipewire_service.cpp` / `pipewire_service.hpp`

**Class `PipeWireService`:** Singleton accessed via `instance()`.

**Initialization:**
1. Connects to PipeWire context via `pw_context_new()`
2. Creates core via `pw_context_connect()`
3. Registers registry listener for audio nodes
4. Sets up event loop integration (fd-based)

**Device tracking:**
- Monitors sinks (outputs) and sources (inputs)
- Tracks: name, description, volume (0-1), mute, node_id, ports, formats
- Updates on device add/remove/change

**Snapshot state (`Snapshot` struct):**
- `available` - whether PipeWire is connected
- `default_sink` / `default_source` - node_id of default devices
- `sinks` / `sources` - vector of `Device` structs
- `streams` - per-application audio streams

**Public API:**
- `snapshot()` - returns current audio state
- `set_sink_volume(id, 0-1)` - set output volume
- `set_sink_mute(id, bool)` - mute/unmute output
- `set_source_volume(id, 0-1)` - set input volume
- `set_source_mute(id, bool)` - mute/unmute input
- `set_default_sink(id)` - change default output device
- `set_default_source(id)` - change default input device
- `on_change(callback)` - register volume/device change listener

**Threading:** PipeWire runs in its own thread. Updates are pushed to the main thread via `deferred_call::post()`.

**OSD integration:** Calls `osd_audio_sink_volume_changed()` / `osd_audio_src_volume_changed()` when volume changes to trigger OSD display.

---

## Related Services

- [IPC Service](IPC.md) - inter-process communication for external control
- [PolicyKit](Polkit.md) - authentication agent for privilege elevation
