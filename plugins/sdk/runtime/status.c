#include "eta/status.h"

const char *eta_status_string(eta_status status) {
    switch (status) {
    case ETA_OK: return "ok";
    case ETA_E_INVALID_ARGUMENT: return "invalid argument";
    case ETA_E_INVALID_STATE: return "invalid state";
    case ETA_E_NOT_INITIALIZED: return "not initialized";
    case ETA_E_ALREADY_INITIALIZED: return "already initialized";
    case ETA_E_NOT_SUPPORTED: return "not supported";
    case ETA_E_NO_MEMORY: return "out of memory";
    case ETA_E_IO: return "io error";
    case ETA_E_PROTOCOL: return "protocol error";
    case ETA_E_TIMEOUT: return "timeout";
    case ETA_E_PERMISSION: return "permission denied";
    case ETA_E_NOT_FOUND: return "not found";
    case ETA_E_BUSY: return "busy";
    default: return "unknown error";
    }
}

