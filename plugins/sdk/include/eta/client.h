#pragma once

#include <pthread.h>
#include <stdint.h>

#include "eta/plugin.h"
#include "eta/services.h"
#include "eta/transport.h"
#include "eta/ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct eta_client {
    eta_transport *transport;
    pthread_mutex_t mutex;
    uint32_t next_request_id;
    uint32_t session_capabilities;
    int initialized;
    int session_open;
} eta_client;

#define ETA_CLIENT_INITIALIZER {0}

eta_status eta_client_init(eta_client *client, eta_transport *transport);
/* Bind one cooperative plugin identity to this transport connection. */
eta_status eta_client_open_session(
    eta_client *client, const eta_plugin_descriptor_v1 *descriptor);
void eta_client_deinit(eta_client *client);
eta_status eta_client_make_services(eta_client *client,
                                        eta_host_services_v1 *out_services);
eta_status eta_client_ping(eta_client *client);
/* Non-blocking poll; returns ETA_E_NOT_FOUND when no UI action is queued. */
eta_status eta_client_poll_ui_event(eta_client *client,
                                        eta_ui_event_v1 *out_event);

#ifdef __cplusplus
}
#endif
