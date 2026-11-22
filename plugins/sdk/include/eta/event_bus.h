#pragma once

#include <stddef.h>
#include <stdint.h>

#include "eta/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t eta_event_id;
typedef uint64_t eta_event_subscription;
typedef void (*eta_event_callback)(eta_event_id event, const void *data,
                                     size_t data_size, void *user_data);

typedef struct eta_event_bus eta_event_bus;

enum eta_system_event {
    ETA_EVENT_SYSTEM_READY = 0x100,
    ETA_EVENT_SHELLUI_READY = 0x101,
    ETA_EVENT_GAME_STARTED = 0x102,
    ETA_EVENT_GAME_STOPPED = 0x103,
    ETA_EVENT_REST_MODE = 0x104,
    ETA_EVENT_RESUME = 0x105,
    ETA_EVENT_LANGUAGE_CHANGED = 0x106,
    ETA_EVENT_HOST_DISCONNECTING = 0x107,
    ETA_EVENT_UI_ACTION = 0x108
};

eta_status eta_event_bus_create(eta_event_bus **out_bus);
void eta_event_bus_destroy(eta_event_bus *bus);
eta_status eta_event_subscribe(eta_event_bus *bus, eta_event_id event,
                                   eta_event_callback callback, void *user_data,
                                   eta_event_subscription *out_subscription);
eta_status eta_event_unsubscribe(eta_event_bus *bus,
                                     eta_event_subscription subscription);
eta_status eta_event_publish(eta_event_bus *bus, eta_event_id event,
                                 const void *data, size_t data_size);

#ifdef __cplusplus
}
#endif
