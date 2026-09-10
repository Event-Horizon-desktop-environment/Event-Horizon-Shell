#pragma once

#include <glib-object.h>

typedef struct _PolkitAgentListener PolkitAgentListener;

#ifdef __cplusplus
extern "C" {
#endif

PolkitAgentListener *eh_polkit_listener_new(void);

#ifdef __cplusplus
}
#endif
