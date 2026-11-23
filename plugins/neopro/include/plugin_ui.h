#pragma once

#include <eta/services.h>
#include <eta/status.h>
#include <eta/ui.h>

#include "neo_core.h"
#include "plugin_settings.h"

#define PLUGIN_UI_STATUS_MAX 160

typedef enum plugin_ui_action_kind {
    PLUGIN_UI_ACTION_NONE = 0,
    PLUGIN_UI_ACTION_SET_ENABLED,
    PLUGIN_UI_ACTION_SET_ALL_GAMES,
    PLUGIN_UI_ACTION_SET_GAME,
    PLUGIN_UI_ACTION_SET_FORCE_8GIB,
    PLUGIN_UI_ACTION_SET_GPU,
    PLUGIN_UI_ACTION_SET_GPU_MHZ,
    PLUGIN_UI_ACTION_SET_CPU,
    PLUGIN_UI_ACTION_SET_CPU_MHZ,
    PLUGIN_UI_ACTION_SET_HZ120,
    PLUGIN_UI_ACTION_SYNC
} plugin_ui_action_kind;

typedef struct plugin_ui_action {
    plugin_ui_action_kind kind;
    int      enabled;
    int      game_mode; /* 0 off, 1 Pro, 2 Pro + 8 GiB */
    unsigned mhz;
    char     tid[10];
} plugin_ui_action;

typedef struct plugin_ui_state {
    const plugin_settings *settings;
    char                   status[PLUGIN_UI_STATUS_MAX];
    char                   message[PLUGIN_UI_STATUS_MAX];
    char                   memory_text[PLUGIN_UI_STATUS_MAX];
    char                   gpu_text[PLUGIN_UI_STATUS_MAX];
    char                   cpu_text[PLUGIN_UI_STATUS_MAX];
    neo_limits             limits;
    int                    hz120; /* -1 unavailable, 0 off, 1 on */
    neo_game_info          games[NEO_MAX_GAMES];
    int                    game_count;
} plugin_ui_state;

void plugin_ui_state_init(plugin_ui_state *state, const plugin_settings *settings);

eta_status plugin_ui_create(const plugin_ui_state *state, eta_ui_document **out_document);
eta_status plugin_ui_decode_action(eta_ui_handle handle, const eta_ui_event_v1 *event,
                                   plugin_ui_action *out_action);
