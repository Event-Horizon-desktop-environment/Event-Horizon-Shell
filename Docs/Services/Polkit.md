# PolicyKit

Located in `src/services/polkit/`. Implements a PolicyKit authentication agent.

## Key Files

| File | Purpose |
|---|---|
| `polkit_auth_service.cpp` / `polkit_auth_service.hpp` | D-Bus PolicyKit authentication agent service |
| `polkit_auth_dialog.cpp` / `polkit_auth_dialog.hpp` | Authentication dialog rendering and interaction |
| `eh_polkit_listener.h` | C API header for the Polkit listener |
| `polkit_bridge_capi.h` | C API bridge for Polkit integration |

## Architecture

1. Registers as a `org.freedesktop.PolicyKit1.AuthenticationAgent` on the system bus
2. Listens for authentication requests from PolicyKit
3. Displays a modal dialog asking for the user's password
4. Sends the response back to PolicyKit
5. Handles success/failure and retry logic

## Dialog

The authentication dialog (`polkit_auth_dialog.cpp`) is rendered as a popup surface with:
- Application icon and name requesting elevation
- Message describing the action
- Password input field
- Confirm and cancel buttons
- Error display for incorrect passwords

## Seat Input

The auth dialog runs on its own Wayland connection (`polkit_conn_`). A `WaylandSeat`
(`polkit_seat_`) bound on that connection routes input to the dialog:
- Pointer motion/buttons are forwarded to the dialog only while the pointer is over the dialog
  surface (`motion_in_dialog`).
- Keyboard keys arrive seat-decoded as `(state, sym, utf8)` and are consumed by
  `PolkitAuthDialog::consume_keyboard_key` — the dialog no longer owns an `xkb_state`.
- The dialog picks its output from `polkit_conn_->outputs()` (never the dock's connection).

The old `polkit_seat_dispatch` forwarding layer (global dialog registry fed by the dock's
`shell_input.cpp`) was deleted when the dialog moved to its own connection.
