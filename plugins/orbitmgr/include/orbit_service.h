#pragma once

#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** pid (>0) if orbit_store.elf is currently running, else -1. */
pid_t orbit_service_pid(void);

static inline bool orbit_service_running(void) {
    return orbit_service_pid() > 0;
}

/**
 * Launch @elf_path (must already exist on the console) through etaHEN's
 * private elfldr (:9020). No-op (returns the live pid) if already running.
 * Returns the pid on success, -1 on failure.
 */
pid_t orbit_service_start(const char *elf_path);

/**
 * Stop a running orbit_store.elf: sceKernelTerminateProcess with a
 * temporarily raised authid, SIGKILL fallback. True if it was running and
 * is no longer, or if it was already stopped.
 */
bool orbit_service_stop(void);

#ifdef __cplusplus
}
#endif
