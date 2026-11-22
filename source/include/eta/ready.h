/* Copyright (C) 2025 etaHEN / LightningMods

Cross-process readiness protocol for etaHEN services.

Services publish a named marker under /system_tmp/etahen/ready/<name>.
Consumers wait with timeout instead of fixed sleep() races.

All runtime markers live under the shared etaHEN system_tmp namespace.
*/

#pragma once

#include <eta/system_tmp.h>

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ETA_READY_ROOT ETA_SYSTEM_TMP_READY_ROOT

/* Well-known service names (startup orchestration) */
#define ETA_READY_UTIL "util"
#define ETA_READY_KSTUFF "kstuff"
#define ETA_READY_DAEMON "daemon"
#define ETA_READY_TOOLBOX "toolbox"

/*
 * Runtime flags (same storage as ready markers; typed names replace ad-hoc
 * /system_tmp flag files).
 *   fps_overlay  — legacy only; cleared on boot (FPS inject removed)
 *   util_booted  — util finished at least one full start (mid-session restart detect)
 */
#define ETA_FLAG_FPS_OVERLAY "fps_overlay"
#define ETA_FLAG_UTIL_BOOTED "util_booted"

/* Publish / clear / query. name is a short token (no path separators). */
bool eta_ready_signal(const char *name);
bool eta_ready_clear(const char *name);
bool eta_ready_is_set(const char *name);

/*
 * Process-instance readiness.
 *
 * A service publishes the PID of the process instance it initialized.  A
 * consumer can then distinguish a still-running initialized instance from a
 * replacement process that reused the same service name.  The marker remains
 * compatible with eta_ready_is_set(); only its contents differ from the plain
 * marker value "1".
 */
bool eta_ready_signal_pid(const char *name, pid_t pid);
bool eta_ready_read_pid(const char *name, pid_t *pid_out);
bool eta_ready_matches_pid(const char *name, pid_t pid);

/*
 * Poll until marker is set or timeout_ms elapses.
 * poll_ms is the sleep between checks (clamped to >= 50).
 * Returns true if ready, false on timeout or invalid name.
 */
bool eta_ready_wait(const char *name, int timeout_ms, int poll_ms);

/* Wait until the marker contains exactly the expected process PID. */
bool eta_ready_wait_pid(const char *name, pid_t pid, int timeout_ms,
                          int poll_ms);

/* Build absolute path for a name into buf (for logging). */
bool eta_ready_path(const char *name, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif
