#include <eta/client.h>
#include <eta/plugin.h>
#include <eta/status.h>
#include <eta/transport.h>
#include <eta/ui.h>

#include <ps5/kernel.h>

#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "downloader.h"
#include "plugin_config.h"
#include "plugin_settings.h"
#include "plugin_ui.h"
#include "progress_file.h"
#include "plugin_lang.h"

#define CONNECT_ATTEMPTS 30
#define CONNECT_RETRY_US (250 * 1000)
#define EVENT_POLL_US (100 * 1000)
#define PLUGIN_AUTH_ID UINT64_C(0x4801000000000013)

extern const eta_plugin_descriptor_v1 eta_plugin_descriptor;

typedef struct NotifyRequest {
    char useless[45];
    char message[3075];
} NotifyRequest;

int sceKernelSendNotificationRequest(int device, NotifyRequest *request, size_t size, int unused);

typedef struct plugin_app {
    eta_transport transport;
    eta_socket_transport socket;
    eta_client client;
    eta_host_services_v1 services;
    eta_ui_document *document;
    eta_ui_handle ui_handle;
    plugin_settings settings;
    plugin_list list;
    char status[96];
    FILE *log_file;
    int transport_connected;
    int client_initialized;
} plugin_app;

static volatile sig_atomic_t running = 1;

static void request_stop(int signal_number) {
    (void)signal_number;
    running = 0;
}

static void log_message(plugin_app *app, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vprintf(format, arguments);
    va_end(arguments);
    fflush(stdout);

    if (!app || !app->log_file) return;
    va_start(arguments, format);
    vfprintf(app->log_file, format, arguments);
    va_end(arguments);
    fflush(app->log_file);
}

