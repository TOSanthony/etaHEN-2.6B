#pragma once
/* Trimmed, self-contained process lookup (sysctl KERN_PROC_PROC), adapted
 * from etaHEN's libeta_proc so this plugin does not depend on core-only
 * headers (eta/log.h). Matches the kinfo_proc layout in freebsd-helper.h. */

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Exact match on kinfo_proc.ki_comm (and ki_tdname). Returns pid or -1. */
pid_t orbit_find_pid(const char *name);

/** True if pid still exists (KERN_PROC_PID). */
int orbit_proc_is_alive(pid_t pid);

#ifdef __cplusplus
}
#endif
