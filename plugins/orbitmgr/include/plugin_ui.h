#pragma once

#include <eta/services.h>
#include <eta/status.h>
#include <eta/ui.h>

#include "plugin_settings.h"

#define PLUGIN_UI_STATUS_MAX 192

typedef enum plugin_ui_action_kind {
    PLUGIN_UI_ACTION_NONE = 0,
    PLUGIN_UI_ACTION_START,
    PLUGIN_UI_ACTION_STOP,
    PLUGIN_UI_ACTION_SET_AUTOSTART,
    PLUGIN_UI_ACTION_SET_SEARCH_PATH,
    PLUGIN_UI_ACTION_INSTALL_FFPKG
} plugin_ui_action_kind;

typedef struct plugin_ui_action {
    plugin_ui_action_kind kind;
    int autostart;
    char search_path[256];
} plugin_ui_action;

typedef struct plugin_ui_state {
    const plugin_settings *settings;
    char status[PLUGIN_UI_STATUS_MAX]; /* "Running" / "Stopped" */
    char hint[PLUGIN_UI_STATUS_MAX];   /* last install/start/stop result */
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
eta_status plugin_ui_set_autostart(const eta_host_services_v1 *services,
                                   eta_ui_handle handle, int autostart);
eta_status plugin_ui_set_search_path(const eta_host_services_v1 *services,
                                     eta_ui_handle handle,
                                     const char *search_path);
