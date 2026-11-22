#include "plugin_ui.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plugin_config.h"

#define CONTRIBUTION_ID "settings"

static eta_ui_node_desc_v1 make_node(uint32_t kind, const char *id,
                                       const char *parent_id,
                                       const char *title) {
    eta_ui_node_desc_v1 node;
    memset(&node, 0, sizeof(node));
    node.struct_size = sizeof(node);
    node.abi_version = ETA_UI_ABI_VERSION;
    node.kind = kind;
    /* Node ids are protocol identifiers restricted to [A-Za-z0-9_.-]; they are
     * never display text and can never carry a path or URL. */
    snprintf(node.id, sizeof(node.id), "%s", id);
    snprintf(node.parent_id, sizeof(node.parent_id), "%s",
             parent_id ? parent_id : "");
    snprintf(node.title, sizeof(node.title), "%s", title);
    return node;
}

static eta_status add_node(eta_ui_document *document,
                             const eta_ui_node_desc_v1 *node) {
    return eta_ui_document_add_node(document, node);
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
    plugin_ui_state_set_status(state, "Starting…", "");
}

static eta_status add_nodes(eta_ui_document *document,
                              const plugin_ui_state *state) {
    const plugin_settings *settings = state->settings;

    eta_ui_node_desc_v1 node =
        make_node(ETA_UI_NODE_PAGE, "main", NULL, PLUGIN_NAME);
    eta_status status = add_node(document, &node);
    if (status != ETA_OK) return status;

    /* LABEL carries no value and no binding, so the status text is only
     * refreshed by rebuilding the whole document. */
    node = make_node(ETA_UI_NODE_LABEL, "status", "main",
                     state->status[0] ? state->status : "Unknown");
    snprintf(node.description, sizeof(node.description), "%s",
             state->hint[0] ? state->hint : "Read-only JSON API over the "
                                             "console capture library");
    status = add_node(document, &node);
    if (status != ETA_OK) return status;

    node = make_node(ETA_UI_NODE_TOGGLE, "enabled", "main", "HTTP service");
    node.value_type = ETA_UI_VALUE_BOOL;
    node.binding = ETA_UI_BINDING_EVENT;
    snprintf(node.binding_key, sizeof(node.binding_key), "enabled_changed");
    snprintf(node.value, sizeof(node.value), "%s",
             settings->enabled ? "true" : "false");
    snprintf(node.description, sizeof(node.description),
             "Serve the media API on the local network");
    status = add_node(document, &node);
    if (status != ETA_OK) return status;

    node = make_node(ETA_UI_NODE_INPUT, "port", "main", "Port");
    node.value_type = ETA_UI_VALUE_INT;
    node.binding = ETA_UI_BINDING_EVENT;
    node.min_value = 1;
    node.max_value = 65535;
    node.min_length = 1;
    node.max_length = 5;
    snprintf(node.binding_key, sizeof(node.binding_key), "port_changed");
    snprintf(node.value, sizeof(node.value), "%u", (unsigned)settings->port);
    snprintf(node.description, sizeof(node.description),
             "TCP port for the HTTP listener");
    status = add_node(document, &node);
    if (status != ETA_OK) return status;

    node = make_node(ETA_UI_NODE_TOGGLE, "local_only", "main",
                     "Localhost only");
    node.value_type = ETA_UI_VALUE_BOOL;
    node.binding = ETA_UI_BINDING_EVENT;
    snprintf(node.binding_key, sizeof(node.binding_key), "local_only_changed");
    snprintf(node.value, sizeof(node.value), "%s",
             settings->local_only ? "true" : "false");
    snprintf(node.description, sizeof(node.description),
             "Bind 127.0.0.1 so other devices cannot reach the API");
    status = add_node(document, &node);
    if (status != ETA_OK) return status;

    node = make_node(ETA_UI_NODE_ACTION, "rescan", "main",
                     "Rescan media library");
    node.flags = ETA_UI_NODE_FLAG_CONFIRM;
    node.binding = ETA_UI_BINDING_EVENT;
    snprintf(node.binding_key, sizeof(node.binding_key), "rescan_requested");
    snprintf(node.description, sizeof(node.description),
             "Walk %s again and rebuild the index", PLUGIN_MEDIA_ROOT);
    return add_node(document, &node);
}

