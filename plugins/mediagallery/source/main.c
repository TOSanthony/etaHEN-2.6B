/* Media gallery plugin — main lifecycle.
 *
 * The plugin is an HTTP service. Its ShellUI contribution is a *configuration
 * page only*: ShellUI has no node kind capable of rendering a media list, so
 * browsing happens in an external client against the HTTP API.
 *
 * Start-up order matters: credentials are elevated before anything touches
 * /user, and the listener only starts after the first index exists, so a client
 * that connects immediately never sees a half-built library.
 */

#include <eta/client.h>
#include <eta/plugin.h>
#include <eta/status.h>
#include <eta/transport.h>
#include <eta/ui.h>

#include <ps5/kernel.h>

#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "http_service.h"
#include "media_index.h"
#include "plugin_config.h"
#include "plugin_settings.h"
#include "plugin_ui.h"
#include "plugin_lang.h"

#define CONNECT_ATTEMPTS 30
#define CONNECT_RETRY_US (250 * 1000)
#define EVENT_POLL_US (100 * 1000)

/* Required to read /user/av_contents. Same authid the FTPSRV plugin uses. */
#define PLUGIN_AUTH_ID UINT64_C(0x4801000000000013)

extern const eta_plugin_descriptor_v1 eta_plugin_descriptor;

