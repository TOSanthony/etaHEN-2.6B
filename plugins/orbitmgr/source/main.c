/* Orbit Store Manager — main lifecycle.
 *
 * Controls TOSanthony/orbit-store's backend service (orbit_store.elf) from
 * the etaHEN menu: start/stop it, optionally auto-start it when this plugin
 * loads, and copy a downloaded PPSA99177.ffpkg into /data/homebrew so
 * ShadowMountPlus registers it as a real title.
 */

#include <eta/client.h>
#include <eta/plugin.h>
#include <eta/status.h>
#include <eta/transport.h>
#include <eta/ui.h>

#include <ps5/kernel.h>

#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "orbit_ffpkg.h"
#include "orbit_service.h"
#include "plugin_config.h"
#include "plugin_lang.h"
#include "plugin_settings.h"
#include "plugin_ui.h"

#define CONNECT_ATTEMPTS 30
#define CONNECT_RETRY_US (250 * 1000)
#define EVENT_POLL_US (100 * 1000)
#define STATUS_POLL_TICKS 20 /* ~2s (20 * 100ms), same cadence as ftpsrv's i18n refresh */

/* Same authid the FTP/MediaGallery plugins use to reach /data freely. */
#define PLUGIN_AUTH_ID UINT64_C(0x4801000000000013)

extern const eta_plugin_descriptor_v1 eta_plugin_descriptor;

