/* remote.h -- attach to another process (ptrace, credential-elevated) and
 * read/write/map its memory. Everything here operates on an already-attached
 * target pid (see install.c for the attach/detach calls). */
#ifndef WH_REMOTE_H
#define WH_REMOTE_H

#include "common.h"

/* ptrace(2) wrapper that temporarily elevates this process's own credentials
 * (required on PS5 to ptrace another process at all), then restores them. */
int sys_ptrace(int req, pid_t pid, caddr_t addr, int data);

int remote_read(pid_t pid, uintptr_t a, void *o, size_t l);
int remote_write_d(pid_t pid, uintptr_t a, const void *s, size_t l);
int remote_write_chunked(pid_t pid, uintptr_t a, const uint8_t *s, size_t l);

/* Resolves a small ROP gadget in the target process, used to make it run a
 * syscall (mmap) on our behalf via remote_mmap(). */
uintptr_t gadget_of(pid_t pid);

/* Allocates `len` bytes in the target process (RW, or RWX if exec != 0). */
uintptr_t remote_mmap(pid_t pid, uintptr_t g, size_t len, int exec);

#endif /* WH_REMOTE_H */
