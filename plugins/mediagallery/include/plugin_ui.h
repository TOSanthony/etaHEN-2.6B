#pragma once

#include <eta/services.h>
#include <eta/status.h>
#include <eta/ui.h>

#include "plugin_settings.h"

/* This contribution is the configuration page only. The media library itself is
 * served over HTTP and rendered by an external client — ShellUI has no node
 * kind capable of listing media, so it is not used for that. */

#define PLUGIN_UI_STATUS_MAX 192

typedef enum plugin_ui_action_kind {
    PLUGIN_UI_ACTION_NONE = 0,
    PLUGIN_UI_ACTION_SET_ENABLED,
    PLUGIN_UI_ACTION_SET_PORT,
    PLUGIN_UI_ACTION_SET_LOCAL_ONLY,
    PLUGIN_UI_ACTION_RESCAN
} plugin_ui_action_kind;

typedef struct plugin_ui_action {
    plugin_ui_action_kind kind;
    int                   enabled;
    uint16_t              port;
    int                   local_only;
} plugin_ui_action;

typedef struct plugin_ui_state {
    const plugin_settings *settings;
    char                   status[PLUGIN_UI_STATUS_MAX];
    char                   hint[PLUGIN_UI_STATUS_MAX];
} plugin_ui_state;

void plugin_ui_state_init(plugin_ui_state *state,
                          const plugin_settings *settings);
void plugin_ui_state_set_status(plugin_ui_state *state, const char *status,
                                const char *hint);

eta_status plugin_ui_create(const plugin_ui_state *state,
                              eta_ui_document **out_document);

eta_status plugin_ui_decode_action(eta_ui_handle handle,
                                     const eta_ui_event_v1 *event,
                                     plugin_ui_action *out_action);

/* Reflecting a rejected change back into the UI. */
eta_status plugin_ui_set_enabled(const eta_host_services_v1 *services,
                                   eta_ui_handle handle, int enabled);
eta_status plugin_ui_set_port(const eta_host_services_v1 *services,
                                eta_ui_handle handle, uint16_t port);
eta_status plugin_ui_set_local_only(const eta_host_services_v1 *services,
                                      eta_ui_handle handle, int local_only);
