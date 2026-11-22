#pragma once

#include <stddef.h>
#include <stdint.h>

#include "eta/host_api.h"
#include "eta/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ETA_HOST_SERVICES_ABI_VERSION 1u

typedef eta_status (*eta_service_log_fn)(void *context,
                                             eta_log_level level,
                                             const char *message);
typedef eta_status (*eta_service_notify_fn)(void *context,
                                                const char *message);
typedef eta_status (*eta_service_config_get_fn)(void *context,
                                                    const char *key,
                                                    char *value,
                                                    size_t value_size);
typedef eta_status (*eta_service_config_set_fn)(void *context,
                                                    const char *key,
                                                    const char *value);
typedef eta_status (*eta_service_query_interface_fn)(
    void *context, const char *name, uint32_t min_version,
    void *out_interface, size_t interface_size);

typedef struct eta_host_services_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    void *context;
    eta_service_log_fn log;
    eta_service_notify_fn notify;
    eta_service_config_get_fn config_get;
    eta_service_config_set_fn config_set;
    /* Optional tail extension. Check struct_size before reading this field. */
    eta_service_query_interface_fn query_interface;
} eta_host_services_v1;

eta_status eta_services_validate(const eta_host_services_v1 *services);
eta_status eta_service_log(const eta_host_services_v1 *services,
                               eta_log_level level, const char *message);
eta_status eta_service_notify(const eta_host_services_v1 *services,
                                  const char *message);
eta_status eta_service_config_get(const eta_host_services_v1 *services,
                                      const char *key, char *value,
                                      size_t value_size);
eta_status eta_service_config_set(const eta_host_services_v1 *services,
                                      const char *key, const char *value);
eta_status eta_service_config_get_int(const eta_host_services_v1 *services,
                                          const char *key, int64_t *value);
eta_status eta_service_config_set_int(const eta_host_services_v1 *services,
                                          const char *key, int64_t value);
eta_status eta_service_config_get_bool(const eta_host_services_v1 *services,
                                           const char *key, int *value);
eta_status eta_service_config_set_bool(const eta_host_services_v1 *services,
                                           const char *key, int value);
eta_status eta_service_query_interface(
    const eta_host_services_v1 *services, const char *name,
    uint32_t min_version, void *out_interface, size_t interface_size);

#ifdef __cplusplus
}
#endif
