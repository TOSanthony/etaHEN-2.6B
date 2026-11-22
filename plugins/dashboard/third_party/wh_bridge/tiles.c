/* Sets home-screen tile visibility directly in app.db (tables tbl_iconinfo*),
 * reusing the logic from TOSanthony/ps5-unhide-debug-settings' appdb.h /
 * dashboard.c set_tile(). Trimmed to just what's needed here (visibility --
 * not the Orbit Store pin/lock logic, which stays in that project's own
 * dashboard payload). Uses this payload's own LOG()/notify() from util.h
 * instead of appdb.h's private notification helper, to avoid declaring
 * sceKernelSendNotificationRequest twice with two different struct types. */
#include "tiles.h"
#include "common.h"
#include "util.h"
#include "sqlite3.h"

#define APPDB_PATH "/system_data/priv/mms/app.db"
#define APPDB_BACKUP_DIR "/data"

/* Default policy: show Welcome Hub, hide the PlayStation Store and
 * PlayStation Plus tiles. */
static const struct { const char* title_id; const char* label; int visible; } TILES[] = {
  { "NPXS40063", "Welcome Hub",      1 },
  { "NPXS40047", "PlayStation Store", 0 },
  { "NPXS40037", "PlayStation Plus",  0 },
};
#define N_TILES (sizeof TILES / sizeof TILES[0])

/* Orbit Store pin+lock (ported from ps5-unhide-debug-settings' appdb.h) */
#define ORBIT_ID "PPSA99177"
#define APPDB_DISP_SYSTEM 146
#define APPDB_DISP_GAMES  138
/* Same idea as PSGFX: the shell sorts tiles by lastAccessIndex/time, and a
 * launched game gets counter+1 from tbl_version. A tile given an index far
 * above the counter and a date in 2099 therefore stays in front forever.
 * tbl_version itself is never touched. */
#define APPDB_LOCK_INDEX 1000000000
#define APPDB_LOCK_TIME  "2099-12-31 12:00:00.000"

static int appdb_copy_file(const char *src, const char *dst) {
  int in = open(src, O_RDONLY);
  if (in < 0) return -1;
  int out = open(dst, O_WRONLY | O_CREAT | O_EXCL, 0666);
  if (out < 0) { int e = errno; close(in); errno = e; return -1; }
  char buf[65536]; ssize_t n; int rc = 0;
  while ((n = read(in, buf, sizeof buf)) > 0) {
    char *p = buf;
    while (n > 0) { ssize_t w = write(out, p, (size_t)n); if (w < 0) { rc = -1; goto done; } p += w; n -= w; }
  }
  if (n < 0) rc = -1;
  if (rc == 0 && fsync(out) != 0) rc = -1;
done:
  close(in);
  if (close(out) != 0) rc = -1;
  if (rc != 0) unlink(dst);
  return rc;
}
static int appdb_same_size(const char *a, const char *b) {
  struct stat sa, sb;
  return stat(a, &sa) == 0 && stat(b, &sb) == 0 && sa.st_size == sb.st_size;
}
/* Raw copy of app.db (+ -wal / -journal) to /data/app.db.<time>.bak.
 * Never overwrites an existing backup. Returns 0 on success. */
