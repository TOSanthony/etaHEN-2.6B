#include "plugin_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plugin_config.h"
#include "plugin_lang.h"

#define CONTRIBUTION_ID "settings"

static eta_ui_node_desc_v1 make_node(uint32_t kind, const char *id,
                                     const char *parent_id, const char *title) {
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

static eta_status add_toggle(eta_ui_document *doc, const char *id, const char *key,
                             const char *title, const char *desc, int value) {
    eta_ui_node_desc_v1 node = make_node(ETA_UI_NODE_TOGGLE, id, "main", title);
    node.value_type = ETA_UI_VALUE_BOOL;
    node.binding = ETA_UI_BINDING_EVENT;
    snprintf(node.binding_key, sizeof(node.binding_key), "%s", key);
    snprintf(node.value, sizeof(node.value), "%s", value ? "true" : "false");
    snprintf(node.description, sizeof(node.description), "%s", desc);
    return eta_ui_document_add_node(doc, &node);
}

static eta_status add_nodes(eta_ui_document *doc, const plugin_settings *s) {
    eta_ui_node_desc_v1 node = make_node(ETA_UI_NODE_PAGE, "main", NULL, "Dashboard");
    eta_status st = eta_ui_document_add_node(doc, &node);
    if (st != ETA_OK) return st;

    node = make_node(ETA_UI_NODE_LABEL, "about", "main", "A propos");
    snprintf(node.description, sizeof(node.description), "%s",
             TR("Affiche ou masque les tuiles de l'accueil (modifie app.db). \"Appliquer maintenant\" redemarre aussi le shell pour charger le Welcome Hub.",
        "Shows or hides the home screen tiles (edits app.db). \"Apply now\" also restarts the shell to load the Welcome Hub."));
    st = eta_ui_document_add_node(doc, &node);
    if (st != ETA_OK) return st;

    st = add_toggle(doc, "bridge", "bridge_changed", TR("Welcome Hub : connexion forcee + widgets", "Welcome Hub: forced sign-in + widgets"),
                    TR("Fait croire au Welcome Hub que le compte est connecte et charge les widgets "
                       "(/data/wh-widgets). Remplace wh_bridge.elf, relance a chaque demarrage. "
                       "Desactiver prend effet au prochain demarrage.",
                       "Makes the Welcome Hub believe the account is signed in and loads the widgets "
                       "(/data/wh-widgets). Replaces wh_bridge.elf, relaunched at every start. "
                       "Disabling takes effect at the next start."), s->bridge);
    if (st != ETA_OK) return st;
    st = add_toggle(doc, "restart", "restart_changed", TR("Redemarrer le shell apres injection", "Restart the shell after injection"),
                    TR("Une fois le Welcome Hub injecte, redemarre SceShellUI une seule fois puis "
                       "reinjecte (evite de changer de profil). Experimental. Prend effet au prochain "
                       "demarrage du plugin.",
                       "Once the Welcome Hub is injected, restarts SceShellUI once then injects again "
                       "(avoids switching profile). Experimental. Takes effect at the next plugin start."), s->restart_shell);
    if (st != ETA_OK) return st;
    node = make_node(ETA_UI_NODE_INPUT, "restart_delay", "main", TR("Delai avant redemarrage du shell (s)", "Delay before shell restart (s)"));
    node.value_type = ETA_UI_VALUE_INT;
    node.binding = ETA_UI_BINDING_EVENT;
    node.min_value = 5;
    node.max_value = 300;
    node.min_length = 1;
    node.max_length = 3;
    snprintf(node.binding_key, sizeof(node.binding_key), "restart_delay_changed");
    snprintf(node.value, sizeof(node.value), "%d", s->restart_delay);
    snprintf(node.description, sizeof(node.description), "%s",
             TR("Secondes d'attente apres l'injection avant de redemarrer le shell (5 a 300).", "Seconds to wait after injection before restarting the shell (5 to 300)."));
    st = eta_ui_document_add_node(doc, &node);
    if (st != ETA_OK) return st;
    st = add_toggle(doc, "store", "store_changed", "PlayStation Store",
                    TR("Afficher la tuile PlayStation Store sur l'accueil.", "Show the PlayStation Store tile on the home screen."), s->store);
    if (st != ETA_OK) return st;
    st = add_toggle(doc, "plus", "plus_changed", "PlayStation Plus",
                    TR("Afficher la tuile PlayStation Plus sur l'accueil.", "Show the PlayStation Plus tile on the home screen."), s->plus);
    if (st != ETA_OK) return st;
    st = add_toggle(doc, "hub", "hub_changed", "Welcome Hub",
                    TR("Afficher la tuile Welcome Hub sur l'accueil.", "Show the Welcome Hub tile on the home screen."), s->hub);
    if (st != ETA_OK) return st;
    st = add_toggle(doc, "keep", "keep_changed", TR("Forcer en continu", "Keep enforcing"),
                    TR("Reapplique ces choix toutes les 5 s (le Welcome Hub reste active et visible, "
                       "le systeme ne peut pas le remasquer).",
                       "Re-applies these choices every 5 s (the Welcome Hub stays enabled and visible, "
                       "the system cannot hide it again)."), s->keep);
    if (st != ETA_OK) return st;
    st = add_toggle(doc, "notify", "notify_changed", "Notifications",
                    TR("Afficher une notification quand une tuile change.", "Show a notification when a tile changes."), s->notify);
    if (st != ETA_OK) return st;

    node = make_node(ETA_UI_NODE_ACTION, "apply_now", "main", TR("Appliquer maintenant", "Apply now"));
    node.binding = ETA_UI_BINDING_EVENT;
    snprintf(node.binding_key, sizeof(node.binding_key), "apply_now_requested");
    snprintf(node.description, sizeof(node.description), "%s",
             TR("Ecrit immediatement les reglages ci-dessus dans app.db.", "Writes the settings above to app.db immediately."));
    return eta_ui_document_add_node(doc, &node);
}

eta_status plugin_ui_create(const plugin_settings *settings, eta_ui_document **out_document) {
    if (!settings || !out_document) return ETA_E_INVALID_ARGUMENT;

    eta_ui_document_desc_v1 d;
    memset(&d, 0, sizeof(d));
    d.struct_size = sizeof(d);
    d.abi_version = ETA_UI_ABI_VERSION;
    d.priority = 100;
    snprintf(d.plugin_id, sizeof(d.plugin_id), "%s", PLUGIN_ID);
    snprintf(d.contribution_id, sizeof(d.contribution_id), "%s", CONTRIBUTION_ID);
    snprintf(d.title, sizeof(d.title), "%s", "Dashboard");
    snprintf(d.description, sizeof(d.description), "%s",
             TR("Tuiles de l'accueil : PlayStation Store, PlayStation Plus, Welcome Hub.", "Home screen tiles: PlayStation Store, PlayStation Plus, Welcome Hub."));
    snprintf(d.root_page_id, sizeof(d.root_page_id), "main");

    eta_status st = eta_ui_document_create(&d, out_document);
    if (st != ETA_OK) return st;
    st = add_nodes(*out_document, settings);
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

    if (strcmp(event->node_id, "apply_now") == 0) {
        out->kind = PLUGIN_UI_ACTION_APPLY_NOW;
        return ETA_OK;
    }
    if (strcmp(event->node_id, "restart_delay") == 0) {
        if (event->value_type != ETA_UI_VALUE_INT) return ETA_E_INVALID_ARGUMENT;
        char *end = NULL;
        long v = strtol(event->value, &end, 10);
        if (end == event->value || *end != '\0' || v < 5 || v > 300) return ETA_E_INVALID_ARGUMENT;
        out->kind = PLUGIN_UI_ACTION_SET_RESTART_DELAY;
        out->value = (int)v;
        return ETA_OK;
    }
    static const struct { const char *id; plugin_ui_action_kind kind; } map[] = {
        {"store", PLUGIN_UI_ACTION_SET_STORE}, {"plus", PLUGIN_UI_ACTION_SET_PLUS},
        {"hub", PLUGIN_UI_ACTION_SET_HUB},     {"keep", PLUGIN_UI_ACTION_SET_KEEP},
        {"notify", PLUGIN_UI_ACTION_SET_NOTIFY}, {"bridge", PLUGIN_UI_ACTION_SET_BRIDGE}, {"restart", PLUGIN_UI_ACTION_SET_RESTART},
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(event->node_id, map[i].id) == 0) {
            if (event->value_type != ETA_UI_VALUE_BOOL) return ETA_E_INVALID_ARGUMENT;
            out->kind = map[i].kind;
            out->value = (strcmp(event->value, "true") == 0 || strcmp(event->value, "1") == 0);
            return ETA_OK;
        }
    }
    return ETA_E_NOT_FOUND;
}
