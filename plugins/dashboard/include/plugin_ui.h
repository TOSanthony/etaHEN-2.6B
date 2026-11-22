#pragma once

#include <eta/services.h>
#include <eta/status.h>
#include <eta/ui.h>

#include "plugin_settings.h"

typedef enum plugin_ui_action_kind {
    PLUGIN_UI_ACTION_NONE = 0,
    PLUGIN_UI_ACTION_APPLY_NOW,
    PLUGIN_UI_ACTION_SET_STORE,
    PLUGIN_UI_ACTION_SET_PLUS,
    PLUGIN_UI_ACTION_SET_HUB,
    PLUGIN_UI_ACTION_SET_KEEP,
    PLUGIN_UI_ACTION_SET_NOTIFY,
    PLUGIN_UI_ACTION_SET_BRIDGE
} plugin_ui_action_kind;

typedef struct plugin_ui_action {
    plugin_ui_action_kind kind;
    int value;
} plugin_ui_action;

eta_status plugin_ui_create(const plugin_settings *settings,
                            eta_ui_document **out_document);

eta_status plugin_ui_decode_action(eta_ui_handle handle,
                                   const eta_ui_event_v1 *event,
                                   plugin_ui_action *out_action);
