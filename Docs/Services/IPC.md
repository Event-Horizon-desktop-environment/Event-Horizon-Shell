# IPC Service

Located in `src/services/ipc/`. Provides an inter-process communication mechanism for external control.

## Files

- `ipc_server.cpp` / `ipc_server.hpp` - Unix socket server. Accepts connections, parses text commands, dispatches to shell components. `IpcService` class registered with `poll_mux`.

## Protocol

The IPC service listens on a Unix socket (`/tmp/event-horizon-ipc-<pid>.sock` or similar). Commands are sent as text lines and responses are returned as text.

## Commands

- `toggle <feature>` - Toggle a shell feature (e.g., nightlight, do-not-disturb)
- `set <feature> <value>` - Set a feature value
- `get <feature>` - Get a feature value
- `notify <title> <body>` - Show a notification

## Client

`src/ipc_client/entry_point.cpp` - Standalone binary for sending IPC commands from scripts or keybindings. Usage:

```sh
eh-ipc-client toggle nightlight
eh-ipc-client get volume
```

## Integration

The IPC service runs within the shell process and handles connections in the main event loop. It uses Unix socket polling via `poll_mux`.
