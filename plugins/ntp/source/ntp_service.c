#include "ntp_service.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void *ntp_worker_thread(void *arg) {
    ntp_service *service = (ntp_service *)arg;

    bool perform_boot_sync = true;

    while (1) {
        char server_copy[PLUGIN_SETTINGS_SERVER_MAX] = {0};
        bool should_sync = false;
        uint32_t interval_mins = 0;

        pthread_mutex_lock(&service->mutex);
        if (!service->running) {
            pthread_mutex_unlock(&service->mutex);
            break;
        }

        if (service->trigger_sync_requested) {
            should_sync = true;
            service->trigger_sync_requested = 0;
        } else if (perform_boot_sync) {
            perform_boot_sync = false;
            if (service->settings.enabled) {
                should_sync = true;
            }
        }

        snprintf(server_copy, sizeof(server_copy), "%s", service->settings.server);
        interval_mins = service->settings.interval_minutes;

        if (should_sync) {
            service->status.sync_in_progress = 1;
            snprintf(service->status.last_status_text,
                     sizeof(service->status.last_status_text),
                     "Synchronizing clock...");
        }
        pthread_mutex_unlock(&service->mutex);

        if (should_sync) {
            ntp_sync_result sync_res;
            int rc = ntp_synchronize_clock(server_copy, &sync_res);

            ntp_service_status status_copy;
            ntp_service_sync_done_callback cb = NULL;
            void *cb_data = NULL;

            pthread_mutex_lock(&service->mutex);
            service->status.sync_in_progress = 0;
            if (rc == 0) {
                service->status.last_sync_success = 1;
                snprintf(service->status.last_sync_utc,
                         sizeof(service->status.last_sync_utc),
                         "%s", sync_res.formatted_time);
                snprintf(service->status.last_server,
                         sizeof(service->status.last_server),
                         "%s", sync_res.server_used);
                snprintf(service->status.last_status_text,
                         sizeof(service->status.last_status_text),
                         "Synchronized via %s", sync_res.server_used);
            } else {
                service->status.last_sync_success = 0;
                snprintf(service->status.last_status_text,
                         sizeof(service->status.last_status_text),
                         "Sync error: %s", sync_res.error_message);
            }

            status_copy = service->status;
            cb = service->callback;
            cb_data = service->callback_userdata;
            pthread_mutex_unlock(&service->mutex);

            if (cb) {
                cb(&status_copy, cb_data);
            }
        }

        /* Wait for next event or interval */
        pthread_mutex_lock(&service->mutex);
        if (!service->running) {
            pthread_mutex_unlock(&service->mutex);
            break;
        }

        if (service->trigger_sync_requested) {
            pthread_mutex_unlock(&service->mutex);
            continue;
        }

        if (interval_mins > 0 && service->settings.enabled) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += (time_t)(interval_mins * 60);

            while (service->running && !service->trigger_sync_requested) {
                int wait_rc = pthread_cond_timedwait(&service->cond, &service->mutex, &ts);
                if (wait_rc != 0) {
                    /* Timed out -> periodic sync triggered */
                    service->trigger_sync_requested = 1;
                    break;
                }
            }
        } else {
            while (service->running && !service->trigger_sync_requested) {
                pthread_cond_wait(&service->cond, &service->mutex);
            }
        }
        pthread_mutex_unlock(&service->mutex);
    }

    return NULL;
}

int ntp_service_init(ntp_service *service, const plugin_settings *settings,
                     ntp_service_sync_done_callback cb, void *userdata) {
    if (!service || !settings) return 0;
    memset(service, 0, sizeof(*service));

    if (pthread_mutex_init(&service->mutex, NULL) != 0) return 0;
    if (pthread_cond_init(&service->cond, NULL) != 0) {
        pthread_mutex_destroy(&service->mutex);
        return 0;
    }

    service->settings = *settings;
    service->callback = cb;
    service->callback_userdata = userdata;
    snprintf(service->status.last_status_text, sizeof(service->status.last_status_text), "Idle");
    snprintf(service->status.last_sync_utc, sizeof(service->status.last_sync_utc), "Never");
    service->initialized = 1;
    return 1;
}

void ntp_service_destroy(ntp_service *service) {
    if (!service || !service->initialized) return;
    ntp_service_stop(service);
    pthread_cond_destroy(&service->cond);
    pthread_mutex_destroy(&service->mutex);
    memset(service, 0, sizeof(*service));
}

int ntp_service_start(ntp_service *service) {
    if (!service || !service->initialized) return 0;
    ntp_service_stop(service);

    pthread_mutex_lock(&service->mutex);
    service->running = 1;
    service->trigger_sync_requested = 0;
    int rc = pthread_create(&service->thread, NULL, ntp_worker_thread, service);
    if (rc == 0) {
        service->thread_created = 1;
    } else {
        service->running = 0;
    }
    pthread_mutex_unlock(&service->mutex);

    return rc == 0;
}

void ntp_service_stop(ntp_service *service) {
    if (!service || !service->initialized) return;

    pthread_t thread = {0};
    int join_thread = 0;

    pthread_mutex_lock(&service->mutex);
    if (service->thread_created) {
        service->running = 0;
        pthread_cond_signal(&service->cond);
        thread = service->thread;
        join_thread = 1;
    }
    pthread_mutex_unlock(&service->mutex);

    if (join_thread) {
        pthread_join(thread, NULL);
    }

    pthread_mutex_lock(&service->mutex);
    service->thread_created = 0;
    service->running = 0;
    pthread_mutex_unlock(&service->mutex);
}

void ntp_service_trigger_sync(ntp_service *service) {
    if (!service || !service->initialized) return;
    pthread_mutex_lock(&service->mutex);
    service->trigger_sync_requested = 1;
    pthread_cond_signal(&service->cond);
    pthread_mutex_unlock(&service->mutex);
}

void ntp_service_update_settings(ntp_service *service, const plugin_settings *settings) {
    if (!service || !service->initialized || !settings) return;
    pthread_mutex_lock(&service->mutex);
    service->settings = *settings;
    pthread_cond_signal(&service->cond);
    pthread_mutex_unlock(&service->mutex);
}

void ntp_service_get_status(ntp_service *service, ntp_service_status *out_status) {
    if (!service || !service->initialized || !out_status) return;
    pthread_mutex_lock(&service->mutex);
    *out_status = service->status;
    pthread_mutex_unlock(&service->mutex);
}
