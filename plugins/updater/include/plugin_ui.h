#pragma once

#include <eta/services.h>
#include <eta/status.h>
#include <eta/ui.h>

#include "downloader.h"
#include "plugin_settings.h"

typedef enum plugin_ui_action_kind {
    PLUGIN_UI_ACTION_NONE = 0,
    PLUGIN_UI_ACTION_SET_CHANNEL,
    PLUGIN_UI_ACTION_INSTALL_ONE,
    PLUGIN_UI_ACTION_REFRESH,
    PLUGIN_UI_ACTION_RESTORE
} plugin_ui_action_kind;

typedef struct plugin_ui_action {
    plugin_ui_action_kind kind;
    int channel;
    char plugin_id[DL_ID_LEN];
} plugin_ui_action;

/* Plugins offered on the page (discovered from the GitHub release). */
typedef struct plugin_list {
    char ids[DL_MAX_PLUGINS][DL_ID_LEN];
    unsigned count;
    int from_release; /* 0 = built-in fallback list (release not reachable yet) */
} plugin_list;

/* Friendly name of a known plugin id (the id itself for unknown plugins). */
const char *plugin_label(const char *id);

eta_status plugin_ui_create(const plugin_settings *settings, const plugin_list *list,
                            const char *status_text, eta_ui_document **out_document);

eta_status plugin_ui_decode_action(eta_ui_handle handle, const eta_ui_event_v1 *event,
                                   plugin_ui_action *out_action);
