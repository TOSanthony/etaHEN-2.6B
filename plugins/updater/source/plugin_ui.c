#include "plugin_ui.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "plugin_config.h"

#define CONTRIBUTION_ID "settings"

typedef struct known_plugin {
    const char *id;
    const char *label;
} known_plugin;

static const known_plugin kKnown[] = {
    {"DPIV00001", "DPI v2"},
    {"FTPS00001", "Serveur FTP"},
    {"NTPS00001", "NTP (heure)"},
    {"DASH00001", "Dashboard"},
    {"MGAL00001", "Media Gallery"},
};

const char *plugin_label(const char *id) {
    for (unsigned i = 0; i < sizeof(kKnown) / sizeof(kKnown[0]); ++i)
        if (strcmp(kKnown[i].id, id) == 0) return kKnown[i].label;
    return id;
}

static eta_ui_node_desc_v1 make_node(uint32_t kind, const char *id, const char *parent_id,
                                     const char *title) {
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

static eta_status add_action(eta_ui_document *doc, const char *id, const char *key,
                             const char *title, const char *desc) {
    eta_ui_node_desc_v1 node = make_node(ETA_UI_NODE_ACTION, id, "main", title);
    node.flags = ETA_UI_NODE_FLAG_CONFIRM;
    node.binding = ETA_UI_BINDING_EVENT;
    snprintf(node.binding_key, sizeof(node.binding_key), "%s", key);
    snprintf(node.description, sizeof(node.description), "%s", desc);
    return eta_ui_document_add_node(doc, &node);
}

static eta_status add_nodes(eta_ui_document *doc, const plugin_settings *s,
                            const plugin_list *list, const char *status_text) {
    (void)status_text; /* progress is shown by the ShellUI progress panel */
    eta_ui_node_desc_v1 node = make_node(ETA_UI_NODE_PAGE, "main", NULL, "Mises a jour des plugins");
    eta_status st = eta_ui_document_add_node(doc, &node);
    if (st != ETA_OK) return st;

    node = make_node(ETA_UI_NODE_LABEL, "about", "main", "A propos");
    snprintf(node.description, sizeof(node.description), "%s",
             "Telecharge les plugins depuis les releases GitHub " UPDATE_REPO
             ". La liste est lue dans la release : un nouveau plugin y apparait tout seul.");
    st = eta_ui_document_add_node(doc, &node);
    if (st != ETA_OK) return st;

    node = make_node(ETA_UI_NODE_LIST, "channel", "main", "Source");
    node.value_type = ETA_UI_VALUE_STRING;
    node.binding = ETA_UI_BINDING_EVENT;
    snprintf(node.binding_key, sizeof(node.binding_key), "channel_changed");
    snprintf(node.value, sizeof(node.value), "%s", s->channel ? "latest" : "nightly");
    snprintf(node.description, sizeof(node.description), "%s",
             "Release a utiliser : Nightly (derniere build de main) ou Latest (derniere release stable).");
    st = eta_ui_document_add_node(doc, &node);
    if (st != ETA_OK) return st;
    node = make_node(ETA_UI_NODE_LIST_ITEM, "ch_nightly", "channel", "Nightly");
    node.value_type = ETA_UI_VALUE_STRING;
    snprintf(node.value, sizeof(node.value), "nightly");
    st = eta_ui_document_add_node(doc, &node);
    if (st != ETA_OK) return st;
    node = make_node(ETA_UI_NODE_LIST_ITEM, "ch_latest", "channel", "Latest (stable)");
    node.value_type = ETA_UI_VALUE_STRING;
    snprintf(node.value, sizeof(node.value), "latest");
    st = eta_ui_document_add_node(doc, &node);
    if (st != ETA_OK) return st;

    st = add_action(doc, "refresh", "refresh_requested", "Actualiser la liste",
                    list->from_release ? "Relit la liste des plugins dans la release."
                                       : "Liste de secours : impossible de lire la release, reessaie.");
    if (st != ETA_OK) return st;
    st = add_action(doc, "install_all", "install_all_requested", "Tout installer / mettre a jour",
                    "Telecharge et installe tous les plugins de la liste.");
    if (st != ETA_OK) return st;
    for (unsigned i = 0; i < list->count; ++i) {
        char id[64], key[64], title[96], path[96], desc[64];
        snprintf(id, sizeof id, "install_%s", list->ids[i]);
        snprintf(key, sizeof key, "install_%s_requested", list->ids[i]);
        snprintf(path, sizeof path, PLUGINS_DIR "/%s.elf", list->ids[i]);
        int installed = access(path, F_OK) == 0;
        snprintf(title, sizeof title, "%s : %s", installed ? "Mettre a jour" : "Installer",
                 plugin_label(list->ids[i]));
        snprintf(desc, sizeof desc, "%s.elf", list->ids[i]);
        st = add_action(doc, id, key, title, desc);
        if (st != ETA_OK) return st;
    }
    return ETA_OK;
}

eta_status plugin_ui_create(const plugin_settings *settings, const plugin_list *list,
                            const char *status_text, eta_ui_document **out_document) {
    if (!settings || !list || !out_document) return ETA_E_INVALID_ARGUMENT;

    eta_ui_document_desc_v1 d;
    memset(&d, 0, sizeof(d));
    d.struct_size = sizeof(d);
    d.abi_version = ETA_UI_ABI_VERSION;
    d.priority = 100;
    snprintf(d.plugin_id, sizeof(d.plugin_id), "%s", PLUGIN_ID);
    snprintf(d.contribution_id, sizeof(d.contribution_id), "%s", CONTRIBUTION_ID);
    snprintf(d.title, sizeof(d.title), "%s", "Mises a jour des plugins");
    snprintf(d.description, sizeof(d.description), "%s", "Telecharger et installer les plugins depuis GitHub.");
    snprintf(d.root_page_id, sizeof(d.root_page_id), "main");

    eta_status st = eta_ui_document_create(&d, out_document);
    if (st != ETA_OK) return st;
    st = add_nodes(*out_document, settings, list, status_text);
    if (st == ETA_OK) st = eta_ui_document_validate(*out_document);
    if (st != ETA_OK) {
        eta_ui_document_destroy(*out_document);
        *out_document = NULL;
    }
    return st;
}

eta_status plugin_ui_decode_action(eta_ui_handle handle, const eta_ui_event_v1 *event,
                                   plugin_ui_action *out) {
    if (handle == 0 || !event || !out) return ETA_E_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (event->handle != handle || strcmp(event->contribution_id, CONTRIBUTION_ID) != 0)
        return ETA_E_NOT_FOUND;

    if (strcmp(event->node_id, "channel") == 0) {
        if (event->value_type != ETA_UI_VALUE_STRING) return ETA_E_INVALID_ARGUMENT;
        out->kind = PLUGIN_UI_ACTION_SET_CHANNEL;
        out->channel = strcmp(event->value, "latest") == 0;
        return ETA_OK;
    }
    if (strcmp(event->node_id, "install_all") == 0) { out->kind = PLUGIN_UI_ACTION_INSTALL_ALL; return ETA_OK; }
    if (strcmp(event->node_id, "restore") == 0) { out->kind = PLUGIN_UI_ACTION_RESTORE; return ETA_OK; }
    if (strcmp(event->node_id, "refresh") == 0) { out->kind = PLUGIN_UI_ACTION_REFRESH; return ETA_OK; }
    if (strncmp(event->node_id, "install_", 8) == 0 && strlen(event->node_id) == 8 + 9) {
        out->kind = PLUGIN_UI_ACTION_INSTALL_ONE;
        snprintf(out->plugin_id, sizeof(out->plugin_id), "%s", event->node_id + 8);
        return ETA_OK;
    }
    return ETA_E_NOT_FOUND;
}
