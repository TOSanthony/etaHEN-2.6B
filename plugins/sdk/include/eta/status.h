#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum eta_status {
    ETA_OK = 0,
    ETA_E_INVALID_ARGUMENT = -1,
    ETA_E_INVALID_STATE = -2,
    ETA_E_NOT_INITIALIZED = -3,
    ETA_E_ALREADY_INITIALIZED = -4,
    ETA_E_NOT_SUPPORTED = -5,
    ETA_E_NO_MEMORY = -6,
    ETA_E_IO = -7,
    ETA_E_PROTOCOL = -8,
    ETA_E_TIMEOUT = -9,
    ETA_E_PERMISSION = -10,
    ETA_E_NOT_FOUND = -11,
    ETA_E_BUSY = -12
} eta_status;

const char *eta_status_string(eta_status status);

#ifdef __cplusplus
}
#endif