static int appdb_backup(void) {
  char dst[256], src[256], dsx[256];
  long t = (long)time(NULL);
  int ok = 0;
  for (int i = 0; i < 100 && !ok; i++) {
    if (i == 0) snprintf(dst, sizeof dst, APPDB_BACKUP_DIR "/app.db.%ld.bak", t);
    else snprintf(dst, sizeof dst, APPDB_BACKUP_DIR "/app.db.%ld-%d.bak", t, i);
    if (appdb_copy_file(APPDB_PATH, dst) == 0) { ok = 1; break; }
    if (errno != EEXIST) break;
  }
  if (!ok || !appdb_same_size(APPDB_PATH, dst)) { LOG("tiles: app.db backup failed: %s\n", strerror(errno)); return -1; }
  static const char *const sfx[] = {"-wal", "-journal"};
  for (int i = 0; i < 2; i++) {
    snprintf(src, sizeof src, APPDB_PATH "%s", sfx[i]);
    if (access(src, F_OK) != 0) continue;
    snprintf(dsx, sizeof dsx, "%s%s", dst, sfx[i]);
    if (appdb_copy_file(src, dsx) != 0) { LOG("tiles: backup of %s failed: %s\n", src, strerror(errno)); return -1; }
  }
  LOG("tiles: app.db backup OK: %s\n", dst);
  return 0;
}
static sqlite3 *appdb_open(void) {
  sqlite3 *db = NULL;
  if (sqlite3_open_v2(APPDB_PATH, &db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK) {
    LOG("tiles: cannot open %s: %s\n", APPDB_PATH, db ? sqlite3_errmsg(db) : "?");
    if (db) sqlite3_close(db);
    return NULL;
  }
  sqlite3_busy_timeout(db, 5000);
  return db;
}
static int appdb_has_columns(sqlite3 *db, const char *table) {
  char *sql = sqlite3_mprintf("PRAGMA table_info(\"%w\")", table);
  sqlite3_stmt *st = NULL;
  int has_id = 0, has_vis = 0;
  if (!sql) return 0;
  int rc = sqlite3_prepare_v2(db, sql, -1, &st, NULL);
  sqlite3_free(sql);
  if (rc != SQLITE_OK) return 0;
  while (sqlite3_step(st) == SQLITE_ROW) {
    const char *name = (const char *)sqlite3_column_text(st, 1);
    if (!name) continue;
    if (!strcasecmp(name, "titleId")) has_id = 1;
    if (!strcasecmp(name, "visible")) has_vis = 1;
  }
  sqlite3_finalize(st);
  return has_id && has_vis;
}
static int appdb_tables(sqlite3 *db, char **names, int max) {
  sqlite3_stmt *st = NULL; int n = 0;
  if (sqlite3_prepare_v2(db,
      "SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 'tbl_iconinfo%'",
      -1, &st, NULL) != SQLITE_OK) { LOG("tiles: table query failed: %s\n", sqlite3_errmsg(db)); return -1; }
  while (n < max && sqlite3_step(st) == SQLITE_ROW) {
    const char *s = (const char *)sqlite3_column_text(st, 0);
    if (s) names[n++] = strdup(s);
  }
  sqlite3_finalize(st);
  return n;
}
static int appdb_count(sqlite3 *db, const char *title_id, int *total) {
  char *names[64];
  int n = appdb_tables(db, names, 64);
  if (n < 0) return -1;
  *total = 0;
  for (int i = 0; i < n; i++) {
    if (appdb_has_columns(db, names[i])) {
      char *sql = sqlite3_mprintf("SELECT COUNT(*) FROM \"%w\" WHERE titleId=%Q", names[i], title_id);
      sqlite3_stmt *st = NULL;
      if (sql && sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) *total += sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
      }
      sqlite3_free(sql);
    }
    free(names[i]);
  }
  return 0;
}
/* Sets visible=value for title_id in every usable table. Returns the number
 * of rows changed, or -1 on error. */
static int appdb_set_visible(sqlite3 *db, const char *title_id, int value) {
  char *names[64];
  int n = appdb_tables(db, names, 64);
  int changed = 0, err = 0;
  if (n < 0) return -1;
  for (int i = 0; i < n; i++) {
    if (appdb_has_columns(db, names[i])) {
      char *e = NULL;
      char *sql = sqlite3_mprintf(
          "UPDATE \"%w\" SET visible=%d WHERE titleId=%Q AND IFNULL(visible,-1)<>%d",
          names[i], value, title_id, value);
      if (!sql) { err = 1; }
      else if (sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, &e) != SQLITE_OK) {
        LOG("tiles: %s: %s\n", names[i], e ? e : "begin failed"); err = 1;
      } else if (sqlite3_exec(db, sql, NULL, NULL, &e) != SQLITE_OK) {
        LOG("tiles: %s: %s\n", names[i], e ? e : "update failed");
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL); err = 1;
      } else {
        int c = sqlite3_changes(db);
        if (sqlite3_exec(db, "COMMIT", NULL, NULL, &e) != SQLITE_OK) {
          LOG("tiles: %s: %s\n", names[i], e ? e : "commit failed"); err = 1;
        } else changed += c;
      }
      sqlite3_free(e); sqlite3_free(sql);
    }
    free(names[i]);
  }
  return err ? -1 : changed;
}