eta_status plugin_ui_create(const plugin_ui_state *state,
                              eta_ui_document **out_document) {
    if (!state || !state->settings || !out_document)
        return ETA_E_INVALID_ARGUMENT;

    eta_ui_document_desc_v1 description;
    memset(&description, 0, sizeof(description));
    description.struct_size = sizeof(description);
    description.abi_version = ETA_UI_ABI_VERSION;
    description.priority = 100;
    snprintf(description.plugin_id, sizeof(description.plugin_id), "%s",
             PLUGIN_ID);
    snprintf(description.contribution_id, sizeof(description.contribution_id),
             "%s", CONTRIBUTION_ID);
    snprintf(description.title, sizeof(description.title), "%s", PLUGIN_NAME);
    snprintf(description.description, sizeof(description.description),
             "HTTP media API for the console capture library");
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

static eta_status parse_port(const char *value, uint16_t *out_port) {
    char *end = NULL;
    errno = 0;
    const long port = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || port < 1 ||
        port > 65535) {
        return ETA_E_INVALID_ARGUMENT;
    }
    *out_port = (uint16_t)port;
    return ETA_OK;
}

static eta_status parse_bool(const char *value, int *out_flag) {
    if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
        *out_flag = 1;
        return ETA_OK;
    }
    if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0) {
        *out_flag = 0;
        return ETA_OK;
    }
    return ETA_E_INVALID_ARGUMENT;
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

    if (strcmp(event->node_id, "enabled") == 0) {
        if (event->value_type != ETA_UI_VALUE_BOOL)
            return ETA_E_INVALID_ARGUMENT;
        out_action->kind = PLUGIN_UI_ACTION_SET_ENABLED;
        return parse_bool(event->value, &out_action->enabled);
    }

    if (strcmp(event->node_id, "port") == 0) {
        if (event->value_type != ETA_UI_VALUE_INT)
            return ETA_E_INVALID_ARGUMENT;
        out_action->kind = PLUGIN_UI_ACTION_SET_PORT;
        return parse_port(event->value, &out_action->port);
    }

    if (strcmp(event->node_id, "local_only") == 0) {
        if (event->value_type != ETA_UI_VALUE_BOOL)
            return ETA_E_INVALID_ARGUMENT;
        out_action->kind = PLUGIN_UI_ACTION_SET_LOCAL_ONLY;
        return parse_bool(event->value, &out_action->local_only);
    }

    if (strcmp(event->node_id, "rescan") == 0) {
        if (event->value_type != ETA_UI_VALUE_NONE)
            return ETA_E_INVALID_ARGUMENT;
        out_action->kind = PLUGIN_UI_ACTION_RESCAN;
        return ETA_OK;
    }

    return ETA_E_NOT_FOUND;
}

eta_status plugin_ui_set_enabled(const eta_host_services_v1 *services,
                                   eta_ui_handle handle, int enabled) {
    if (!services || handle == 0) return ETA_E_INVALID_ARGUMENT;
    return eta_ui_set_value(services, handle, "enabled", ETA_UI_VALUE_BOOL,
                              enabled ? "true" : "false");
}

eta_status plugin_ui_set_local_only(const eta_host_services_v1 *services,
                                      eta_ui_handle handle, int local_only) {
    if (!services || handle == 0) return ETA_E_INVALID_ARGUMENT;
    return eta_ui_set_value(services, handle, "local_only",
                              ETA_UI_VALUE_BOOL,
                              local_only ? "true" : "false");
}

eta_status plugin_ui_set_port(const eta_host_services_v1 *services,
                                eta_ui_handle handle, uint16_t port) {
    if (!services || handle == 0) return ETA_E_INVALID_ARGUMENT;
    char value[8];
    snprintf(value, sizeof(value), "%u", (unsigned)port);
    return eta_ui_set_value(services, handle, "port", ETA_UI_VALUE_INT,
                              value);
}
