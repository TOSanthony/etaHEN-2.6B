#include "plugin_ui.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ntp_i18n.h"
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
    snprintf(node.id, sizeof(node.id), "%s", id);
    snprintf(node.parent_id, sizeof(node.parent_id), "%s",
             parent_id ? parent_id : "");
    snprintf(node.title, sizeof(node.title), "%s", title);
    return node;
}

static eta_status add_nodes(eta_ui_document *document,
                              const plugin_settings *settings) {
    /* Root page: directly hosts all NTP settings */
    eta_ui_node_desc_v1 node =
        make_node(ETA_UI_NODE_PAGE, "main", NULL, ntp_tr("title"));
    eta_status status = eta_ui_document_add_node(document, &node);
    if (status != ETA_OK) return status;

    /* Informational label */
    node = make_node(ETA_UI_NODE_LABEL, "about", "main", ntp_tr("about_title"));
    snprintf(node.description, sizeof(node.description), "%s", ntp_tr("about_desc"));
    status = eta_ui_document_add_node(document, &node);
    if (status != ETA_OK) return status;

    /* Auto Sync on Boot toggle */
    node = make_node(ETA_UI_NODE_TOGGLE, "enabled", "main", ntp_tr("enabled_title"));
    node.value_type = ETA_UI_VALUE_BOOL;
    node.binding = ETA_UI_BINDING_EVENT;
    snprintf(node.binding_key, sizeof(node.binding_key), "enabled_changed");
    snprintf(node.value, sizeof(node.value), "%s",
             settings->enabled ? "true" : "false");
    snprintf(node.description, sizeof(node.description), "%s", ntp_tr("enabled_desc"));
    status = eta_ui_document_add_node(document, &node);
    if (status != ETA_OK) return status;

    /* NTP Server List */
    const char *current_server = settings->server[0] != '\0'
                                     ? settings->server
                                     : PLUGIN_DEFAULT_SERVER;
    node = make_node(ETA_UI_NODE_LIST, "server", "main", ntp_tr("server_title"));
    node.value_type = ETA_UI_VALUE_STRING;
    node.binding = ETA_UI_BINDING_EVENT;
    snprintf(node.binding_key, sizeof(node.binding_key), "server_changed");
    snprintf(node.value, sizeof(node.value), "%s", current_server);
    snprintf(node.description, sizeof(node.description), "%s", ntp_tr("server_desc"));
    status = eta_ui_document_add_node(document, &node);
    if (status != ETA_OK) return status;

    struct {
        const char *id;
        const char *title_key;
        const char *value;
    } default_items[] = {
        {"srv_fr_pool", "srv_fr_pool", "fr.pool.ntp.org"},
        {"srv_eu_pool", "srv_eu_pool", "europe.pool.ntp.org"},
        {"srv_de_pool", "srv_de_pool", "de.pool.ntp.org"},
        {"srv_ptb", "srv_ptb", "ptbtime1.ptb.de"},
        {"srv_pool", "srv_global", "pool.ntp.org"},
        {"srv_cf", "srv_cf", "time.cloudflare.com"},
        {"srv_google", "srv_google", "time.google.com"},
    };

    bool matched = false;
    for (size_t i = 0; i < sizeof(default_items) / sizeof(default_items[0]); ++i) {
        node = make_node(ETA_UI_NODE_LIST_ITEM, default_items[i].id, "server",
                         ntp_tr(default_items[i].title_key));
        node.value_type = ETA_UI_VALUE_STRING;
        snprintf(node.value, sizeof(node.value), "%s", default_items[i].value);
        status = eta_ui_document_add_node(document, &node);
        if (status != ETA_OK) return status;
        if (strcmp(current_server, default_items[i].value) == 0) {
            matched = true;
        }
    }

    if (!matched) {
        char custom_title[64];
        snprintf(custom_title, sizeof(custom_title), ntp_tr("srv_custom"), current_server);
        node = make_node(ETA_UI_NODE_LIST_ITEM, "srv_custom", "server", custom_title);
        node.value_type = ETA_UI_VALUE_STRING;
        snprintf(node.value, sizeof(node.value), "%s", current_server);
        status = eta_ui_document_add_node(document, &node);
        if (status != ETA_OK) return status;
    }

    /* Interval in minutes */
    node = make_node(ETA_UI_NODE_INPUT, "interval", "main", ntp_tr("interval_title"));
    node.value_type = ETA_UI_VALUE_INT;
    node.binding = ETA_UI_BINDING_EVENT;
    node.min_value = 0;
    node.max_value = 10080;
    node.min_length = 1;
    node.max_length = 5;
    snprintf(node.binding_key, sizeof(node.binding_key), "interval_changed");
    snprintf(node.value, sizeof(node.value), "%u", (unsigned)settings->interval_minutes);
    snprintf(node.description, sizeof(node.description), "%s", ntp_tr("interval_desc"));
    status = eta_ui_document_add_node(document, &node);
    if (status != ETA_OK) return status;

    /* Show Notifications toggle */
    node = make_node(ETA_UI_NODE_TOGGLE, "notify", "main", ntp_tr("notify_title"));
    node.value_type = ETA_UI_VALUE_BOOL;
    node.binding = ETA_UI_BINDING_EVENT;
    snprintf(node.binding_key, sizeof(node.binding_key), "notify_changed");
    snprintf(node.value, sizeof(node.value), "%s",
             settings->notify ? "true" : "false");
    snprintf(node.description, sizeof(node.description), "%s", ntp_tr("notify_desc"));
    status = eta_ui_document_add_node(document, &node);
    if (status != ETA_OK) return status;

    /* Sync Clock Now button */
    node = make_node(ETA_UI_NODE_ACTION, "sync_now", "main", ntp_tr("sync_now_title"));
    node.flags = ETA_UI_NODE_FLAG_CONFIRM;
    node.binding = ETA_UI_BINDING_EVENT;
    snprintf(node.binding_key, sizeof(node.binding_key), "sync_now_requested");
    snprintf(node.description, sizeof(node.description), "%s", ntp_tr("sync_now_desc"));
    return eta_ui_document_add_node(document, &node);
}

