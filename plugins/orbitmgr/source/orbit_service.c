/* Starts/stops TOSanthony/orbit-store's backend service (orbit_store.elf)
 * from the etaHEN plugin menu. Launch goes through etaHEN's private elfldr
 * (:9020, see elfldr_remote.h); stop mirrors the dashboard plugin's
 * restart_shellui() helper (temporarily raised authid + SIGKILL fallback). */
#include "orbit_service.h"

#include <ps5/kernel.h>

#include <signal.h>
#include <unistd.h>

#include "elfldr_remote.h"
#include "orbit_proc.h"
#include "plugin_config.h"

extern int sceKernelTerminateProcess(int pid, int *ret);

/* Same authid used elsewhere in etaHEN (dashboard's restart_shellui) to be
 * allowed to terminate another process. */
#define DECID_AUTH_ID UINT64_C(0x4800000000000022)

pid_t orbit_service_pid(void) {
    return orbit_find_pid(ORBIT_SERVICE_PROC_NAME);
}

pid_t orbit_service_start(const char *elf_path) {
    pid_t existing = orbit_service_pid();
    if (existing > 0) return existing;
    if (!elf_path || elf_path[0] != '/') return -1;
    return elfldr_remote_eta_launch_file_get_pid(elf_path, NULL);
}

bool orbit_service_stop(void) {
    pid_t pid = orbit_service_pid();
    if (pid <= 0) return true; /* already stopped */
    if (pid == getpid()) return false;

    uint64_t old_authid = kernel_get_ucred_authid(getpid());
    kernel_set_ucred_authid(getpid(), DECID_AUTH_ID);
    int ret = 0;
    int rc = sceKernelTerminateProcess(pid, &ret);
    kernel_set_ucred_authid(getpid(), old_authid);
    if (rc != 0) rc = kill(pid, SIGKILL);

    return !orbit_proc_is_alive(pid);
}
