#pragma once

#include <glib.h>

#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE
#include <polkitagent/polkitagent.h>

#ifdef __cplusplus
extern "C" {
#endif

// Parity payload for one authentication request: the polkit action id, the
// human-readable message, the session cookie, the themed icon name polkit
// handed us, and a pre-serialized "key=value, ..." run of PolkitDetails.
void eh_polkit_bridge_begin_prompt(const char* action_id, const char* message,
                                   const char* cookie, const char* icon_name,
                                   const char* details);
void eh_polkit_bridge_end_prompt(int gained_authorization);

// Identity chosen for this request ("user" / "Full Name (user)") — the dialog's
// subtitle. Kept separate from the prompt: both arrive as "info" lines today
// and used to clobber one shared field.
void eh_polkit_bridge_set_identity(const char* text);
// The session's request string ("Password:") — the password field's label.
void eh_polkit_bridge_set_prompt(const char* text);

void eh_polkit_bridge_show_error_line(const char* text);
void eh_polkit_bridge_show_info_line(const char* text);

void eh_polkit_bridge_deliver_response(const char* response);
void eh_polkit_bridge_cancel_session(void);

void eh_polkit_bridge_store_session(PolkitAgentSession* session, int echo_on);

// C-side event trail into ~/EH-logs/polkit.log (implemented C++ side).
void eh_polkit_log_c(const char* msg);

#ifdef __cplusplus
}
#endif