static void send_notification(plugin_app *app, const char *format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    log_message(app, "[%s] notification: %s\n", PLUGIN_ID, buffer);

    if (app && app->client_initialized &&
        eta_services_validate(&app->services) == ETA_OK &&
        app->services.notify) {
        if (eta_service_notify(&app->services, buffer) == ETA_OK) {
            return;
        }
    }

    NotifyRequest req;
    memset(&req, 0, sizeof(req));
    snprintf(req.message, sizeof(req.message), "%s", buffer);
    (void)sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

static const char *result_text(dl_result r) {
    switch (r) {
    case DL_OK: return "OK";
    case DL_ERR_NETWORK: return TR("reseau", "network");
    case DL_ERR_HTTP: return TR("introuvable", "not found");
    case DL_ERR_INVALID: return TR("fichier invalide", "invalid file");
    case DL_ERR_IO: return TR("ecriture", "write error");
    }
    return "?";
}

/* Built-in list used until the release has been read. */
static void fallback_list(plugin_list *list) {
    static const char *const ids[] = {"DPIV00001", "FTPS00001", "NTPS00001", "DASH00001", "MGAL00001", "NEOP00001"};
    memset(list, 0, sizeof(*list));
    for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
        snprintf(list->ids[list->count++], DL_ID_LEN, "%s", ids[i]);
    list->from_release = 0;
}

static void set_status(plugin_app *app, const char *text) {
    snprintf(app->status, sizeof(app->status), "%s", text);
    log_message(app, "[%s] %s\n", PLUGIN_ID, text);
}

/* Overall progress of the current install session (one or several plugins). */
typedef struct progress_user {
    plugin_app *app;
    const char *id;
    unsigned index; /* 0-based position in the session */
    unsigned total;
} progress_user;

static void on_progress(void *user, unsigned pct, size_t got, size_t total) {
    (void)got;
    (void)total;
    progress_user *pu = (progress_user *)user;
    char phase[96];
    snprintf(phase, sizeof phase, TR("Telechargement %s (%u/%u)", "Downloading %s (%u/%u)"), plugin_label(pu->id), pu->index + 1, pu->total);
    progress_update((pu->index * 100u + pct) / (pu->total ? pu->total : 1u), phase);
}

static void rebuild_ui(plugin_app *app) {
    if (app->ui_handle != 0) {
        (void)eta_ui_unregister(&app->services, app->ui_handle);
        app->ui_handle = 0;
    }
    if (app->document) {
        eta_ui_document_destroy(app->document);
        app->document = NULL;
    }
    if (plugin_ui_create(&app->settings, &app->list, app->status, &app->document) == ETA_OK) {
        if (eta_ui_register(&app->services, app->document, &app->ui_handle) != ETA_OK)
            log_message(app, "[%s] UI register failed\n", PLUGIN_ID);
    } else {
        log_message(app, "[%s] UI create failed\n", PLUGIN_ID);
    }
}

/* Re-reads the release; rebuilds the page when the list changed (or force). */
static void refresh_list(plugin_app *app, int force) {
    plugin_list fresh;
    memset(&fresh, 0, sizeof(fresh));
    char detail[128] = "";
    fresh.count = downloader_list_plugins(app->settings.channel, fresh.ids, DL_MAX_PLUGINS,
                                          detail, sizeof detail);
    fresh.from_release = fresh.count > 0;
    if (!fresh.from_release) {
        log_message(app, "[%s] release list failed: %s\n", PLUGIN_ID, detail);
        if (app->list.from_release) return; /* keep the last good list */
        if (!force) return;
        fallback_list(&fresh);
        set_status(app, TR("Liste de secours (release injoignable)", "Fallback list (release unreachable)"));
    } else {
        int same = app->list.from_release && app->list.count == fresh.count &&
                   memcmp(app->list.ids, fresh.ids, sizeof(fresh.ids[0]) * fresh.count) == 0;
        if (same && !force) return;
        log_message(app, "[%s] release lists %u plugin(s)\n", PLUGIN_ID, fresh.count);
        snprintf(app->status, sizeof(app->status), "%u plugin(s) disponible(s)", fresh.count);
    }
    app->list = fresh;
    rebuild_ui(app);
}

/* Installs one plugin as step `index` of a `total`-step session (progress is
 * published for the ShellUI panel by on_progress). Returns 1 on success. */
static int install_one(plugin_app *app, const char *id, unsigned index, unsigned total, char *err,
                       size_t err_size) {
    char detail[160] = "";
    progress_user pu = {app, id, index, total};
    log_message(app, "[%s] downloading %s (channel=%s)\n", PLUGIN_ID, id,
                app->settings.channel ? "latest" : "nightly");
    char phase[96];
    snprintf(phase, sizeof phase, TR("Connexion %s (%u/%u)", "Connecting %s (%u/%u)"), plugin_label(id), index + 1, total);
    progress_update(index * 100u / (total ? total : 1u), phase);
    dl_result r = downloader_install_plugin(id, app->settings.channel, on_progress, &pu, detail,
                                            sizeof detail);
    if (r == DL_OK) {
        log_message(app, "[%s] %s installed\n", PLUGIN_ID, id);
        return 1;
    }
    log_message(app, "[%s] %s failed (%s): %s\n", PLUGIN_ID, id, result_text(r), detail);
    snprintf(err, err_size, "%s : echec (%s)", plugin_label(id), result_text(r));
    return 0;
}

/* One progress session over `ids` (the ShellUI shows a native progress panel). */
static void install_session(plugin_app *app, const char *const *ids, unsigned total) {
    if (total == 0) return;
    unsigned ok = 0;
    char err[96] = "";
    progress_begin("Plugin Manager", TR("Preparation...", "Preparing..."));
    for (unsigned i = 0; i < total; ++i) ok += (unsigned)install_one(app, ids[i], i, total, err, sizeof err);
    char text[128];
    if (ok == total) {
        if (total == 1) snprintf(text, sizeof text, TR("%s : installe", "%s: installed"), plugin_label(ids[0]));
        else snprintf(text, sizeof text, TR("%u plugins installes", "%u plugins installed"), total);
    } else if (total == 1) {
        snprintf(text, sizeof text, "%s", err);
    } else {
        snprintf(text, sizeof text, TR("%u / %u plugins installes", "%u / %u plugins installed"), ok, total);
    }
    progress_end(ok == total, text);
    set_status(app, text);
    rebuild_ui(app); /* refresh the "Statut" line */
    send_notification(app, "Plugin Manager\n%s%s", text, ok ? TR("\nRedemarre les plugins mis a jour.", "\nRestart the updated plugins.") : "");
}

static eta_status connect_to_daemon(plugin_app *app) {
    for (int attempt = 1; attempt <= CONNECT_ATTEMPTS && running; ++attempt) {
        eta_status status = eta_socket_transport_connect(
            &app->transport, &app->socket, ETA_PLUGIN_IPC_SOCKET_PATH);
        if (status == ETA_OK) {
            app->transport_connected = 1;
            return ETA_OK;
        }
        if (attempt < CONNECT_ATTEMPTS) usleep(CONNECT_RETRY_US);
    }
    return ETA_E_IO;
}

static void save_settings(plugin_app *app) {
    if (plugin_settings_save(PLUGIN_SETTINGS_PATH, &app->settings) != 0) {
        log_message(app, "[%s] failed to save %s\n", PLUGIN_ID,
                    PLUGIN_SETTINGS_PATH);
    }
}

static eta_status start_plugin(plugin_app *app) {
    if (kernel_set_ucred_authid(getpid(), PLUGIN_AUTH_ID) != 0) {
        log_message(app, "[%s] warning: kernel_set_ucred_authid failed\n", PLUGIN_ID);
    }
    if (kernel_set_ucred_uid(getpid(), 0) != 0) {
        log_message(app, "[%s] warning: kernel_set_ucred_uid failed\n", PLUGIN_ID);
    }

    eta_status status = connect_to_daemon(app);
    if (status != ETA_OK) return status;

    status = eta_client_init(&app->client, &app->transport);
    if (status != ETA_OK) return status;
    app->client_initialized = 1;

    status = eta_client_open_session(&app->client, &eta_plugin_descriptor);
    if (status == ETA_OK) {
        status = eta_client_make_services(&app->client, &app->services);
    }
    if (status != ETA_OK) return status;

    status = plugin_ui_create(&app->settings, &app->list, app->status, &app->document);
    if (status == ETA_OK) {
        status = eta_ui_register(&app->services, app->document, &app->ui_handle);
    }
    return status;
}

static void stop_plugin(plugin_app *app) {
    if (app->ui_handle != 0) {
        (void)eta_ui_unregister(&app->services, app->ui_handle);
        app->ui_handle = 0;
    }
    if (app->document) {
        eta_ui_document_destroy(app->document);
        app->document = NULL;
    }
    if (app->client_initialized) {
        eta_client_deinit(&app->client);
        app->client_initialized = 0;
    }
    if (app->transport_connected) {
        eta_socket_transport_deinit(&app->transport);
        app->transport_connected = 0;
    }
}

static eta_status apply_action(plugin_app *app, const plugin_ui_action *action) {
    switch (action->kind) {
    case PLUGIN_UI_ACTION_SET_CHANNEL:
        app->settings.channel = action->channel;
        save_settings(app);
        app->list.from_release = 0;
        refresh_list(app, 1);
        return ETA_OK;
    case PLUGIN_UI_ACTION_REFRESH:
        refresh_list(app, 1);
        return ETA_OK;
    case PLUGIN_UI_ACTION_INSTALL_ONE:
        for (unsigned i = 0; i < app->list.count; ++i) {
            if (strcmp(app->list.ids[i], action->plugin_id) == 0) {
                const char *one[1] = {app->list.ids[i]};
                install_session(app, one, 1);
            }
        }
        return ETA_OK;
    case PLUGIN_UI_ACTION_RESTORE:
        for (unsigned i = 0; i < app->list.count; ++i) downloader_forget_plugin(app->list.ids[i]);
        send_notification(app, TR("Plugin Manager\nVersions integrees restaurees au prochain demarrage d'etaHEN.", "Plugin Manager\nBuilt-in versions restored at the next etaHEN start."));
        return ETA_OK;
    case PLUGIN_UI_ACTION_NONE:
        return ETA_E_NOT_FOUND;
    }
    return ETA_E_NOT_FOUND;
}

static int run_event_loop(plugin_app *app) {
    unsigned refresh_ticks = 30; /* first release read ~3 s after start, then every ~10 min */
    while (running) {
        if (refresh_ticks > 0 && --refresh_ticks == 0) {
            refresh_ticks = 6000;
            refresh_list(app, !app->list.from_release && app->list.count == 0);
        }
        eta_ui_event_v1 event;
        eta_status status = eta_client_poll_ui_event(&app->client, &event);
        if (status == ETA_E_NOT_FOUND) {
            usleep(EVENT_POLL_US);
            continue;
        }
        if (status != ETA_OK) {
            log_message(app, "[%s] UI event poll failed: %s\n", PLUGIN_ID,
                        eta_status_string(status));
            return 1;
        }

        plugin_ui_action action;
        status = plugin_ui_decode_action(app->ui_handle, &event, &action);
        if (status == ETA_OK) {
            status = apply_action(app, &action);
        }
        if (status != ETA_OK && status != ETA_E_NOT_FOUND) {
            log_message(app, "[%s] action %s failed: %s\n", PLUGIN_ID,
                        event.node_id, eta_status_string(status));
        }
    }
    return 0;
}

int main(void) {
    plugin_app app;
    memset(&app, 0, sizeof(app));

    plugin_settings_defaults(&app.settings);
    fallback_list(&app.list);
    snprintf(app.status, sizeof(app.status), "");
    if (plugin_settings_load(PLUGIN_SETTINGS_PATH, &app.settings) != 0) {
        fprintf(stderr, "[%s] failed to read %s; using defaults\n", PLUGIN_ID,
                PLUGIN_SETTINGS_PATH);
    }
    app.log_file = fopen(PLUGIN_LOG_PATH, "a");

    signal(SIGINT, request_stop);
    signal(SIGTERM, request_stop);
    signal(SIGPIPE, SIG_IGN);
    (void)syscall(SYS_thr_set_name, -1, "updater.elf");

    log_message(&app, "[%s] starting %s %s\n", PLUGIN_ID, PLUGIN_NAME, PLUGIN_VERSION);

    const eta_status status = start_plugin(&app);
    int exit_code = 1;
    if (status == ETA_OK) {
        log_message(&app, "[%s] ready; channel=%s handle=%llu\n", PLUGIN_ID,
                    app.settings.channel ? "latest" : "nightly", (unsigned long long)app.ui_handle);
        exit_code = run_event_loop(&app);
    } else {
        log_message(&app, "[%s] startup failed: %s\n", PLUGIN_ID,
                    eta_status_string(status));
    }

    stop_plugin(&app);
    log_message(&app, "[%s] stopped with exit_code=%d\n", PLUGIN_ID, exit_code);
    if (app.log_file) fclose(app.log_file);
    return exit_code;
}
