/* common.h -- shared system includes, basics and small macros used across
 * every file of this payload. Include this first in every .c file. */
#ifndef WH_COMMON_H
#define WH_COMMON_H

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <machine/reg.h>
#include <ps5/kernel.h>
#include <ps5/klog.h>
#include <ps5/mdbg.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

/* ---- payload version ---- */
#define WH_VERSION "26.11.13 beta"

#define WH_WIDGETS_DIR "/data/wh-widgets"

#endif /* WH_COMMON_H */
