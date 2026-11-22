#pragma once

#include <pthread.h>
#include <stdint.h>

#include "media_index.h"

/* Owns the listener thread. The media store is borrowed, not owned, so its
 * lifetime stays with main(). */
typedef struct http_service {
    pthread_mutex_t mutex;
    pthread_t       thread;
    media_store    *store;
    uint16_t        port;
    int             local_only;
    int             initialized;
    int             running;
    int             thread_created;
    int             listening;
    int             stop_requested;
    int             active_connections;
} http_service;

int http_service_init(http_service *service, media_store *store, uint16_t port,
                      int local_only);
void http_service_destroy(http_service *service);
int http_service_start(http_service *service);
void http_service_stop(http_service *service);
int http_service_reconfigure(http_service *service, uint16_t port,
                             int local_only);
int http_service_restart(http_service *service);
int http_service_running(http_service *service);
uint16_t http_service_port(http_service *service);
