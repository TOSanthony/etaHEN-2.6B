#include "eta/services.h"

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

eta_status eta_services_validate(const eta_host_services_v1 *services) {
    const size_t required_size = offsetof(eta_host_services_v1, query_interface);
    if (!services || services->struct_size < required_size ||
        services->abi_version != ETA_HOST_SERVICES_ABI_VERSION) {
        return ETA_E_PROTOCOL;
    }
    return ETA_OK;
}

eta_status eta_service_log(const eta_host_services_v1 *services,
                               eta_log_level level, const char *message) {
    if (eta_services_validate(services) != ETA_OK || !services->log || !message) {
        return ETA_E_INVALID_ARGUMENT;
    }
    return services->log(services->context, level, message);
}

eta_status eta_service_notify(const eta_host_services_v1 *services,
                                  const char *message) {
    if (eta_services_validate(services) != ETA_OK || !services->notify || !message) {
        return ETA_E_INVALID_ARGUMENT;
    }
    return services->notify(services->context, message);
}

eta_status eta_service_config_get(const eta_host_services_v1 *services,
                                      const char *key, char *value,
                                      size_t value_size) {
    if (eta_services_validate(services) != ETA_OK || !services->config_get ||
        !key || !value || value_size == 0) {
        return ETA_E_INVALID_ARGUMENT;
    }
    return services->config_get(services->context, key, value, value_size);
}

eta_status eta_service_config_set(const eta_host_services_v1 *services,
                                      const char *key, const char *value) {
    if (eta_services_validate(services) != ETA_OK || !services->config_set ||
        !key || !value) {
        return ETA_E_INVALID_ARGUMENT;
    }
    return services->config_set(services->context, key, value);
}

eta_status eta_service_config_get_int(const eta_host_services_v1 *services,
                                          const char *key, int64_t *value) {
    char text[64];
    if (!value) return ETA_E_INVALID_ARGUMENT;
    eta_status result = eta_service_config_get(services, key, text, sizeof(text));
    if (result != ETA_OK) return result;
    char *end = NULL;
    long long parsed = strtoll(text, &end, 10);
    if (end == text || *end != '\0') return ETA_E_PROTOCOL;
    *value = (int64_t)parsed;
    return ETA_OK;
}

eta_status eta_service_config_set_int(const eta_host_services_v1 *services,
                                          const char *key, int64_t value) {
    char text[64];
    int written = snprintf(text, sizeof(text), "%lld", (long long)value);
    if (written < 0 || (size_t)written >= sizeof(text)) return ETA_E_INVALID_ARGUMENT;
    return eta_service_config_set(services, key, text);
}

eta_status eta_service_config_get_bool(const eta_host_services_v1 *services,
                                           const char *key, int *value) {
    char text[16];
    if (!value) return ETA_E_INVALID_ARGUMENT;
    eta_status result = eta_service_config_get(services, key, text, sizeof(text));
    if (result != ETA_OK) return result;
    if (strcmp(text, "true") == 0 || strcmp(text, "1") == 0) *value = 1;
    else if (strcmp(text, "false") == 0 || strcmp(text, "0") == 0) *value = 0;
    else return ETA_E_PROTOCOL;
    return ETA_OK;
}

eta_status eta_service_config_set_bool(const eta_host_services_v1 *services,
                                           const char *key, int value) {
    return eta_service_config_set(services, key, value ? "true" : "false");
}

eta_status eta_service_query_interface(
    const eta_host_services_v1 *services, const char *name,
    uint32_t min_version, void *out_interface, size_t interface_size) {
    const size_t required_size = offsetof(eta_host_services_v1, query_interface) +
                                 sizeof(services->query_interface);
    if (eta_services_validate(services) != ETA_OK || !name || !out_interface ||
        interface_size == 0 || services->struct_size < required_size ||
        !services->query_interface) {
        return ETA_E_NOT_SUPPORTED;
    }
    return services->query_interface(services->context, name, min_version,
                                     out_interface, interface_size);
}
