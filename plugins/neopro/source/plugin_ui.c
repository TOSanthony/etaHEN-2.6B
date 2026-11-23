#include "plugin_ui.h"

#include <stdio.h>
#include <string.h>

#include "plugin_config.h"

#define CONTRIBUTION_ID "settings"

static eta_ui_node_desc_v1 make_node(uint32_t kind, const char *id, const char *parent_id, const char *title) {
    eta_ui_node_desc_v1 node;
    memset(&node, 0, sizeof(node));
    node.struct_size = sizeof(node);
    node.abi_version = ETA_UI_ABI_VERSION;
    node.kind = kind;
    snprintf(node.id, sizeof(node.id), "%s", id);
    snprintf(node.parent_id, sizeof(node.parent_id), "%s", parent_id ? parent_id : "");
    snprintf(node.title, sizeof(node.title), "%s", title);
    return node;
}

void plugin_ui_state_init(plugin_ui_state *state, const plugin_settings *settings) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->settings = settings;
    snprintf(state->status, sizeof(state->status), "Demarrage");
}

static eta_status add_toggle(eta_ui_document *doc, const char *id, const char *key, const char *title,
                             const char *desc, int on) {
    eta_ui_node_desc_v1 node = make_node(ETA_UI_NODE_TOGGLE, id, "main", title);
    node.value_type = ETA_UI_VALUE_BOOL;
    node.binding = ETA_UI_BINDING_EVENT;
    snprintf(node.binding_key, sizeof(node.binding_key), "%s", key);
    snprintf(node.value, sizeof(node.value), "%s", on ? "true" : "false");
    snprintf(node.description, sizeof(node.description), "%s", desc);
    return eta_ui_document_add_node(doc, &node);
}

static eta_status add_nodes(eta_ui_document *doc, const plugin_ui_state *st) {
    eta_ui_node_desc_v1 node = make_node(ETA_UI_NODE_PAGE, "main", NULL, PLUGIN_NAME);
    eta_status status = eta_ui_document_add_node(doc, &node);
    if (status != ETA_OK) return status;

    /* Read-only status: a LIST with a single item (a DISABLED node would break the whole page). */
    node = make_node(ETA_UI_NODE_LIST, "status", "main", "Statut");
    node.value_type = ETA_UI_VALUE_STRING;
    node.binding = ETA_UI_BINDING_EVENT;
    snprintf(node.binding_key, sizeof(node.binding_key), "status_selected");
    snprintf(node.value, sizeof(node.value), "%s", st->status);
    if ((status = eta_ui_document_add_node(doc, &node)) != ETA_OK) return status;
    node = make_node(ETA_UI_NODE_LIST_ITEM, "status_item", "status", st->status);
    node.value_type = ETA_UI_VALUE_STRING;
    snprintf(node.value, sizeof(node.value), "%s", st->status);
    if ((status = eta_ui_document_add_node(doc, &node)) != ETA_OK) return status;

    if ((status = add_toggle(doc, "enabled", "enabled_changed", "Mode PS4 Pro",
                             "Active le mode PS4 Pro pour les jeux PS4 (ShellCore hook + patchs XML).",
                             st->settings->enabled)) != ETA_OK) return status;
    if ((status = add_toggle(doc, "all_games", "all_games_changed", "Tous les jeux PS4",
                             "Force le mode Pro pour tous les jeux PS4. Certains jeux ont besoin de leur patch XML pour demarrer.",
                             st->settings->all_games)) != ETA_OK) return status;

    node = make_node(ETA_UI_NODE_ACTION, "sync", "main", "Reinstaller les patchs de jeux");
    node.flags = ETA_UI_NODE_FLAG_CONFIRM;
    node.binding = ETA_UI_BINDING_EVENT;
    snprintf(node.binding_key, sizeof(node.binding_key), "sync_requested");
    snprintf(node.description, sizeof(node.description), "%s",
             st->message[0] ? st->message : "Copie les patchs inclus dans /data/user/patches/xml_orbis.");
    if ((status = eta_ui_document_add_node(doc, &node)) != ETA_OK) return status;

    for (int i = 0; i < st->game_count; i++) {
        char id[32], key[48], title[128];
        snprintf(id, sizeof(id), "game_%s", st->games[i].tid);
        snprintf(key, sizeof(key), "game_%s_changed", st->games[i].tid);
        snprintf(title, sizeof(title), "%.90s", st->games[i].name);
        if ((status = add_toggle(doc, id, key, title, st->games[i].tid, st->games[i].pro_on)) != ETA_OK) return status;
    }
    return ETA_OK;
}

eta_status plugin_ui_create(const plugin_ui_state *state, eta_ui_document **out_document) {
    if (!state || !state->settings || !out_document) return ETA_E_INVALID_ARGUMENT;
    eta_ui_document_desc_v1 d;
    memset(&d, 0, sizeof(d));
    d.struct_size = sizeof(d);
    d.abi_version = ETA_UI_ABI_VERSION;
    d.priority = 100;
    snprintf(d.plugin_id, sizeof(d.plugin_id), "%s", PLUGIN_ID);
    snprintf(d.contribution_id, sizeof(d.contribution_id), "%s", CONTRIBUTION_ID);
    snprintf(d.title, sizeof(d.title), "%s", PLUGIN_NAME);
    snprintf(d.description, sizeof(d.description), "Jeux PS4 en mode PS4 Pro (ps-patch-system + NeoEnabler)");
    snprintf(d.root_page_id, sizeof(d.root_page_id), "main");
    eta_status status = eta_ui_document_create(&d, out_document);
    if (status != ETA_OK) return status;
    status = add_nodes(*out_document, state);
    if (status == ETA_OK) status = eta_ui_document_validate(*out_document);
    if (status != ETA_OK) {
        eta_ui_document_destroy(*out_document);
        *out_document = NULL;
    }
    return status;
}

static int parse_bool(const char *v) { return !strcmp(v, "true") || !strcmp(v, "1"); }

eta_status plugin_ui_decode_action(eta_ui_handle handle, const eta_ui_event_v1 *event, plugin_ui_action *out) {
    if (handle == 0 || !event || !out) return ETA_E_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (event->handle != handle || strcmp(event->contribution_id, CONTRIBUTION_ID) != 0) return ETA_E_NOT_FOUND;
    if (!strcmp(event->node_id, "enabled")) {
        out->kind = PLUGIN_UI_ACTION_SET_ENABLED;
        out->enabled = parse_bool(event->value);
        return ETA_OK;
    }
    if (!strcmp(event->node_id, "all_games")) {
        out->kind = PLUGIN_UI_ACTION_SET_ALL_GAMES;
        out->enabled = parse_bool(event->value);
        return ETA_OK;
    }
    if (!strcmp(event->node_id, "sync")) {
        out->kind = PLUGIN_UI_ACTION_SYNC;
        return ETA_OK;
    }
    if (!strncmp(event->node_id, "game_", 5) && strlen(event->node_id) == 5 + 9) {
        out->kind = PLUGIN_UI_ACTION_SET_GAME;
        out->enabled = parse_bool(event->value);
        snprintf(out->tid, sizeof(out->tid), "%s", event->node_id + 5);
        return ETA_OK;
    }
    return ETA_E_NOT_FOUND;
}
