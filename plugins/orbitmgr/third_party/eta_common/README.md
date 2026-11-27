# Vendored etaHEN-core helpers

This plugin needs two small pieces of etaHEN-core functionality that are not
part of `plugins/sdk` (plugins only link the public SDK, not etaHEN's
internal libraries):

- `elfldr_remote.c` / `.h` — unmodified copy of `source/common/elfldr_remote.c`
  and `source/include/elfldr_remote.h`. Lets the plugin launch an existing
  ELF through etaHEN's private elfldr (`127.0.0.1:9020`), the same socket
  protocol the daemon itself uses for user payloads.
- `orbit_proc.c` / `.h` — a trimmed rewrite of `source/libeta_proc`'s
  sysctl(KERN_PROC_PROC) process lookup, dropping the `eta/log.h` dependency
  so it builds standalone. `freebsd-helper.h` (also vendored, unmodified) is
  `kinfo_proc`'s layout for this console's kernel.

If etaHEN's elfldr protocol or `kinfo_proc` layout ever changes, re-sync these
files from `source/common/`, `source/include/` and `source/libeta_proc/` at
the top of the repository.
