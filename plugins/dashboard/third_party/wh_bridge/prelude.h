/* prelude.h -- builds the JS prelude spliced into the bridge: the generic
 * loader head (loader.js, embedded via embed.S) plus one section per widget
 * folder found in /data/wh-widgets. */
#ifndef WH_PRELUDE_H
#define WH_PRELUDE_H

#include "common.h"

/* Widget-load summary, filled in by build_prelude() and read back by main()
 * for the "Widgets: N loaded" notification line. */
extern int g_wok, g_wbad, g_wmore;
extern char g_wfail[200];

/* Scans WH_WIDGETS_DIR and returns a malloc'd, NUL-terminated JS prelude in
 * outbuf/outlen (caller frees *outbuf). Never fails: a widget with no/bad
 * config.json or widget.js is skipped and logged, not fatal.
 * If gen_out is non-NULL, *gen_out gets the generation hash (see prelude.c's
 * hot-reload note) -- a cheap way to ask "did any widget's id/config.json/
 * widget.js change on disk?" without the ptrace install that normally comes
 * with building a prelude; used by the background watcher in server.c. */
void build_prelude(char** outbuf, size_t* outlen, uint64_t* gen_out);

#endif /* WH_PRELUDE_H */