static int appdb_col_exists(sqlite3 *db, const char *table, const char *col) {
  char *sql = sqlite3_mprintf("SELECT 1 FROM pragma_table_info(%Q) WHERE name=%Q", table, col);
  sqlite3_stmt *st = NULL;
  int ok = 0;
  if (sql && sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) {
    ok = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
  }
  sqlite3_free(sql);
  return ok;
}
/* 1 if title_id exists (first table that has it); *pinned = in the system
 * group, sorted with it, and with the lock index. */
static int appdb_system_state(sqlite3 *db, const char *title_id, int *pinned) {
  char *names[64];
  int n = appdb_tables(db, names, 64);
  int found = 0;
  for (int i = 0; i < n; i++) {
    if (!found) {
      char *sql = sqlite3_mprintf(
          "SELECT dispLocation, primaryTitleSort = IFNULL((SELECT primaryTitleSort FROM \"%w\""
          " WHERE dispLocation=%d AND titleId LIKE 'NPXS%%' LIMIT 1), -1),"
          " IFNULL(lastAccessIndex,0) >= %d "
          "FROM \"%w\" WHERE titleId=%Q LIMIT 1",
          names[i], APPDB_DISP_SYSTEM, APPDB_LOCK_INDEX, names[i], title_id);
      sqlite3_stmt *st = NULL;
      if (sql && sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) {
          *pinned = sqlite3_column_int(st, 0) == APPDB_DISP_SYSTEM
                    && sqlite3_column_int(st, 1) == 1
                    && sqlite3_column_int(st, 2) == 1;
          found = 1;
        }
        sqlite3_finalize(st);
      }
      sqlite3_free(sql);
    }
    free(names[i]);
  }
  return found;
}
/* Sets the recency fields of one row (table.key = val). lock=1: index 1e9
 * and a 2099 date; lock=0: back to the current tbl_version counter and the
 * current time. Columns that are NULL for the played-only fields stay NULL. */
