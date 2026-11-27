#include "plugin_ui.h"

#include <stdio.h>
#include <string.h>

#include "plugin_config.h"
#include "plugin_lang.h"

#define CONTRIBUTION_ID "settings"

static eta_ui_node_desc_v1 make_node(uint32_t kind, const char *id,
                                     const char *parent_id,
                                     const char *title) {
    eta_ui_node_desc_v1 node;
    memset(&node, 0, sizeof(node));
    node.struct_size = sizeof(node);
    node.abi_version = ETA_UI_ABI_VERSION;
    node.kind = kind;
    snprintf(node.id, sizeof(node.id), "%s", id);
    snprintf(node.parent_id, sizeof(node.parent_id), "%s",
             parent_id ? parent_id : "");
    snprintf(node.title, sizeof(node.title), "%s", title);
    return node;
}

void plugin_ui_state_set_status(plugin_ui_state *state, const char *status,
                                const char *hint) {
    if (!state) return;
    snprintf(state->status, sizeof(state->status), "%s", status ? status : "");
    snprintf(state->hint, sizeof(state->hint), "%s", hint ? hint : "");
}

void plugin_ui_state_init(plugin_ui_state *state,
                          const plugin_settings *settings) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->settings = settings;
    plugin_ui_state_set_status(state, TR("Verification…", "Checking…"), "");
}

static eta_status add_nodes(eta_ui_document *document,
                            const plugin_ui_state *state) {
    const plugin_settings *settings = state->settings;

    eta_ui_node_desc_v1 node =
        make_node(ETA_UI_NODE_PAGE, "main", NULL, "Orbit Store");
    eta_status status = eta_ui_document_add_node(document, &node);
    if (status != ETA_OK) return status;

    /* LABEL carries no value/binding: refreshed by rebuilding the document
     * (see main.c's rebuild_ui / update_status), same as the mediagallery
     * plugin's status line. */
    node = make_node(ETA_UI_NODE_LABEL, "status", "main",
                     state->status[0] ? state->status : TR("Inconnu", "Unknown"));
    snprintf(node.description, sizeof(node.description), "%s",
             state->hint[0] ? state->hint
                            : TR("Service Orbit Store (catalogue, telechargements)",
                                 "Orbit Store service (catalog, downloads)"));
    status = eta_ui_document_add_node(document, &node);
    if (status != ETA_OK) return status;

    node = make_node(ETA_UI_NODE_ACTION, "start", "main",
                     TR("Demarrer Orbit Store", "Start Orbit Store"));
    node.binding = ETA_UI_BINDING_EVENT;
    snprintf(node.binding_key, sizeof(node.binding_key), "start_requested");
    snprintf(node.description, sizeof(node.description), "%s",
             TR("Lance orbit_store.elf s'il n'est pas deja en cours.",
                "Launches orbit_store.elf if it isn't already running."));
    status = eta_ui_document_add_node(document, &node);
    if (status != ETA_OK) return status;

    node = make_node(ETA_UI_NODE_ACTION, "stop", "main",
                     TR("Arreter Orbit Store", "Stop Orbit Store"));
    node.flags = ETA_UI_NODE_FLAG_CONFIRM;
    node.binding = ETA_UI_BINDING_EVENT;
    snprintf(node.binding_key, sizeof(node.binding_key), "stop_requested");
    snprintf(node.description, sizeof(node.description), "%s",
             TR("Termine le service Orbit Store s'il tourne.",
                "Terminates the Orbit Store service if it is running."));
    status = eta_ui_document_add_node(document, &node);
    if (status != ETA_OK) return status;

    node = make_node(ETA_UI_NODE_TOGGLE, "autostart", "main",
                     TR("Demarrage automatique", "Auto-start"));
    node.value_type = ETA_UI_VALUE_BOOL;
    node.binding = ETA_UI_BINDING_EVENT;
    snprintf(node.binding_key, sizeof(node.binding_key), "autostart_changed");
    snprintf(node.value, sizeof(node.value), "%s",
             settings->autostart ? "true" : "false");
    snprintf(node.description, sizeof(node.description), "%s",
             TR("Lance Orbit Store a chaque demarrage d'etaHEN.",
                "Launches Orbit Store every time etaHEN starts."));
    status = eta_ui_document_add_node(document, &node);
    if (status != ETA_OK) return status;

    node = make_node(ETA_UI_NODE_INPUT, "search_path", "main",
                     TR("Dossier de recherche FFPKG", "FFPKG search folder"));
    node.value_type = ETA_UI_VALUE_STRING;
    node.binding = ETA_UI_BINDING_EVENT;
    node.min_length = 2;
    node.max_length = 255;
    snprintf(node.binding_key, sizeof(node.binding_key),
             "search_path_changed");
    snprintf(node.value, sizeof(node.value), "%s", settings->search_path);
    snprintf(node.description, sizeof(node.description), "%s",
             TR("Dossier scanne pour un fichier PPSA99177.ffpkg.",
                "Folder scanned for a PPSA99177.ffpkg file."));
    status = eta_ui_document_add_node(document, &node);
    if (status != ETA_OK) return status;

    node = make_node(ETA_UI_NODE_ACTION, "install_ffpkg", "main",
                     TR("Installer l'app TV (.ffpkg)",
                        "Install the TV app (.ffpkg)"));
    node.flags = ETA_UI_NODE_FLAG_CONFIRM;
    node.binding = ETA_UI_BINDING_EVENT;
    snprintf(node.binding_key, sizeof(node.binding_key),
             "install_ffpkg_requested");
    snprintf(node.description, sizeof(node.description), "%s",
             TR("Copie le premier .ffpkg trouve vers /data/homebrew ; "
                "ShadowMountPlus l'enregistre en ~10s.",
                "Copies the first .ffpkg found into /data/homebrew; "
                "ShadowMountPlus registers it in ~10s."));
    return eta_ui_document_add_node(document, &node);
}

