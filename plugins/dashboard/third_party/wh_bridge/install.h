/* install.h -- one install into SceShellUI: attach, build the code cave,
 * write it, and point the JSI vtable slot at the detour. */
#ifndef WH_INSTALL_H
#define WH_INSTALL_H

#include "common.h"

/* Attaches to `pid`, discovers the hook point, builds the cave (prelude +
 * bridge), writes it in and re-points evaluateJavaScript's vtable slot at it.
 * `want_dump` enables the opt-in HBC dump diagnostics (WH_DUMP_FLAG present).
 * Always detaches before returning. Sets *reload_out to 1 if this replaced an
 * existing hook of ours (vs. a clean first install). Returns 0 (WH_OK) on
 * success, or a small positive rc -- see main.c's "why" table for what each
 * one means. */
int wh_install_once(pid_t pid, int want_dump, int* reload_out);

/* Same as wh_install_once, but also reports the widget generation hash that
 * was just baked into the cave (see prelude.c's hot-reload note) when
 * gen_out is non-NULL -- used by the background watcher in server.c so it
 * knows, after re-installing, which generation is now live without a second
 * disk scan. */
int wh_install_once_g(pid_t pid, int want_dump, int* reload_out, uint64_t* gen_out);

#endif /* WH_INSTALL_H */