typedef struct plugin_app {
    eta_transport         transport;
    eta_socket_transport  socket;
    eta_client            client;
    eta_host_services_v1  services;
    eta_ui_document      *document;
    eta_ui_handle         ui_handle;
    plugin_ui_state         ui_state;
    plugin_settings         settings;
    media_store             store;
    http_service            http;
    FILE                   *log_file;
    int                     transport_connected;
    int                     client_initialized;
    int                     store_initialized;
    int                     http_initialized;
    int                     last_scan_ok;
    /* Set by apply_action(); the loop rebuilds the document at the top of the
     * next iteration. Rebuilding inside the event callback would unregister a
     * handle the daemon is still dispatching against. */
    int                     ui_dirty;
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

/* Recomputes the status line shown on the configuration page. */
static void update_status(plugin_app *app) {
    char status[PLUGIN_UI_STATUS_MAX];
    char hint[PLUGIN_UI_STATUS_MAX];

    media_index *index = media_store_acquire(&app->store);
    const unsigned long photos = index ? (unsigned long)index->photo_count : 0UL;
    const unsigned long videos = index ? (unsigned long)index->video_count : 0UL;
    const int truncated = index ? (index->truncated != 0) : 0;
    media_store_release(&app->store);

    if (!app->last_scan_ok) {
        snprintf(status, sizeof(status), TR("Echec du scan", "Scan failed"));
    } else {
        snprintf(status, sizeof(status), TR("%lu photos / %lu videos%s", "%lu photos / %lu videos%s"), photos,
                 videos, truncated ? TR(" (tronque)", " (truncated)") : "");
    }

    if (!app->store.privileged) {
        /* The library lives under /user, which is unreadable without the
         * elevated authid. Say so here rather than letting the client see an
         * empty list and guess. */
        snprintf(hint, sizeof(hint), TR("Non eleve ; /user peut etre illisible", "Not elevated; /user may be unreadable"));
    } else if (http_service_running(&app->http)) {
        snprintf(hint, sizeof(hint), TR("Actif sur :%u%s", "Serving on :%u%s"),
                 (unsigned)http_service_port(&app->http),
                 app->settings.local_only ? TR(" (local uniquement)", " (loopback)") : "");
    } else if (app->settings.enabled) {
        snprintf(hint, sizeof(hint), TR("Ecoute arretee ; port %u", "Listener down; port %u"),
                 (unsigned)app->settings.port);
    } else {
        snprintf(hint, sizeof(hint), TR("Service desactive", "Service disabled"));
    }

    plugin_ui_state_set_status(&app->ui_state, status, hint);
}

/* The SDK has no in-place update for LABEL text, so any change to the status
 * line means tearing the document down and registering a fresh one. The node
 * count here is a handful, so this is cheap. */
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
    /* Best effort. If elevation fails the plugin still starts, still serves
     * /api/v1/health, and still exposes its configuration page — the failure is
     * reported through the health payload and the status hint instead of
     * turning into an opaque "plugin exited immediately". */
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

    media_store_init(&app->store, PLUGIN_MEDIA_ROOT);
    app->store_initialized = 1;
    app->store.privileged = elevated;
    app->last_scan_ok = media_store_rescan(&app->store) == 0;
    log_message(app, "[%s] indexed %s (privileged=%d)\n", PLUGIN_ID,
                app->last_scan_ok ? "ok" : "failed", elevated);

    /* Leave the plugin sandbox the way the DPI pkg-server does: root ids, the
     * real root/jail directory and full caps. A sandboxed process can bind a
     * port (listen succeeds) yet never receive LAN connections. */
    {
        const pid_t me = getpid();
        int rc = 0;
        rc |= kernel_set_ucred_uid(me, 0);
        rc |= kernel_set_ucred_ruid(me, 0);
        rc |= kernel_set_ucred_svuid(me, 0);
        rc |= kernel_set_ucred_rgid(me, 0);
        rc |= kernel_set_ucred_svgid(me, 0);
        const intptr_t rootvnode = kernel_get_root_vnode();
        if (rootvnode) {
            rc |= kernel_set_proc_rootdir(me, rootvnode);
            rc |= kernel_set_proc_jaildir(me, rootvnode);
        }
        uint8_t caps[16];
        memset(caps, 0xff, sizeof(caps));
        rc |= kernel_set_ucred_caps(me, caps);
        log_message(app, "[%s] sandbox escape %s\n", PLUGIN_ID, rc == 0 ? "ok" : "partial/failed");
    }

    if (!http_service_init(&app->http, &app->store, app->settings.port,
                           app->settings.local_only)) {
        return ETA_E_IO;
    }
    app->http_initialized = 1;
    if (app->settings.enabled && !http_service_start(&app->http)) {
        /* Do not leave the toggle claiming the service is up when it is not. */
        app->settings.enabled = 0;
        save_settings(app);
        log_message(app, "[%s] failed to listen on TCP %u\n", PLUGIN_ID,
                    (unsigned)app->settings.port);
    }

    update_status(app);
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

    /* Listener first: it borrows the store, so it must be gone before the
     * store is torn down. */
    if (app->http_initialized) {
        http_service_destroy(&app->http);
        app->http_initialized = 0;
    }
    if (app->store_initialized) {
        media_store_destroy(&app->store);
        app->store_initialized = 0;
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

static eta_status apply_action(plugin_app *app,
                                 const plugin_ui_action *action) {
    switch (action->kind) {
    case PLUGIN_UI_ACTION_SET_ENABLED:
        if (action->enabled == app->settings.enabled) return ETA_E_NOT_FOUND;
        if (action->enabled) {
            if (!http_service_start(&app->http)) {
                /* settings.enabled is untouched, so the rebuilt page shows the
                 * toggle still off. */
                app->ui_dirty = 1;
                return ETA_E_IO;
            }
        } else {
            http_service_stop(&app->http);
        }
        app->settings.enabled = action->enabled;
        save_settings(app);
        update_status(app);
        app->ui_dirty = 1;
        return ETA_OK;

    case PLUGIN_UI_ACTION_SET_PORT: {
        const uint16_t previous_port = app->settings.port;
        if (action->port == previous_port) return ETA_E_NOT_FOUND;
        if (!http_service_reconfigure(&app->http, action->port,
                                      app->settings.local_only)) {
            app->ui_dirty = 1;
            return ETA_E_IO;
        }
        app->settings.port = action->port;
        save_settings(app);
        update_status(app);
        app->ui_dirty = 1;
        return ETA_OK;
    }

    case PLUGIN_UI_ACTION_SET_LOCAL_ONLY: {
        if (action->local_only == app->settings.local_only) {
            return ETA_E_NOT_FOUND;
        }
        if (!http_service_reconfigure(&app->http, app->settings.port,
                                      action->local_only)) {
            app->ui_dirty = 1;
            return ETA_E_IO;
        }
        app->settings.local_only = action->local_only;
        save_settings(app);
        update_status(app);
        app->ui_dirty = 1;
        return ETA_OK;
    }

    case PLUGIN_UI_ACTION_RESCAN:
        app->last_scan_ok = media_store_rescan(&app->store) == 0;
        log_message(app, "[%s] rescan %s\n", PLUGIN_ID,
                    app->last_scan_ok ? "ok" : "failed");
        update_status(app);
        app->ui_dirty = 1;
        return app->last_scan_ok ? ETA_OK : ETA_E_IO;

    case PLUGIN_UI_ACTION_NONE:
        break;
    }
    return ETA_E_NOT_FOUND;
}

static int run_event_loop(plugin_app *app) {
    while (running) {
        if (app->ui_dirty) {
            app->ui_dirty = 0;
            rebuild_ui(app);
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
    /* Clients abandon range requests mid-stream; without this the first
     * disconnect would take the whole plugin down. */
    signal(SIGPIPE, SIG_IGN);
    (void)syscall(SYS_thr_set_name, -1, PLUGIN_ID ".elf");
    log_message(&app, "[%s] starting %s %s\n", PLUGIN_ID, PLUGIN_NAME,
                PLUGIN_VERSION);

    const eta_status status = start_plugin(&app);
    int exit_code = 1;
    if (status == ETA_OK) {
        log_message(&app, "[%s] ready; enabled=%d port=%u handle=%llu\n",
                    PLUGIN_ID, app.settings.enabled,
                    (unsigned)app.settings.port,
                    (unsigned long long)app.ui_handle);
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
