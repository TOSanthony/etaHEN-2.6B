/* Trimmed, self-contained process lookup (sysctl KERN_PROC_PROC), adapted
 * from etaHEN's libeta_proc for use inside a plugin build (no eta/log.h
 * dependency). See freebsd-helper.h for the kinfo_proc layout. */
#include "orbit_proc.h"

#include "freebsd-helper.h"

#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>

pid_t orbit_find_pid(const char *name) {
    if (!name || !*name) return -1;

    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t buf_size = 0;
    if (sysctl(mib, 4, NULL, &buf_size, NULL, 0)) return -1;

    void *buf = malloc(buf_size);
    if (!buf) return -1;
    if (sysctl(mib, 4, buf, &buf_size, NULL, 0)) {
        free(buf);
        return -1;
    }

    pid_t found = -1;
    for (char *ptr = (char *)buf; ptr < (char *)buf + buf_size;) {
        struct kinfo_proc *ki = (struct kinfo_proc *)ptr;
        if (ki->ki_structsize <= 0) break;
        ptr += ki->ki_structsize;

        int match = ki->ki_comm[0] && strcmp(ki->ki_comm, name) == 0;
        if (!match && ki->ki_tdname[0]) {
            match = strcmp(ki->ki_tdname, name) == 0;
        }
        if (match) {
            found = ki->ki_pid;
            break;
        }
    }
    free(buf);
    return found;
}

int orbit_proc_is_alive(pid_t pid) {
    if (pid <= 0) return 0;
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)pid};
    return sysctl(mib, 4, NULL, NULL, NULL, 0) == 0 ? 1 : 0;
}