eta_status plugin_ui_create(const plugin_ui_state *state,
                            eta_ui_document **out_document) {
    if (!state || !state->settings || !out_document) return ETA_E_INVALID_ARGUMENT;

    eta_ui_document_desc_v1 description;
    memset(&description, 0, sizeof(description));
    description.struct_size = sizeof(description);
    description.abi_version = ETA_UI_ABI_VERSION;
    description.priority = 100;
    snprintf(description.plugin_id, sizeof(description.plugin_id), "%s",
             PLUGIN_ID);
    snprintf(description.contribution_id,
             sizeof(description.contribution_id), "%s", CONTRIBUTION_ID);
    snprintf(description.title, sizeof(description.title), "Orbit Store");
    snprintf(description.description, sizeof(description.description), "%s",
             TR("Gestion du service et de l'app TV Orbit Store",
                "Manage the Orbit Store service and TV app"));
    snprintf(description.root_page_id, sizeof(description.root_page_id),
             "main");

    eta_status status = eta_ui_document_create(&description, out_document);
    if (status != ETA_OK) return status;
    status = add_nodes(*out_document, state);
    if (status == ETA_OK) status = eta_ui_document_validate(*out_document);
    if (status != ETA_OK) {
        eta_ui_document_destroy(*out_document);
        *out_document = NULL;
    }
    return status;
}

eta_status plugin_ui_decode_action(eta_ui_handle handle,
                                   const eta_ui_event_v1 *event,
                                   plugin_ui_action *out_action) {
    if (handle == 0 || !event || !out_action) return ETA_E_INVALID_ARGUMENT;
    memset(out_action, 0, sizeof(*out_action));
    if (event->handle != handle ||
        strcmp(event->contribution_id, CONTRIBUTION_ID) != 0) {
        return ETA_E_NOT_FOUND;
    }

    if (strcmp(event->node_id, "start") == 0) {
        if (event->value_type != ETA_UI_VALUE_NONE) return ETA_E_INVALID_ARGUMENT;
        out_action->kind = PLUGIN_UI_ACTION_START;
        return ETA_OK;
    }

    if (strcmp(event->node_id, "stop") == 0) {
        if (event->value_type != ETA_UI_VALUE_NONE) return ETA_E_INVALID_ARGUMENT;
        out_action->kind = PLUGIN_UI_ACTION_STOP;
        return ETA_OK;
    }

    if (strcmp(event->node_id, "autostart") == 0) {
        if (event->value_type != ETA_UI_VALUE_BOOL) return ETA_E_INVALID_ARGUMENT;
        out_action->kind = PLUGIN_UI_ACTION_SET_AUTOSTART;
        if (strcmp(event->value, "true") == 0 ||
            strcmp(event->value, "1") == 0) {
            out_action->autostart = 1;
            return ETA_OK;
        }
        if (strcmp(event->value, "false") == 0 ||
            strcmp(event->value, "0") == 0) {
            return ETA_OK;
        }
        return ETA_E_INVALID_ARGUMENT;
    }

    if (strcmp(event->node_id, "search_path") == 0) {
        if (event->value_type != ETA_UI_VALUE_STRING) return ETA_E_INVALID_ARGUMENT;
        if (event->value[0] != '/') return ETA_E_INVALID_ARGUMENT;
        out_action->kind = PLUGIN_UI_ACTION_SET_SEARCH_PATH;
        snprintf(out_action->search_path, sizeof(out_action->search_path),
                 "%s", event->value);
        return ETA_OK;
    }

    if (strcmp(event->node_id, "install_ffpkg") == 0) {
        if (event->value_type != ETA_UI_VALUE_NONE) return ETA_E_INVALID_ARGUMENT;
        out_action->kind = PLUGIN_UI_ACTION_INSTALL_FFPKG;
        return ETA_OK;
    }

    return ETA_E_NOT_FOUND;
}

eta_status plugin_ui_set_autostart(const eta_host_services_v1 *services,
                                   eta_ui_handle handle, int autostart) {
    if (!services || handle == 0) return ETA_E_INVALID_ARGUMENT;
    return eta_ui_set_value(services, handle, "autostart", ETA_UI_VALUE_BOOL,
                            autostart ? "true" : "false");
}

eta_status plugin_ui_set_search_path(const eta_host_services_v1 *services,
                                     eta_ui_handle handle,
                                     const char *search_path) {
    if (!services || handle == 0) return ETA_E_INVALID_ARGUMENT;
    return eta_ui_set_value(services, handle, "search_path",
                            ETA_UI_VALUE_STRING, search_path);
}