eta_status plugin_ui_create(const plugin_settings *settings,
                              eta_ui_document **out_document) {
    if (!settings || !out_document) return ETA_E_INVALID_ARGUMENT;

    eta_ui_document_desc_v1 description;
    memset(&description, 0, sizeof(description));
    description.struct_size = sizeof(description);
    description.abi_version = ETA_UI_ABI_VERSION;
    description.priority = 100;
    snprintf(description.plugin_id, sizeof(description.plugin_id), "%s", PLUGIN_ID);
    snprintf(description.contribution_id, sizeof(description.contribution_id), "%s", CONTRIBUTION_ID);
    snprintf(description.title, sizeof(description.title), "%s", ntp_tr("title"));
    snprintf(description.description, sizeof(description.description), "%s",
             ntp_tr("about_desc"));
    snprintf(description.root_page_id, sizeof(description.root_page_id), "main");

    eta_status status = eta_ui_document_create(&description, out_document);
    if (status != ETA_OK) return status;

    status = add_nodes(*out_document, settings);
    if (status == ETA_OK) {
        status = eta_ui_document_validate(*out_document);
    }
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

    if (strcmp(event->node_id, "sync_now") == 0) {
        out_action->kind = PLUGIN_UI_ACTION_SYNC_NOW;
        return ETA_OK;
    }

    if (strcmp(event->node_id, "enabled") == 0) {
        if (event->value_type != ETA_UI_VALUE_BOOL) return ETA_E_INVALID_ARGUMENT;
        out_action->kind = PLUGIN_UI_ACTION_SET_ENABLED;
        out_action->enabled = (strcmp(event->value, "true") == 0 || strcmp(event->value, "1") == 0);
        return ETA_OK;
    }

    if (strcmp(event->node_id, "server") == 0) {
        if (event->value_type != ETA_UI_VALUE_STRING) return ETA_E_INVALID_ARGUMENT;
        out_action->kind = PLUGIN_UI_ACTION_SET_SERVER;
        snprintf(out_action->server, sizeof(out_action->server), "%s", event->value);
        return ETA_OK;
    }

    if (strcmp(event->node_id, "interval") == 0) {
        if (event->value_type != ETA_UI_VALUE_INT) return ETA_E_INVALID_ARGUMENT;
        char *end = NULL;
        errno = 0;
        long val = strtol(event->value, &end, 10);
        if (errno != 0 || end == event->value || *end != '\0' || val < 0 || val > 10080) {
            return ETA_E_INVALID_ARGUMENT;
        }
        out_action->kind = PLUGIN_UI_ACTION_SET_INTERVAL;
        out_action->interval_minutes = (uint32_t)val;
        return ETA_OK;
    }

    if (strcmp(event->node_id, "notify") == 0) {
        if (event->value_type != ETA_UI_VALUE_BOOL) return ETA_E_INVALID_ARGUMENT;
        out_action->kind = PLUGIN_UI_ACTION_SET_NOTIFY;
        out_action->notify = (strcmp(event->value, "true") == 0 || strcmp(event->value, "1") == 0);
        return ETA_OK;
    }

    return ETA_E_NOT_FOUND;
}

eta_status plugin_ui_set_enabled(const eta_host_services_v1 *services,
                                   eta_ui_handle handle, int enabled) {
    if (!services || handle == 0) return ETA_E_INVALID_ARGUMENT;
    return eta_ui_set_value(services, handle, "enabled",
                              ETA_UI_VALUE_BOOL, enabled ? "true" : "false");
}

eta_status plugin_ui_set_server(const eta_host_services_v1 *services,
                                  eta_ui_handle handle, const char *server) {
    if (!services || handle == 0 || !server) return ETA_E_INVALID_ARGUMENT;
    return eta_ui_set_value(services, handle, "server",
                              ETA_UI_VALUE_STRING, server);
}

eta_status plugin_ui_set_interval(const eta_host_services_v1 *services,
                                    eta_ui_handle handle, uint32_t interval) {
    if (!services || handle == 0) return ETA_E_INVALID_ARGUMENT;
    char val[16];
    snprintf(val, sizeof(val), "%u", (unsigned)interval);
    return eta_ui_set_value(services, handle, "interval",
                              ETA_UI_VALUE_INT, val);
}

eta_status plugin_ui_set_notify(const eta_host_services_v1 *services,
                                  eta_ui_handle handle, int notify) {
    if (!services || handle == 0) return ETA_E_INVALID_ARGUMENT;
    return eta_ui_set_value(services, handle, "notify",
                              ETA_UI_VALUE_BOOL, notify ? "true" : "false");
}
