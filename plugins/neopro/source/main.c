/* PS4 Pro mode plugin: ps-patch-system + NeoEnabler merged into one etaHEN plugin. */

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

#include "neo_core.h"
#include "plugin_config.h"
#include "plugin_settings.h"
#include "plugin_ui.h"

#define CONNECT_ATTEMPTS 30
#define CONNECT_RETRY_US (250 * 1000)
#define EVENT_POLL_US (100 * 1000)
#define PLUGIN_AUTH_ID UINT64_C(0x4801000000000013)

extern const eta_plugin_descriptor_v1 eta_plugin_descriptor;

typedef struct plugin_app {
    eta_transport        transport;
    eta_socket_transport socket;
    eta_client           client;
    eta_host_services_v1 services;
    eta_ui_document     *document;
    eta_ui_handle        ui_handle;
    plugin_ui_state      ui_state;
    plugin_settings      settings;
    FILE                *log_file;
    int                  transport_connected;
    int                  client_initialized;
    int                  ui_dirty;
    unsigned             refresh_ticks;
    char                 last_status[4 * PLUGIN_UI_STATUS_MAX];
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
        if (eta_socket_transport_connect(&app->transport, &app->socket, ETA_PLUGIN_IPC_SOCKET_PATH) == ETA_OK) {
            app->transport_connected = 1;
            return ETA_OK;
        }
        if (attempt < CONNECT_ATTEMPTS) usleep(CONNECT_RETRY_US);
    }
    return ETA_E_IO;
}

static void save_settings(plugin_app *app) {
    if (plugin_settings_save(PLUGIN_SETTINGS_PATH, &app->settings) != 0)
        log_message(app, "[%s] failed to save %s\n", PLUGIN_ID, PLUGIN_SETTINGS_PATH);
}

/* The SDK cannot update a LIST in place: the document is rebuilt when something changed. */
static neo_config make_config(const plugin_settings *s) {
    neo_config c = {.enabled = s->enabled, .all_games = s->all_games, .force_8gib = s->force_8gib,
                    .gpu_on = s->gpu_on, .gpu_mhz = s->gpu_mhz, .cpu_on = s->cpu_on, .cpu_mhz = s->cpu_mhz};
    return c;
}

static void refresh_state(plugin_app *app) {
    neo_core_text(-1, app->ui_state.status, sizeof(app->ui_state.status));
    neo_core_text(0, app->ui_state.memory_text, sizeof(app->ui_state.memory_text));
    neo_core_text(1, app->ui_state.gpu_text, sizeof(app->ui_state.gpu_text));
    neo_core_text(2, app->ui_state.cpu_text, sizeof(app->ui_state.cpu_text));
    neo_core_limits(&app->ui_state.limits);
    app->ui_state.game_count = neo_games_list(app->ui_state.games, NEO_MAX_GAMES);
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
    if (plugin_ui_create(&app->ui_state, &app->document) != ETA_OK) {
        app->document = NULL;
        return;
    }
    if (eta_ui_register(&app->services, app->document, &app->ui_handle) != ETA_OK) {
        eta_ui_document_destroy(app->document);
        app->document = NULL;
        app->ui_handle = 0;
    }
}

static eta_status start_plugin(plugin_app *app) {
    (void)kernel_set_ucred_authid(getpid(), PLUGIN_AUTH_ID);

    eta_status status = connect_to_daemon(app);
    if (status != ETA_OK) return status;
    status = eta_client_init(&app->client, &app->transport);
    if (status != ETA_OK) return status;
    app->client_initialized = 1;
    status = eta_client_open_session(&app->client, &eta_plugin_descriptor);
    if (status == ETA_OK) status = eta_client_make_services(&app->client, &app->services);
    if (status != ETA_OK) return status;

    /* Same sandbox exit as the Media Gallery / DPI plugins: root ids, real root directory, full caps. */
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

    {
        neo_config cfg = make_config(&app->settings);
        if (neo_core_start(&cfg) != 0) return ETA_E_IO;
    }

    plugin_ui_state_init(&app->ui_state, &app->settings);
    refresh_state(app);
    status = plugin_ui_create(&app->ui_state, &app->document);
    if (status == ETA_OK) status = eta_ui_register(&app->services, app->document, &app->ui_handle);
    return status;
}

