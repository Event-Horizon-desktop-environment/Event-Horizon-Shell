#pragma once

#include <glib.h>

#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE
#include <polkitagent/polkitagent.h>

#ifdef __cplusplus
extern "C" {
#endif

void eh_polkit_bridge_begin_prompt(const char* action_id, const char* message, const char* cookie);
void eh_polkit_bridge_end_prompt(void);

void eh_polkit_bridge_show_error_line(const char* text);
void eh_polkit_bridge_show_info_line(const char* text);

void eh_polkit_bridge_deliver_response(const char* response);
void eh_polkit_bridge_cancel_session(void);

void eh_polkit_bridge_store_session(PolkitAgentSession* session, int echo_on);

#ifdef __cplusplus
}
#endif
