#pragma once

#include <stdint.h>

#include "eta/plugin.h"
#include "eta/services.h"
#include "eta/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum eta_plugin_state {
    ETA_PLUGIN_STATE_CREATED = 0,
    ETA_PLUGIN_STATE_INITIALIZED = 1,
    ETA_PLUGIN_STATE_RUNNING = 2,
    ETA_PLUGIN_STATE_STOPPING = 3,
    ETA_PLUGIN_STATE_STOPPED = 4,
    ETA_PLUGIN_STATE_FAILED = 5,
    ETA_PLUGIN_STATE_SHUTDOWN = 6
} eta_plugin_state;

typedef eta_status (*eta_plugin_init_fn)(
    void *context, const eta_host_services_v1 *services);
typedef eta_status (*eta_plugin_lifecycle_fn)(void *context);

typedef struct eta_plugin_callbacks_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    void *context;
    eta_plugin_init_fn on_init;
    eta_plugin_lifecycle_fn on_start;
    eta_plugin_lifecycle_fn on_stop;
    eta_plugin_lifecycle_fn on_shutdown;
} eta_plugin_callbacks_v1;

typedef struct eta_plugin_runtime {
    uint32_t struct_size;
    uint32_t abi_version;
    const eta_plugin_descriptor_v1 *descriptor;
    eta_plugin_callbacks_v1 callbacks;
    eta_host_services_v1 services;
    eta_plugin_state state;
} eta_plugin_runtime;

#define ETA_PLUGIN_RUNTIME_INITIALIZER {0}

eta_status eta_plugin_runtime_init(
    eta_plugin_runtime *runtime,
    const eta_plugin_descriptor_v1 *descriptor,
    const eta_plugin_callbacks_v1 *callbacks,
    const eta_host_services_v1 *services);
eta_status eta_plugin_runtime_start(eta_plugin_runtime *runtime);
eta_status eta_plugin_runtime_stop(eta_plugin_runtime *runtime);
eta_status eta_plugin_runtime_fail(eta_plugin_runtime *runtime);
eta_status eta_plugin_runtime_shutdown(eta_plugin_runtime *runtime);
eta_plugin_state eta_plugin_runtime_state(const eta_plugin_runtime *runtime);

#ifdef __cplusplus
}
#endif
