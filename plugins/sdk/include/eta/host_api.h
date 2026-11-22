#pragma once

#include <stdint.h>

#include "eta/ipc.h"
#include "eta/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum eta_log_level {
    ETA_LOG_DEBUG = 0,
    ETA_LOG_INFO = 1,
    ETA_LOG_WARN = 2,
    ETA_LOG_ERROR = 3
} eta_log_level;

/* Host calls are transported over IPC for standalone plugin processes. */
typedef struct eta_plugin_host_context_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    int control_fd;
    const char *plugin_id;
} eta_plugin_host_context_v1;

eta_status eta_plugin_ipc_send(int fd, eta_plugin_ipc_frame *frame);
eta_status eta_plugin_ipc_recv(int fd, eta_plugin_ipc_frame *frame);
eta_status eta_plugin_log(int fd, eta_log_level level, const char *message);
eta_status eta_plugin_notify(int fd, const char *message);

#ifdef __cplusplus
}
#endif
