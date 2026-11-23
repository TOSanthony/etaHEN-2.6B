#include "http_service.h"

#include <stddef.h>
#include <string.h>
#include <unistd.h>

#include "http_server.h"

#define LISTENER_READY_WAIT_MS 2000
#define CONNECTION_DRAIN_WAIT_MS 2000

static void *http_thread_main(void *argument) {
    http_service *service = (http_service *)argument;

    /* Blocks until http_server_stop is observed or the listener dies. */
    (void)http_server_serve(service);

    pthread_mutex_lock(&service->mutex);
    service->running = 0;
    service->listening = 0;
    pthread_mutex_unlock(&service->mutex);
    return NULL;
}

static int wait_for_listener(http_service *service) {
    for (int waited = 0; waited < LISTENER_READY_WAIT_MS; waited += 50) {
        pthread_mutex_lock(&service->mutex);
        const int listening = service->listening;
        const int running = service->running;
        pthread_mutex_unlock(&service->mutex);

        if (listening) return 1;
        if (!running) return 0;
        usleep(50 * 1000);
    }
    return 0;
}

int http_service_init(http_service *service, media_store *store, uint16_t port,
                      int local_only) {
    if (!service || !store || port == 0) return 0;

    memset(service, 0, sizeof(*service));
    if (pthread_mutex_init(&service->mutex, NULL) != 0) return 0;

    service->store = store;
    service->port = port;
    service->local_only = local_only ? 1 : 0;
    service->initialized = 1;
    return 1;
}

void http_service_stop(http_service *service) {
    if (!service || !service->initialized) return;

    pthread_t thread = {0};
    int join_thread = 0;

    pthread_mutex_lock(&service->mutex);
    if (service->thread_created) {
        service->stop_requested = 1;
        thread = service->thread;
        join_thread = 1;
    }
    const uint16_t wake_port = service->port;
    pthread_mutex_unlock(&service->mutex);

    if (join_thread) {
        http_server_wake(wake_port);
        pthread_join(thread, NULL);
    }

    /* Detached connection workers notice stop_requested and their socket
     * timeouts, then exit; give them a bounded window to drain. */
    for (int waited = 0; waited < CONNECTION_DRAIN_WAIT_MS; waited += 50) {
        pthread_mutex_lock(&service->mutex);
        const int active = service->active_connections;
        pthread_mutex_unlock(&service->mutex);
        if (active <= 0) break;
        usleep(50 * 1000);
    }

    pthread_mutex_lock(&service->mutex);
    service->running = 0;
    service->listening = 0;
    service->thread_created = 0;
    service->stop_requested = 0;
    pthread_mutex_unlock(&service->mutex);
}

void http_service_destroy(http_service *service) {
    if (!service || !service->initialized) return;
    http_service_stop(service);
    pthread_mutex_destroy(&service->mutex);
    memset(service, 0, sizeof(*service));
}

int http_service_start(http_service *service) {
    if (!service || !service->initialized) return 0;
    http_service_stop(service);

    pthread_mutex_lock(&service->mutex);
    service->running = 1;
    service->stop_requested = 0;
    const int result =
        pthread_create(&service->thread, NULL, http_thread_main, service);
    if (result == 0) {
        service->thread_created = 1;
    } else {
        service->running = 0;
    }
    pthread_mutex_unlock(&service->mutex);

    if (result != 0 || !wait_for_listener(service)) {
        http_service_stop(service);
        return 0;
    }
    return 1;
}

int http_service_reconfigure(http_service *service, uint16_t port,
                             int local_only) {
    if (!service || !service->initialized || port == 0) return 0;

    pthread_mutex_lock(&service->mutex);
    const uint16_t previous_port = service->port;
    const int previous_local_only = service->local_only;
    const int was_running = service->running;
    if (!was_running) {
        service->port = port;
        service->local_only = local_only ? 1 : 0;
    }
    pthread_mutex_unlock(&service->mutex);
    if (!was_running) return 1;

    if (previous_port == port &&
        previous_local_only == (local_only ? 1 : 0)) {
        return 1;
    }

    http_service_stop(service);

    pthread_mutex_lock(&service->mutex);
    service->port = port;
    service->local_only = local_only ? 1 : 0;
    pthread_mutex_unlock(&service->mutex);

    if (http_service_start(service)) return 1;

    /* Roll back to the configuration that used to work. */
    pthread_mutex_lock(&service->mutex);
    service->port = previous_port;
    service->local_only = previous_local_only;
    pthread_mutex_unlock(&service->mutex);
    (void)http_service_start(service);
    return 0;
}

int http_service_restart(http_service *service) {
    if (!service || !service->initialized) return 0;
    return http_service_start(service);
}

int http_service_running(http_service *service) {
    if (!service || !service->initialized) return 0;
    pthread_mutex_lock(&service->mutex);
    const int running = service->running && service->listening;
    pthread_mutex_unlock(&service->mutex);
    return running;
}

uint16_t http_service_port(http_service *service) {
    if (!service || !service->initialized) return 0;
    pthread_mutex_lock(&service->mutex);
    const uint16_t port = service->port;
    pthread_mutex_unlock(&service->mutex);
    return port;
}
