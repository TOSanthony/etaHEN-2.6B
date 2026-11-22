/* tiles.h -- at payload startup, set the visibility of a few home-screen
 * tiles directly in app.db (same technique/tables as TOSanthony/
 * ps5-unhide-debug-settings' dashboard.c + appdb.h). No ShellUI restart or
 * console reboot needed: the shell re-reads app.db on its own. */
#ifndef WH_TILES_H
#define WH_TILES_H

/* Writes the default tile visibility: Welcome Hub shown, PlayStation Store
 * and PlayStation Plus hidden. Backs up app.db once (first call only) before
 * making any change, and is idempotent -- a tile already at the wanted value
 * is left untouched. Safe to call even if app.db can't be opened (logs and
 * returns without side effects). */
void wh_apply_tile_visibility(void);

/* Pins Orbit Store (PPSA99177) into the home screen's "system" row and locks
 * its sort order there (same technique as ps5-unhide-debug-settings'
 * dashboard.c set_orbit()/appdb.h, ported here so this payload alone is
 * enough -- no need to also run that other project's dashboard). Call once
 * at startup; idempotent, and a no-op if Orbit Store isn't installed.
 * Silent: only wh.log, never an on-screen notification. */
void wh_apply_orbit_pin(void);

/* Call periodically (e.g. from the existing WH_WATCH_SECS tick). Launching
 * Orbit Store rewrites its lastAccessIndex, which drops the lock -- this
 * re-applies it as soon as that happens. Cheap no-op the rest of the time. */
void wh_orbit_watch_tick(void);

#endif /* WH_TILES_H */