static int appdb_recency(sqlite3 *db, const char *table, const char *key, const char *val, int lock) {
  static const char *always[] = {"lastAccessTime", "recentActivityDatePlayedOrInstalled",
                                 "lastInteractedTime", NULL};
  static const char *played[] = {"lastPlayedDate", "lastPlayedDateOnConsole",
                                 "recentActivityDatePlayedOrPurchased", NULL};
  char *set = sqlite3_mprintf("%s", "");
  const char *when = lock ? "'" APPDB_LOCK_TIME "'" : "strftime('%Y-%m-%d %H:%M:%f','now')";
  char idx[96];
  if (lock) snprintf(idx, sizeof idx, "%d", APPDB_LOCK_INDEX);
  else snprintf(idx, sizeof idx, "IFNULL((SELECT status FROM tbl_version WHERE category='access_index'),1)");
  if (appdb_col_exists(db, table, "lastAccessIndex")) {
    char *t = sqlite3_mprintf("%z%slastAccessIndex=%s", set, *set ? ", " : "", idx); set = t;
  }
  if (appdb_col_exists(db, table, "lastPlayedIndex")) {
    char *t = sqlite3_mprintf("%z%slastPlayedIndex=CASE WHEN lastPlayedIndex IS NULL THEN NULL ELSE %s END",
                              set, *set ? ", " : "", idx); set = t;
  }
  for (int i = 0; always[i]; i++)
    if (appdb_col_exists(db, table, always[i])) {
      char *t = sqlite3_mprintf("%z%s\"%w\"=%s", set, *set ? ", " : "", always[i], when); set = t;
    }
  for (int i = 0; played[i]; i++)
    if (appdb_col_exists(db, table, played[i])) {
      char *t = sqlite3_mprintf("%z%s\"%w\"=CASE WHEN \"%w\" IS NULL THEN NULL ELSE %s END",
                                set, *set ? ", " : "", played[i], played[i], when); set = t;
    }
  int rc = 0;
  if (*set) {
    char *sql = sqlite3_mprintf("UPDATE \"%w\" SET %s WHERE \"%w\"=%Q", table, set, key, val), *e = NULL;
    if (!sql || sqlite3_exec(db, sql, NULL, NULL, &e) != SQLITE_OK) {
      LOG("orbit: %s: %s\n", table, e ? e : "recency failed"); rc = -1;
    }
    sqlite3_free(e); sqlite3_free(sql);
  }
  sqlite3_free(set);
  return rc;
}
/* Lock/unlock the order of title_id in every usable table. Returns 0 or -1. */
static int appdb_set_lock(sqlite3 *db, const char *title_id, int lock) {
  char *names[64];
  int n = appdb_tables(db, names, 64), err = 0;
  if (n < 0) return -1;
  if (sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL) != SQLITE_OK) {
    for (int i = 0; i < n; i++) free(names[i]);
    return -1;
  }
  for (int i = 0; i < n; i++) {
    char *concept = NULL;
    char *q = sqlite3_mprintf("SELECT localConceptId FROM \"%w\" WHERE titleId=%Q LIMIT 1", names[i], title_id);
    sqlite3_stmt *st = NULL;
    if (q && sqlite3_prepare_v2(db, q, -1, &st, NULL) == SQLITE_OK) {
      if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_text(st, 0))
        concept = sqlite3_mprintf("%s", (const char *)sqlite3_column_text(st, 0));
      sqlite3_finalize(st);
    }
    sqlite3_free(q);
    if (appdb_recency(db, names[i], "titleId", title_id, lock)) err = 1;
    if (concept) {
      const char *suffix = names[i] + strlen("tbl_iconinfo_");
      char *ct = sqlite3_mprintf("tbl_concepticoninfo_%s", suffix);
      if (ct && appdb_col_exists(db, ct, "localConceptId"))
        if (appdb_recency(db, ct, "localConceptId", concept, lock)) err = 1;
      sqlite3_free(ct);
      if (i == 0 && appdb_col_exists(db, "tbl_conceptmetadata", "localConceptId"))
        if (appdb_recency(db, "tbl_conceptmetadata", "localConceptId", concept, lock)) err = 1;
    }
    sqlite3_free(concept);
    free(names[i]);
  }
  if (appdb_col_exists(db, "tbl_contentinfo", "titleId"))
    if (appdb_recency(db, "tbl_contentinfo", "titleId", title_id, lock)) err = 1;
  if (sqlite3_exec(db, err ? "ROLLBACK" : "COMMIT", NULL, NULL, NULL) != SQLITE_OK) err = 1;
  return err ? -1 : 0;
}
/* on=1: move title_id into the system group (dispLocation=146, pinnedStatus=2,
 * the same sort key as the system apps, lastAccessIndex just below the
 * lowest one there). Returns rows changed, or -1. */
