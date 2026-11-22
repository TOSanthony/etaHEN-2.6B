#pragma once

#include <eta/services.h>
#include <eta/status.h>
#include <eta/ui.h>

#include "plugin_settings.h"

typedef enum plugin_ui_action_kind {
    PLUGIN_UI_ACTION_NONE = 0,
    PLUGIN_UI_ACTION_SET_CHANNEL,
    PLUGIN_UI_ACTION_INSTALL_ONE,
    PLUGIN_UI_ACTION_INSTALL_ALL,
    PLUGIN_UI_ACTION_RESTORE
} plugin_ui_action_kind;

typedef struct plugin_ui_action {
    plugin_ui_action_kind kind;
    int channel;
    char plugin_id[16];
} plugin_ui_action;

/* Plugins that can be downloaded (this updater itself is not listed). */
typedef struct updatable_plugin {
    const char *id;
    const char *label;
} updatable_plugin;

extern const updatable_plugin kUpdatablePlugins[];
extern const unsigned kUpdatablePluginCount;

eta_status plugin_ui_create(const plugin_settings *settings, eta_ui_document **out_document);

eta_status plugin_ui_decode_action(eta_ui_handle handle, const eta_ui_event_v1 *event,
                                   plugin_ui_action *out_action);
