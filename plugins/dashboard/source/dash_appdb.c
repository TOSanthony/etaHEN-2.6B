/* App.db tile visibility helpers (logic from TOSanthony/dashboard tiles.c,
 * trimmed to tile visibility). */
#include "dash_appdb.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "sqlite3.h"

#define APPDB_PATH "/system_data/priv/mms/app.db"
#define APPDB_BACKUP_DIR "/data"

static void (*g_log)(const char *msg);
void dash_appdb_set_logger(void (*fn)(const char *msg)) { g_log = fn; }
static void dash_log(const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  if (g_log) g_log(buf);
}

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
  if (!ok || !appdb_same_size(APPDB_PATH, dst)) { dash_log("tiles: app.db backup failed: %s\n", strerror(errno)); return -1; }
  static const char *const sfx[] = {"-wal", "-journal"};
  for (int i = 0; i < 2; i++) {
    snprintf(src, sizeof src, APPDB_PATH "%s", sfx[i]);
    if (access(src, F_OK) != 0) continue;
    snprintf(dsx, sizeof dsx, "%s%s", dst, sfx[i]);
    if (appdb_copy_file(src, dsx) != 0) { dash_log("tiles: backup of %s failed: %s\n", src, strerror(errno)); return -1; }
  }
  dash_log("tiles: app.db backup OK: %s\n", dst);
  return 0;
}
static sqlite3 *appdb_open(void) {
  sqlite3 *db = NULL;
  if (sqlite3_open_v2(APPDB_PATH, &db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK) {
    dash_log("tiles: cannot open %s: %s\n", APPDB_PATH, db ? sqlite3_errmsg(db) : "?");
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
      -1, &st, NULL) != SQLITE_OK) { dash_log("tiles: table query failed: %s\n", sqlite3_errmsg(db)); return -1; }
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
        dash_log("tiles: %s: %s\n", names[i], e ? e : "begin failed"); err = 1;
      } else if (sqlite3_exec(db, sql, NULL, NULL, &e) != SQLITE_OK) {
        dash_log("tiles: %s: %s\n", names[i], e ? e : "update failed");
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL); err = 1;
      } else {
        int c = sqlite3_changes(db);
        if (sqlite3_exec(db, "COMMIT", NULL, NULL, &e) != SQLITE_OK) {
          dash_log("tiles: %s: %s\n", names[i], e ? e : "commit failed"); err = 1;
        } else changed += c;
      }
      sqlite3_free(e); sqlite3_free(sql);
    }
    free(names[i]);
  }
  return err ? -1 : changed;
}


/* Number of rows that would change (no write). */
static int appdb_set_visible_dry(sqlite3 *db, const char *title_id, int value) {
  char *names[64];
  int n = appdb_tables(db, names, 64), total = 0;
  if (n < 0) return -1;
  for (int i = 0; i < n; i++) {
    if (appdb_has_columns(db, names[i])) {
      char *sql = sqlite3_mprintf(
          "SELECT COUNT(*) FROM \"%w\" WHERE titleId=%Q AND IFNULL(visible,-1)<>%d",
          names[i], title_id, value);
      sqlite3_stmt *st = NULL;
      if (sql && sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) total += sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
      }
      sqlite3_free(sql);
    }
    free(names[i]);
  }
  return total;
}

int dash_appdb_set_tile(const char *title_id, int visible, int *changed_out) {
  static int backed_up = 0;
  sqlite3 *db = appdb_open();
  int total = 0, changed;
  if (changed_out) *changed_out = 0;
  if (!db) return -1;
  if (appdb_count(db, title_id, &total) != 0) { sqlite3_close(db); return -1; }
  if (total == 0) { sqlite3_close(db); return 1; /* tile absent */ }
  /* Only back up when something will actually change. */
  changed = appdb_set_visible_dry(db, title_id, visible);
  if (changed > 0 && !backed_up) {
    if (appdb_backup() != 0) { sqlite3_close(db); return -1; }
    backed_up = 1;
  }
  changed = changed > 0 ? appdb_set_visible(db, title_id, visible) : changed;
  sqlite3_close(db);
  if (changed < 0) return -1;
  if (changed_out) *changed_out = changed;
  return 0;
}
