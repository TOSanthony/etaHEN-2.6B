#include "eta/runtime.h"

#include <string.h>

static int descriptor_valid(const eta_plugin_descriptor_v1 *descriptor) {
    return descriptor && descriptor->struct_size >= sizeof(*descriptor) &&
           descriptor->abi_version == ETA_PLUGIN_ABI_VERSION &&
           descriptor->plugin_id[0] != '\0' && descriptor->version[0] != '\0';
}

eta_status eta_plugin_runtime_init(
    eta_plugin_runtime *runtime,
    const eta_plugin_descriptor_v1 *descriptor,
    const eta_plugin_callbacks_v1 *callbacks,
    const eta_host_services_v1 *services) {
    if (!runtime || !descriptor || !callbacks || !services || !descriptor_valid(descriptor) ||
        callbacks->struct_size < sizeof(*callbacks) || callbacks->abi_version != ETA_PLUGIN_ABI_VERSION) {
        return ETA_E_INVALID_ARGUMENT;
    }
    if (runtime->struct_size == sizeof(*runtime) &&
        runtime->abi_version == ETA_PLUGIN_ABI_VERSION &&
        runtime->state != ETA_PLUGIN_STATE_CREATED &&
        runtime->state != ETA_PLUGIN_STATE_STOPPED) {
        return ETA_E_ALREADY_INITIALIZED;
    }
    eta_status result = eta_services_validate(services);
    if (result != ETA_OK) {
        return result;
    }
    memset(runtime, 0, sizeof(*runtime));
    runtime->struct_size = sizeof(*runtime);
    runtime->abi_version = ETA_PLUGIN_ABI_VERSION;
    runtime->descriptor = descriptor;
    runtime->callbacks = *callbacks;
    size_t services_size = services->struct_size;
    if (services_size > sizeof(runtime->services)) services_size = sizeof(runtime->services);
    memcpy(&runtime->services, services, services_size);
    runtime->state = ETA_PLUGIN_STATE_INITIALIZED;
    if (runtime->callbacks.on_init) {
        eta_status callback_result = runtime->callbacks.on_init(runtime->callbacks.context, services);
        if (callback_result != ETA_OK) {
            runtime->state = ETA_PLUGIN_STATE_FAILED;
            return callback_result;
        }
    }
    return ETA_OK;
}

eta_status eta_plugin_runtime_start(eta_plugin_runtime *runtime) {
    if (!runtime) return ETA_E_INVALID_ARGUMENT;
    if (runtime->state != ETA_PLUGIN_STATE_INITIALIZED &&
        runtime->state != ETA_PLUGIN_STATE_STOPPED) return ETA_E_INVALID_STATE;
    runtime->state = ETA_PLUGIN_STATE_RUNNING;
    if (runtime->callbacks.on_start) {
        eta_status result = runtime->callbacks.on_start(runtime->callbacks.context);
        if (result != ETA_OK) {
            runtime->state = ETA_PLUGIN_STATE_FAILED;
            return result;
        }
    }
    return ETA_OK;
}

eta_status eta_plugin_runtime_stop(eta_plugin_runtime *runtime) {
    if (!runtime) return ETA_E_INVALID_ARGUMENT;
    if (runtime->state != ETA_PLUGIN_STATE_RUNNING &&
        runtime->state != ETA_PLUGIN_STATE_FAILED) return ETA_E_INVALID_STATE;
    runtime->state = ETA_PLUGIN_STATE_STOPPING;
    if (runtime->callbacks.on_stop) {
        eta_status result = runtime->callbacks.on_stop(runtime->callbacks.context);
        if (result != ETA_OK) {
            runtime->state = ETA_PLUGIN_STATE_FAILED;
            return result;
        }
    }
    runtime->state = ETA_PLUGIN_STATE_STOPPED;
    return ETA_OK;
}

eta_status eta_plugin_runtime_shutdown(eta_plugin_runtime *runtime) {
    if (!runtime) return ETA_E_INVALID_ARGUMENT;
    if (runtime->state == ETA_PLUGIN_STATE_RUNNING || runtime->state == ETA_PLUGIN_STATE_FAILED) {
        eta_status result = eta_plugin_runtime_stop(runtime);
        if (result != ETA_OK) return result;
    }
    if (runtime->state != ETA_PLUGIN_STATE_STOPPED &&
        runtime->state != ETA_PLUGIN_STATE_INITIALIZED) return ETA_E_INVALID_STATE;
    if (runtime->callbacks.on_shutdown) {
        eta_status result = runtime->callbacks.on_shutdown(runtime->callbacks.context);
        if (result != ETA_OK) return result;
    }
    runtime->state = ETA_PLUGIN_STATE_SHUTDOWN;
    return ETA_OK;
}

eta_status eta_plugin_runtime_fail(eta_plugin_runtime *runtime) {
    if (!runtime) return ETA_E_INVALID_ARGUMENT;
    if (runtime->state == ETA_PLUGIN_STATE_CREATED ||
        runtime->state == ETA_PLUGIN_STATE_STOPPED) return ETA_E_INVALID_STATE;
    runtime->state = ETA_PLUGIN_STATE_FAILED;
    return ETA_OK;
}

eta_plugin_state eta_plugin_runtime_state(const eta_plugin_runtime *runtime) {
    return runtime ? runtime->state : ETA_PLUGIN_STATE_FAILED;
}