typedef struct plugin_app {
    eta_transport transport;
    eta_socket_transport socket;
    eta_client client;
    eta_host_services_v1 services;
    eta_ui_document *document;
    eta_ui_handle ui_handle;
    plugin_ui_state ui_state;
    plugin_settings settings;
    FILE *log_file;
    int transport_connected;
    int client_initialized;
    int last_running;
    /* Set by apply_action()/the status poll; the loop rebuilds the document
     * at the top of the next iteration (never inside the event callback). */
    int ui_dirty;
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

    if (!app->log_file) return;
    va_start(arguments, format);
    vfprintf(app->log_file, format, arguments);
    va_end(arguments);
    fflush(app->log_file);
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

/* Recomputes the status line shown on the plugin page. */
static void update_status(plugin_app *app, const char *hint) {
    char status[PLUGIN_UI_STATUS_MAX];
    pid_t pid = orbit_service_pid();
    app->last_running = pid > 0;
    if (pid > 0) {
        snprintf(status, sizeof(status), TR("En cours d'execution (pid %d)",
                                            "Running (pid %d)"), (int)pid);
    } else {
        snprintf(status, sizeof(status), TR("Arrete", "Stopped"));
    }
    plugin_ui_state_set_status(&app->ui_state, status, hint);
}

/* The SDK has no in-place update for LABEL text: tear the document down and
 * register a fresh one (same approach as the mediagallery plugin). */
static void rebuild_ui(plugin_app *app) {
    if (app->ui_handle != 0) {
        (void)eta_ui_unregister(&app->services, app->ui_handle);
        app->ui_handle = 0;
    }
    if (app->document) {
        eta_ui_document_destroy(app->document);
        app->document = NULL;
    }
    if (plugin_ui_create(&app->ui_state, &app->document) != ETA_OK) {
        app->document = NULL;
        return;
    }
    if (eta_ui_register(&app->services, app->document, &app->ui_handle) !=
        ETA_OK) {
        eta_ui_document_destroy(app->document);
        app->document = NULL;
        app->ui_handle = 0;
    }
}

static eta_status start_plugin(plugin_app *app) {
    const int elevated = kernel_set_ucred_authid(getpid(), PLUGIN_AUTH_ID) == 0;

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

    if (app->settings.autostart) {
        pid_t pid = orbit_service_start(app->settings.elf_path);
        log_message(app, "[%s] autostart %s (elevated=%d, pid=%d)\n",
                    PLUGIN_ID, pid > 0 ? "ok" : "failed", elevated, (int)pid);
    }

    update_status(app, "");
    status = plugin_ui_create(&app->ui_state, &app->document);
    if (status == ETA_OK) {
        status = eta_ui_register(
            &app->services, app->document, &app->ui_handle);
    }
    return status;
}

static void stop_plugin(plugin_app *app) {
    if (app->ui_handle != 0) {
        (void)eta_ui_unregister(&app->services, app->ui_handle);
        app->ui_handle = 0;
    }
    eta_ui_document_destroy(app->document);
    app->document = NULL;
    if (app->client_initialized) {
        eta_client_deinit(&app->client);
        app->client_initialized = 0;
    }
    if (app->transport_connected) {
        eta_socket_transport_deinit(&app->transport);
        app->transport_connected = 0;
    }
}

static eta_status apply_action(plugin_app *app,
                               const plugin_ui_action *action) {
    switch (action->kind) {
    case PLUGIN_UI_ACTION_START: {
        pid_t pid = orbit_service_start(app->settings.elf_path);
        update_status(app, pid > 0
                               ? TR("Demarre.", "Started.")
                               : TR("Echec du demarrage (ELF absent ou "
                                    "elfldr indisponible).",
                                    "Failed to start (ELF missing or elfldr "
                                    "unavailable)."));
        app->ui_dirty = 1;
        return pid > 0 ? ETA_OK : ETA_E_IO;
    }

    case PLUGIN_UI_ACTION_STOP: {
        bool stopped = orbit_service_stop();
        update_status(app, stopped ? TR("Arrete.", "Stopped.")
                                    : TR("Echec de l'arret.", "Failed to stop."));
        app->ui_dirty = 1;
        return stopped ? ETA_OK : ETA_E_IO;
    }

    case PLUGIN_UI_ACTION_SET_AUTOSTART:
        if (action->autostart == app->settings.autostart) return ETA_E_NOT_FOUND;
        app->settings.autostart = action->autostart;
        save_settings(app);
        app->ui_dirty = 1;
        return ETA_OK;

    case PLUGIN_UI_ACTION_SET_SEARCH_PATH:
        if (strncmp(action->search_path, app->settings.search_path,
                    sizeof(app->settings.search_path)) == 0) {
            return ETA_E_NOT_FOUND;
        }
        snprintf(app->settings.search_path, sizeof(app->settings.search_path),
                 "%s", action->search_path);
        save_settings(app);
        app->ui_dirty = 1;
        return ETA_OK;

    case PLUGIN_UI_ACTION_INSTALL_FFPKG: {
        char installed_name[256] = {0};
        orbit_ffpkg_result result = orbit_ffpkg_install_from(
            app->settings.search_path, installed_name, sizeof(installed_name));
        char hint[PLUGIN_UI_STATUS_MAX];
        switch (result) {
        case ORBIT_FFPKG_OK:
            snprintf(hint, sizeof(hint),
                     TR("%s copie vers /data/homebrew.",
                        "%s copied to /data/homebrew."), installed_name);
            break;
        case ORBIT_FFPKG_NOT_FOUND:
            snprintf(hint, sizeof(hint), "%s",
                     TR("Aucun .ffpkg trouve dans ce dossier.",
                        "No .ffpkg found in that folder."));
            break;
        case ORBIT_FFPKG_BAD_PATH:
            snprintf(hint, sizeof(hint), "%s",
                     TR("Chemin de recherche invalide.",
                        "Invalid search path."));
            break;
        default:
            snprintf(hint, sizeof(hint), "%s",
                     TR("Echec de la copie.", "Copy failed."));
            break;
        }
        update_status(app, hint);
        app->ui_dirty = 1;
        return result == ORBIT_FFPKG_OK ? ETA_OK : ETA_E_IO;
    }

    case PLUGIN_UI_ACTION_NONE:
        break;
    }
    return ETA_E_NOT_FOUND;
}

static int run_event_loop(plugin_app *app) {
    unsigned status_poll_ticks = 0;
    while (running) {
        if (app->ui_dirty) {
            app->ui_dirty = 0;
            rebuild_ui(app);
        }

        if (++status_poll_ticks >= STATUS_POLL_TICKS) {
            status_poll_ticks = 0;
            int was_running = app->last_running;
            update_status(app, app->ui_state.hint);
            if (app->last_running != was_running) app->ui_dirty = 1;
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
        if (status == ETA_OK) status = apply_action(app, &action);
        if (status != ETA_OK && status != ETA_E_NOT_FOUND) {
            log_message(app, "[%s] rejected action on %s: %s\n", PLUGIN_ID,
                        event.node_id, eta_status_string(status));
        }
    }
    return 0;
}

int main(void) {
    plugin_app app = {0};
    plugin_settings_defaults(&app.settings);
    if (plugin_settings_load(PLUGIN_SETTINGS_PATH, &app.settings) != 0) {
        fprintf(stderr, "[%s] failed to read %s; using defaults\n", PLUGIN_ID,
                PLUGIN_SETTINGS_PATH);
    }
    plugin_ui_state_init(&app.ui_state, &app.settings);
    app.log_file = fopen(PLUGIN_LOG_PATH, "a");

    signal(SIGINT, request_stop);
    signal(SIGTERM, request_stop);
    signal(SIGPIPE, SIG_IGN);
    (void)syscall(SYS_thr_set_name, -1, PLUGIN_ID ".elf");
    log_message(&app, "[%s] starting %s %s\n", PLUGIN_ID, PLUGIN_NAME,
                PLUGIN_VERSION);

    const eta_status status = start_plugin(&app);
    int exit_code = 1;
    if (status == ETA_OK) {
        log_message(&app, "[%s] ready; autostart=%d handle=%llu\n", PLUGIN_ID,
                    app.settings.autostart, (unsigned long long)app.ui_handle);
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
