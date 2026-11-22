/* applist.h -- lists installed games (titleId + display name) for the
 * "jeux-favoris" quick-launch widget, and locates each one's icon0.png.
 *
 * Two pieces of on-disk data:
 *   - which titleIds are installed, non-system "games" -- from app.db
 *     (tbl_iconinfo*, dispLocation=138), same tables tiles.c already reads.
 *   - each title's display name + cover art -- from its appmeta folder,
 *     /system_data/priv/appmeta/<titleId>/{param.json,icon0.png}, with
 *     /user/appmeta/<titleId>/ tried as a fallback.
 *   - "is this title actually installed right now" check:
 *     /user/app/<titleId>/mount.lnk existing.
 *
 * History: the first version of this file used /system_ex/app/<titleId>/
 * sce_sys/{param.json,icon0.png} for the second point, a path copied from
 * SvenGDK/PS-Multi-Tools (an open-source PS5 backup manager) -- but that
 * tool reads from an OFFLINE BACKUP DUMP, not a live console, and on a real
 * console that path doesn't exist. Confirmed empty in a real wh.log: app.db
 * correctly listed 9 installed titleIds, mount.lnk was found for all of
 * them, but param.json was MISSING for all of them at that path. Corrected
 * to /system_data/priv/appmeta/<titleId>/, per PS-Mac-Tools (same tool
 * family, open source): its live-console dump pulls
 * /system_data/priv/appmeta/<titleId>/ (and /user/appmeta/<titleId>/ as an
 * alternate source) into the sce_sys/ folder of its output -- i.e. that's
 * where sce_sys's content actually lives on a running console, cached
 * separately from the encrypted game image so it can be read without
 * mounting it. mount.lnk's path was right the first time (confirmed
 * present in that same wh.log). Still not tested end-to-end by this project
 * (no console of our own) -- if the favoris widget is still empty after
 * this fix, check wh.log's "applist:" lines again, they say exactly which
 * of the two checks is failing and for which titleId. */
#ifndef WH_APPLIST_H
#define WH_APPLIST_H

#include "common.h"

/* Builds the JSON array served at GET /__apps: every installed, non-system
 * game, most-recently-played first, e.g.
 *   [{"id":"PPSA01234","title":"Elden Ring","icon":true}, ...]
 * "icon" is true only if that title's icon0.png was actually found on disk
 * (the widget falls back to a placeholder otherwise). Returns 0 on success
 * (caller frees *out with free()), -1 on failure (app.db unreadable). */
int wh_apps_json(char** out, size_t* outlen);

/* Validates title_id (same charset as wh_name_ok) and, if its icon0.png
 * exists on disk, writes the path into out (cap bytes) and returns 1.
 * Returns 0 if the id is invalid or the file isn't there. */
int wh_app_icon_path(const char* title_id, char* out, size_t cap);

#endif /* WH_APPLIST_H */
