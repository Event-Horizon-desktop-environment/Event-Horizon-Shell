#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE
#define _POLKIT_AGENT_COMPILATION

#include <polkitagent/polkitagent.h>
#include <polkit/polkit.h>
#include <gio/gio.h>
#include <glib-object.h>

#include "polkit_bridge_capi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <stdint.h>

typedef struct _EhPolkitListener EhPolkitListener;
typedef struct _EhPolkitListenerClass EhPolkitListenerClass;

struct _EhPolkitListenerClass {
  PolkitAgentListenerClass parent_class;
};

struct _EhPolkitListener {
  PolkitAgentListener parent_instance;
  GTask *task;
  PolkitAgentSession *active_session;
  gulong cancel_id;
  GCancellable *cancellable;
};

G_DEFINE_TYPE(EhPolkitListener, eh_polkit_listener, POLKIT_AGENT_TYPE_LISTENER)

#define EH_PL(obj) ((EhPolkitListener *)(obj))

static void eh_polkit_listener_initiate_authentication(PolkitAgentListener *_listener, const gchar *action_id,
                                                        const gchar *message, const gchar *icon_name,
                                                        PolkitDetails *details, const gchar *cookie, GList *identities,
                                                        GCancellable *cancellable, GAsyncReadyCallback callback,
                                                        gpointer user_data);

#define EH_AUTH_SOURCE_TAG ((gpointer)(uintptr_t)eh_polkit_listener_initiate_authentication)

static void eh_polkit_listener_init(EhPolkitListener *self) {
  (void)self;
}

static void eh_polkit_listener_finalize(GObject *object) {
  EhPolkitListener *listener = EH_PL(object);
  if (listener->active_session != NULL) {
    g_object_unref(listener->active_session);
    listener->active_session = NULL;
  }
  g_clear_object(&listener->task);
  G_OBJECT_CLASS(eh_polkit_listener_parent_class)->finalize(object);
}

static gchar *identity_to_human_readable_string(PolkitIdentity *identity) {
  gchar *ret = NULL;
  g_return_val_if_fail(POLKIT_IS_IDENTITY(identity), NULL);
  if (POLKIT_IS_UNIX_USER(identity)) {
    struct passwd pw;
    struct passwd *ppw = NULL;
    char buf[2048];
    const int res = getpwuid_r(polkit_unix_user_get_uid(POLKIT_UNIX_USER(identity)), &pw, buf, sizeof buf, &ppw);
    if (res == 0 && ppw) {
      if (ppw->pw_gecos == NULL || strlen(ppw->pw_gecos) == 0 || strcmp(ppw->pw_gecos, ppw->pw_name) == 0)
        ret = g_strdup_printf("%s", ppw->pw_name);
      else
        ret = g_strdup_printf("%s (%s)", ppw->pw_gecos, ppw->pw_name);
    }
  }
  if (ret == NULL) ret = polkit_identity_to_string(identity);
  return ret;
}

static PolkitIdentity *choose_first_unix_user(GList *identities) {
  for (GList *l = identities; l != NULL; l = l->next) {
    PolkitIdentity *id = POLKIT_IDENTITY(l->data);
    if (POLKIT_IS_UNIX_USER(id)) return id;
  }
  return identities ? POLKIT_IDENTITY(identities->data) : NULL;
}

static void on_completed(PolkitAgentSession *session, gboolean gained_authorization, gpointer user_data) {
  EhPolkitListener *listener = EH_PL(user_data);
  (void)session;
  (void)gained_authorization;

  eh_polkit_bridge_end_prompt();

  if (listener->task) {
    GTask *t = listener->task;
    listener->task = NULL;
    g_task_return_boolean(t, TRUE);
    g_object_unref(t);
  }
  if (listener->active_session) {
    g_object_unref(listener->active_session);
    listener->active_session = NULL;
  }
  if (listener->cancellable) {
    if (listener->cancel_id) g_cancellable_disconnect(listener->cancellable, listener->cancel_id);
    listener->cancel_id = 0;
    g_object_unref(listener->cancellable);
    listener->cancellable = NULL;
  }
}

static void on_request(PolkitAgentSession *session, gchar *request, gboolean echo_on, gpointer user_data) {
  EhPolkitListener *listener = EH_PL(user_data);
  (void)listener;
  eh_polkit_bridge_store_session(session, echo_on != FALSE);
  eh_polkit_bridge_show_info_line(request ? request : "");
}

static void on_show_error(PolkitAgentSession *session, gchar *text, gpointer user_data) {
  (void)session;
  (void)user_data;
  eh_polkit_bridge_show_error_line(text ? text : "");
}