static int appdb_set_system_pin(sqlite3 *db, const char *title_id, int on) {
  char *names[64];
  int n = appdb_tables(db, names, 64);
  int changed = 0, err = 0;
  if (n < 0) return -1;
  for (int i = 0; i < n; i++) {
    char *e = NULL;
    char *sql = sqlite3_mprintf(
        "UPDATE \"%w\" SET dispLocation=%d, pinnedStatus=2, visible=1,"
        " primaryTitleSort=IFNULL((SELECT primaryTitleSort FROM \"%w\""
        "  WHERE dispLocation=%d AND titleId LIKE 'NPXS%%' LIMIT 1), 4295163907),"
        " lastAccessIndex="
        " (SELECT IFNULL(MIN(lastAccessIndex),2)-1 FROM \"%w\""
        "  WHERE dispLocation=%d AND titleId<>%Q) WHERE titleId=%Q",
        names[i], APPDB_DISP_SYSTEM, names[i], APPDB_DISP_SYSTEM,
        names[i], APPDB_DISP_SYSTEM, title_id, title_id);
    if (!sql) err = 1;
    else {
      if (sqlite3_exec(db, sql, NULL, NULL, &e) != SQLITE_OK) {
        LOG("orbit: %s: %s\n", names[i], e ? e : "pin failed"); err = 1;
      } else changed += sqlite3_changes(db);
      sqlite3_free(e);
    }
    sqlite3_free(sql);
    free(names[i]);
  }
  (void)on; /* unpin isn't needed by this payload -- it never turns the pin off */
  return err ? -1 : changed;
}
static int g_orbit_watch = 0; /* 1 once Orbit Store has been pinned+locked */

void wh_apply_orbit_pin(void){
  sqlite3 *db = appdb_open();
  if (!db) return;
  int total = 0, pinned = 0;
  if (appdb_count(db, ORBIT_ID, &total) != 0 || total == 0) {
    LOG("orbit: Orbit Store (%s) not found in app.db, skipped\n", ORBIT_ID);
    sqlite3_close(db);
    return;
  }
  if (!appdb_system_state(db, ORBIT_ID, &pinned)) { sqlite3_close(db); return; }
  if (pinned) { g_orbit_watch = 1; sqlite3_close(db); return; }   /* already pinned+locked */
  if (appdb_backup() != 0) { sqlite3_close(db); return; }
  int changed = appdb_set_system_pin(db, ORBIT_ID, 1);
  if (changed >= 0 && appdb_set_lock(db, ORBIT_ID, 1) == 0) {
    g_orbit_watch = 1;
    LOG("orbit: Orbit Store pinned+locked (%d row(s))\n", changed);
  } else LOG("orbit: pin/lock failed\n");
  sqlite3_close(db);
}

/* Launching Orbit Store rewrites its lastAccessIndex, which drops the lock.
 * Re-apply it as soon as that happens; cheap no-op the rest of the time. */
void wh_orbit_watch_tick(void){
  if (!g_orbit_watch) return;
  sqlite3 *db = appdb_open();
  if (!db) return;
  int pinned = 0;
  if (appdb_system_state(db, ORBIT_ID, &pinned) && !pinned) {
    if (appdb_set_system_pin(db, ORBIT_ID, 1) >= 0 && appdb_set_lock(db, ORBIT_ID, 1) == 0)
      LOG("orbit: lock re-applied\n");
    else LOG("orbit: re-lock failed\n");
  }
  sqlite3_close(db);
}

void wh_apply_tile_visibility(void){
  sqlite3 *db = appdb_open();
  if (!db) return;
  static int backed_up = 0;
  for (size_t i = 0; i < N_TILES; i++){
    int total = 0;
    if (appdb_count(db, TILES[i].title_id, &total) != 0 || total == 0) {
      LOG("tiles: %s (%s) not found in app.db, skipped\n", TILES[i].label, TILES[i].title_id);
      continue;
    }
    if (!backed_up) { if (appdb_backup() != 0) { sqlite3_close(db); return; } backed_up = 1; }
    int changed = appdb_set_visible(db, TILES[i].title_id, TILES[i].visible);
    if (changed > 0) LOG("tiles: %s %s (%d row(s))\n", TILES[i].label, TILES[i].visible ? "shown" : "hidden", changed);
    else if (changed < 0) LOG("tiles: %s: update failed\n", TILES[i].label);
    /* changed == 0: already at the wanted value, nothing to log */
  }
  sqlite3_close(db);
}
