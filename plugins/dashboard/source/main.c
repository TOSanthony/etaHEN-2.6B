#include <eta/client.h>
#include <eta/plugin.h>
#include <eta/status.h>
#include <eta/transport.h>
#include <eta/ui.h>

#include <ps5/kernel.h>

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "dash_appdb.h"
#include "wh_runner.h"
#include "plugin_config.h"
#include "plugin_settings.h"
#include "plugin_ui.h"
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

static plugin_app *g_app;

static void appdb_log(const char *msg) {
    if (g_app) log_message(g_app, "[%s] %s\n", PLUGIN_ID, msg);
}

static const struct { const char *id; const char *label; } kTiles[] = {
    {"NPXS40047", "PlayStation Store"},
    {"NPXS40037", "PlayStation Plus"},
    {"NPXS40063", "Welcome Hub"},
};

/* Writes the three tile visibilities into app.db. announce=1 logs/notifies
 * the rows that actually changed. */
static void apply_tiles(plugin_app *app, int announce) {
    const int want[3] = {app->settings.store, app->settings.plus, app->settings.hub};
    for (int i = 0; i < 3; ++i) {
        int changed = 0;
        int rc = dash_appdb_set_tile(kTiles[i].id, want[i], &changed);
        if (rc < 0) {
            if (announce) log_message(app, "[%s] %s: update failed\n", PLUGIN_ID, kTiles[i].label);
        } else if (rc > 0) {
            if (announce) log_message(app, "[%s] %s: not found in app.db\n", PLUGIN_ID, kTiles[i].label);
        } else if (changed > 0) {
            log_message(app, "[%s] %s %s (%d row(s))\n", PLUGIN_ID, kTiles[i].label,
                        want[i] ? "shown" : "hidden", changed);
            if (app->settings.notify)
                send_notification(app, "Dashboard\n%s : %s", kTiles[i].label,
                                  want[i] ? TR("affichee", "shown") : TR("masquee", "hidden"));
        }
    }
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

    status = plugin_ui_create(&app->settings, &app->document);
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
    case PLUGIN_UI_ACTION_APPLY_NOW:
        log_message(app, "[%s] manual apply requested\n", PLUGIN_ID);
        apply_tiles(app, 1);
        if (app->settings.bridge) {
            /* The Welcome Hub only loads into a freshly started shell. */
            int rc = wh_runner_is_started()
                         ? wh_runner_restart_now(app->settings.restart_delay)
                         : wh_runner_start(1, app->settings.restart_delay); /* inject, then restart */
            log_message(app, "[%s] shell restart %s\n", PLUGIN_ID, rc == 0 ? "requested" : "failed");
        }
        return ETA_OK;
    case PLUGIN_UI_ACTION_SET_STORE: app->settings.store = action->value; break;
    case PLUGIN_UI_ACTION_SET_PLUS: app->settings.plus = action->value; break;
    case PLUGIN_UI_ACTION_SET_HUB: app->settings.hub = action->value; break;
    case PLUGIN_UI_ACTION_SET_KEEP: app->settings.keep = action->value; break;
    case PLUGIN_UI_ACTION_SET_NOTIFY: app->settings.notify = action->value; break;
    case PLUGIN_UI_ACTION_SET_RESTART_DELAY: app->settings.restart_delay = action->value; break;
    case PLUGIN_UI_ACTION_SET_RESTART: app->settings.restart_shell = action->value; break;
    case PLUGIN_UI_ACTION_SET_BRIDGE:
        app->settings.bridge = action->value;
        if (action->value && wh_runner_start(0, app->settings.restart_delay) != 0)
            log_message(app, "[%s] failed to start Welcome Hub bridge\n", PLUGIN_ID);
        break;
    case PLUGIN_UI_ACTION_NONE: return ETA_E_NOT_FOUND;
    }
    save_settings(app);
    apply_tiles(app, 1);
    return ETA_OK;
}

static int run_event_loop(plugin_app *app) {
    unsigned int keep_ticks = 0;
    while (running) {
        if (++keep_ticks >= 50) { /* every ~5 seconds (50 * 100ms) */
            keep_ticks = 0;
            if (app->settings.keep) apply_tiles(app, 0);
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
    if (plugin_settings_load(PLUGIN_SETTINGS_PATH, &app.settings) != 0) {
        fprintf(stderr, "[%s] failed to read %s; using defaults\n", PLUGIN_ID,
                PLUGIN_SETTINGS_PATH);
    }
    app.log_file = fopen(PLUGIN_LOG_PATH, "a");
    g_app = &app;
    dash_appdb_set_logger(appdb_log);

    signal(SIGINT, request_stop);
    signal(SIGTERM, request_stop);
    signal(SIGPIPE, SIG_IGN);
    (void)syscall(SYS_thr_set_name, -1, "dashboard.elf");

    log_message(&app, "[%s] starting %s %s\n", PLUGIN_ID, PLUGIN_NAME, PLUGIN_VERSION);

    const eta_status status = start_plugin(&app);
    int exit_code = 1;
    if (status == ETA_OK) {
        log_message(&app, "[%s] ready; store=%d plus=%d hub=%d keep=%d handle=%llu\n",
                    PLUGIN_ID, app.settings.store, app.settings.plus, app.settings.hub,
                    app.settings.keep, (unsigned long long)app.ui_handle);
        apply_tiles(&app, 1);
        if (app.settings.bridge) {
            if (wh_runner_start(app.settings.restart_shell, app.settings.restart_delay) != 0)
                log_message(&app, "[%s] failed to start Welcome Hub bridge\n", PLUGIN_ID);
            else
                log_message(&app, "[%s] Welcome Hub bridge started\n", PLUGIN_ID);
        }
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