static void stop_plugin(plugin_app *app) {
    neo_core_stop();
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

static eta_status apply_action(plugin_app *app, const plugin_ui_action *action) {
    char msg[PLUGIN_UI_STATUS_MAX] = "";
    switch (action->kind) {
    case PLUGIN_UI_ACTION_SET_ENABLED:
        app->settings.enabled = action->enabled;
        break;
    case PLUGIN_UI_ACTION_SET_ALL_GAMES:
        app->settings.all_games = action->enabled;
        break;
    case PLUGIN_UI_ACTION_SYNC:
        (void)neo_core_sync(msg, sizeof(msg));
        snprintf(app->ui_state.message, sizeof(app->ui_state.message), "%s", msg);
        app->ui_dirty = 1;
        return ETA_OK;
    case PLUGIN_UI_ACTION_SET_FORCE_8GIB:
        app->settings.force_8gib = action->enabled;
        break;
    case PLUGIN_UI_ACTION_SET_GPU:
        app->settings.gpu_on = action->enabled;
        break;
    case PLUGIN_UI_ACTION_SET_CPU:
        app->settings.cpu_on = action->enabled;
        break;
    case PLUGIN_UI_ACTION_SET_GPU_MHZ:
    case PLUGIN_UI_ACTION_SET_CPU_MHZ: {
        neo_limits l;
        neo_core_limits(&l);
        const int gpu = action->kind == PLUGIN_UI_ACTION_SET_GPU_MHZ;
        const unsigned lo = gpu ? l.gpu_min : l.cpu_min, hi = gpu ? l.gpu_max : l.cpu_max;
        if (action->mhz < lo || action->mhz > hi || action->mhz % 50 != 0) {
            snprintf(app->ui_state.message, sizeof(app->ui_state.message), "Frequence refusee : %u a %u MHz par pas de 50", lo, hi);
            app->ui_dirty = 1;
            return ETA_E_INVALID_ARGUMENT;
        }
        if (gpu) app->settings.gpu_mhz = action->mhz;
        else app->settings.cpu_mhz = action->mhz;
        break;
    }
    case PLUGIN_UI_ACTION_SET_GAME:
        (void)neo_game_set(action->tid, action->game_mode, msg, sizeof(msg));
        log_message(app, "[%s] %s\n", PLUGIN_ID, msg);
        snprintf(app->ui_state.message, sizeof(app->ui_state.message), "%s", msg);
        app->ui_dirty = 1;
        return ETA_OK;
    case PLUGIN_UI_ACTION_NONE:
    default:
        return ETA_E_NOT_FOUND;
    }
    save_settings(app);
    {
        neo_config cfg = make_config(&app->settings);
        neo_core_set(&cfg);
    }
    app->ui_dirty = 1;
    return ETA_OK;
}

static int run_event_loop(plugin_app *app) {
    while (running) {
        /* Refresh the status line about every 2 s; rebuild only when the text changed. */
        if (++app->refresh_ticks >= 20) {
            app->refresh_ticks = 0;
            char status[4 * PLUGIN_UI_STATUS_MAX];
            neo_core_summary(status, sizeof(status));
            if (strcmp(status, app->last_status) != 0) {
                snprintf(app->last_status, sizeof(app->last_status), "%s", status);
                app->ui_dirty = 1;
            }
        }
        if (app->ui_dirty) {
            app->ui_dirty = 0;
            refresh_state(app);
            rebuild_ui(app);
        }

        eta_ui_event_v1 event;
        eta_status status = eta_client_poll_ui_event(&app->client, &event);
        if (status == ETA_E_NOT_FOUND) {
            usleep(EVENT_POLL_US);
            continue;
        }
        if (status != ETA_OK) {
            log_message(app, "[%s] UI event poll failed: %s\n", PLUGIN_ID, eta_status_string(status));
            return 1;
        }
        plugin_ui_action action;
        status = plugin_ui_decode_action(app->ui_handle, &event, &action);
        if (status == ETA_OK) status = apply_action(app, &action);
        if (status != ETA_OK && status != ETA_E_NOT_FOUND)
            log_message(app, "[%s] rejected action on %s: %s\n", PLUGIN_ID, event.node_id, eta_status_string(status));
    }
    return 0;
}

int main(void) {
    plugin_app app = {0};
    plugin_settings_defaults(&app.settings);
    if (plugin_settings_load(PLUGIN_SETTINGS_PATH, &app.settings) != 0)
        fprintf(stderr, "[%s] failed to read %s; using defaults\n", PLUGIN_ID, PLUGIN_SETTINGS_PATH);
    app.log_file = fopen(PLUGIN_LOG_PATH, "a");

    signal(SIGINT, request_stop);
    signal(SIGPIPE, SIG_IGN);
    (void)syscall(SYS_thr_set_name, -1, PLUGIN_ID ".elf");
    log_message(&app, "[%s] starting %s %s\n", PLUGIN_ID, PLUGIN_NAME, PLUGIN_VERSION);

    const eta_status status = start_plugin(&app);
    int exit_code = 1;
    if (status == ETA_OK) {
        log_message(&app, "[%s] ready; enabled=%d all_games=%d\n", PLUGIN_ID, app.settings.enabled, app.settings.all_games);
        exit_code = run_event_loop(&app);
    } else {
        log_message(&app, "[%s] startup failed: %s\n", PLUGIN_ID, eta_status_string(status));
    }
    stop_plugin(&app);
    log_message(&app, "[%s] stopped with exit_code=%d\n", PLUGIN_ID, exit_code);
    if (app.log_file) fclose(app.log_file);
    return exit_code;
}
