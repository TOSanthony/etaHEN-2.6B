#pragma once

#include <pthread.h>
#include <stdint.h>
#include "ntp_sync.h"
#include "plugin_settings.h"

typedef struct ntp_service_status {
    int sync_in_progress;
    int last_sync_success;
    char last_sync_utc[64];
    char last_server[128];
    char last_status_text[128];
} ntp_service_status;

typedef void (*ntp_service_sync_done_callback)(const ntp_service_status *status, void *userdata);

typedef struct ntp_service {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t thread;
    plugin_settings settings;
    ntp_service_status status;
    ntp_service_sync_done_callback callback;
    void *callback_userdata;
    int initialized;
    int running;
    int thread_created;
    int trigger_sync_requested;
} ntp_service;

int ntp_service_init(ntp_service *service, const plugin_settings *settings,
                     ntp_service_sync_done_callback cb, void *userdata);
void ntp_service_destroy(ntp_service *service);
int ntp_service_start(ntp_service *service);
void ntp_service_stop(ntp_service *service);
void ntp_service_trigger_sync(ntp_service *service);
void ntp_service_update_settings(ntp_service *service, const plugin_settings *settings);
void ntp_service_get_status(ntp_service *service, ntp_service_status *out_status);