static void on_show_info(PolkitAgentSession *session, gchar *text, gpointer user_data) {
  (void)session;
  (void)user_data;
  eh_polkit_bridge_show_info_line(text ? text : "");
}

static void on_cancelled(GCancellable *cancellable, gpointer user_data) {
  EhPolkitListener *listener = EH_PL(user_data);
  (void)cancellable;
  if (listener->active_session) polkit_agent_session_cancel(listener->active_session);
}

static void eh_polkit_listener_initiate_authentication(PolkitAgentListener *_listener, const gchar *action_id,
                                                        const gchar *message, const gchar *icon_name,
                                                        PolkitDetails *details, const gchar *cookie, GList *identities,
                                                        GCancellable *cancellable, GAsyncReadyCallback callback,
                                                        gpointer user_data) {
  EhPolkitListener *listener = EH_PL(_listener);
  (void)action_id;
  (void)icon_name;
  (void)details;

  if (listener->active_session != NULL) {
    GTask *task = g_task_new(G_OBJECT(listener), cancellable, callback, user_data);
    g_task_set_source_tag(task, EH_AUTH_SOURCE_TAG);
    g_task_return_new_error(task, POLKIT_ERROR, POLKIT_ERROR_FAILED,
                            "An authentication session is already underway.");
    g_object_unref(task);
    return;
  }

  if (g_list_length(identities) < 1) {
    GTask *task = g_task_new(G_OBJECT(listener), cancellable, callback, user_data);
    g_task_set_source_tag(task, EH_AUTH_SOURCE_TAG);
    g_task_return_new_error(task, POLKIT_ERROR, POLKIT_ERROR_FAILED, "No identities available.");
    g_object_unref(task);
    return;
  }

  PolkitIdentity *identity = choose_first_unix_user(identities);
  if (identity == NULL) {
    GTask *task = g_task_new(G_OBJECT(listener), cancellable, callback, user_data);
    g_task_set_source_tag(task, EH_AUTH_SOURCE_TAG);
    g_task_return_new_error(task, POLKIT_ERROR, POLKIT_ERROR_FAILED, "No suitable identity.");
    g_object_unref(task);
    return;
  }

  eh_polkit_bridge_begin_prompt(action_id ? action_id : "", message ? message : "", cookie ? cookie : "");

  gchar *who = identity_to_human_readable_string(identity);
  if (who) {
    eh_polkit_bridge_show_info_line(who);
    g_free(who);
  }

  listener->active_session = polkit_agent_session_new(identity, cookie);
  g_signal_connect(listener->active_session, "completed", G_CALLBACK(on_completed), listener);
  g_signal_connect(listener->active_session, "request", G_CALLBACK(on_request), listener);
  g_signal_connect(listener->active_session, "show-info", G_CALLBACK(on_show_info), listener);
  g_signal_connect(listener->active_session, "show-error", G_CALLBACK(on_show_error), listener);

  listener->task = g_task_new(G_OBJECT(listener), cancellable, callback, user_data);
  g_task_set_source_tag(listener->task, EH_AUTH_SOURCE_TAG);

  listener->cancellable = cancellable ? g_object_ref(cancellable) : NULL;
  listener->cancel_id =
      listener->cancellable ? g_cancellable_connect(listener->cancellable, G_CALLBACK(on_cancelled), listener, NULL) : 0;

  polkit_agent_session_initiate(listener->active_session);
}

static gboolean eh_polkit_listener_initiate_authentication_finish(PolkitAgentListener *_listener, GAsyncResult *res,
                                                                   GError **error) {
  (void)_listener;
  g_warn_if_fail(g_task_is_valid(G_TASK(res), G_OBJECT(_listener)));
  g_warn_if_fail(g_task_get_source_tag(G_TASK(res)) == EH_AUTH_SOURCE_TAG);
  return g_task_propagate_boolean(G_TASK(res), error);
}

static void eh_polkit_listener_class_init(EhPolkitListenerClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  PolkitAgentListenerClass *listener_class = POLKIT_AGENT_LISTENER_CLASS(klass);
  gobject_class->finalize = eh_polkit_listener_finalize;
  listener_class->initiate_authentication = eh_polkit_listener_initiate_authentication;
  listener_class->initiate_authentication_finish = eh_polkit_listener_initiate_authentication_finish;
}

PolkitAgentListener *eh_polkit_listener_new(void) {
  return POLKIT_AGENT_LISTENER(g_object_new(eh_polkit_listener_get_type(), NULL));
}
