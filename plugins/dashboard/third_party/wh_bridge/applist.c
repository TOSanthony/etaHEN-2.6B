/* applist.c -- see applist.h. Read-only: no app.db writes, no backup (unlike
 * tiles.c, which mutates app.db and so backs it up first). */
#include "applist.h"
#include "common.h"
#include "util.h"
#include "sqlite3.h"

#define APPDB_PATH   "/system_data/priv/mms/app.db"
#define APPDB_DISP_GAMES 138
#define WH_APPS_MAX 96
/* Game metadata (param.json, icon0.png) on a LIVE console lives here, not
 * under /system_ex/app/<id>/sce_sys/ as first assumed (that path comes from
 * PS-Multi-Tools reading an offline backup dump, a different thing -- see
 * applist.h). Confirmed via PS-Mac-Tools (same family of tools, open
 * source): its live-console dump explicitly pulls
 * /system_data/priv/appmeta/<id>/ into the sce_sys/ folder of its output.
 * /user/appmeta/<id>/ is mentioned as a second source by the same tool;
 * tried as a fallback below in case a given title only has one of the two
 * (e.g. some PS4-BC titles may lack an appmeta entry entirely). */
#define APPMETA_PATH_PRIV "/system_data/priv/appmeta"
#define APPMETA_PATH_USER "/user/appmeta"

typedef struct { char id[32]; long long last; } AppRow;

static sqlite3* appdb_open_ro(void){
  sqlite3* db = NULL;
  if (sqlite3_open_v2(APPDB_PATH, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
    LOG("applist: cannot open %s: %s\n", APPDB_PATH, db ? sqlite3_errmsg(db) : "?");
    if (db) sqlite3_close(db);
    return NULL;
  }
  sqlite3_busy_timeout(db, 3000);
  return db;
}
static int appdb_tables(sqlite3* db, char** names, int max){
  sqlite3_stmt* st = NULL; int n = 0;
  if (sqlite3_prepare_v2(db,
      "SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 'tbl_iconinfo%'",
      -1, &st, NULL) != SQLITE_OK) { LOG("applist: table query failed: %s\n", sqlite3_errmsg(db)); return -1; }
  while (n < max && sqlite3_step(st) == SQLITE_ROW) {
    const char* s = (const char*)sqlite3_column_text(st, 0);
    if (s) names[n++] = strdup(s);
  }
  sqlite3_finalize(st);
  return n;
}
/* Merges titleId rows from every usable table into `rows`, keeping the
 * largest lastAccessIndex seen for a given titleId (several per-profile
 * tables can list the same installed title). */
static int apps_collect(sqlite3* db, AppRow* rows, int max){
  char* names[64];
  int nt = appdb_tables(db, names, 64);
  if (nt < 0) return -1;
  int n = 0;
  for (int i = 0; i < nt; i++) {
    char* sql = sqlite3_mprintf(
      "SELECT titleId, IFNULL(lastAccessIndex,0) FROM \"%w\" WHERE dispLocation=%d AND titleId IS NOT NULL",
      names[i], APPDB_DISP_GAMES);
    sqlite3_stmt* st = NULL;
    if (sql && sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) {
      while (sqlite3_step(st) == SQLITE_ROW) {
        const char* id = (const char*)sqlite3_column_text(st, 0);
        long long last = sqlite3_column_int64(st, 1);
        if (!id || !wh_name_ok(id, strlen(id)) || strlen(id) >= sizeof rows[0].id) continue;
        int found = -1;
        for (int k = 0; k < n; k++) if (!strcmp(rows[k].id, id)) { found = k; break; }
        if (found >= 0) { if (last > rows[found].last) rows[found].last = last; }
        else if (n < max) { snprintf(rows[n].id, sizeof rows[n].id, "%s", id); rows[n].last = last; n++; }
      }
      sqlite3_finalize(st);
    }
    sqlite3_free(sql);
    free(names[i]);
  }
  /* Most recently played/launched first (simple insertion sort -- n is
   * small, at most WH_APPS_MAX). */
  for (int i = 1; i < n; i++) {
    AppRow tmp = rows[i]; int j = i - 1;
    while (j >= 0 && rows[j].last < tmp.last) { rows[j+1] = rows[j]; j--; }
    rows[j+1] = tmp;
  }
  return n;
}
static int path_exists(const char* p){ struct stat st; return stat(p, &st) == 0; }

/* Picks the best title name out of param.json's localizedParameters:
 * en-US/en-GB first (English preferred), then the caller's preferred
 * locale, then whatever defaultLanguage says, then just the first
 * "titleName" anywhere in the file, then the titleId itself if nothing at
 * all is found.
 *
 * Bug fixed after a real-console test (LEGO Batman showed up in Arabic):
 * the locale-block pattern used to require the key, ':' and '{' with no
 * whitespace at all ("\"fr-FR\":{") -- param.json is usually minified like
 * that, but at least one title's wasn't, so the "fr-FR" (and "en-US")
 * lookups silently failed and fell all the way through to the last-resort
 * "first titleName anywhere in the file", which happens to land on
 * whichever locale sorts first in that title's JSON (ar-* in this case).
 * Now whitespace-tolerant around ':' the same way wh_json_str already is. */
static void pick_title(const char* json, const char* prefer_locale, const char* title_id, char* out, size_t cap){
  char pat[48], blk[768];
  const char* tries[4]; int nt = 0;
  char def_lang[16] = "";
  tries[nt++] = "en-US";
  tries[nt++] = "en-GB";
  if (prefer_locale && prefer_locale[0]) tries[nt++] = prefer_locale;
  if (wh_json_str(json, "defaultLanguage", def_lang, sizeof def_lang) == 0 && def_lang[0]) tries[nt++] = def_lang;
  for (int i = 0; i < nt; i++) {
    snprintf(pat, sizeof pat, "\"%s\"", tries[i]);
    const char* p = strstr(json, pat);
    if (!p) continue;
    p += strlen(pat);
    while (*p==' '||*p=='\t'||*p=='\n'||*p=='\r') p++;
    if (*p != ':') continue;
    p++;
    while (*p==' '||*p=='\t'||*p=='\n'||*p=='\r') p++;
    if (*p != '{') continue;              /* that key wasn't a locale block */
    int depth = 0; const char* e = p;
    for (; *e; e++) { if (*e == '{') depth++; else if (*e == '}') { depth--; if (!depth) { e++; break; } } }
    size_t blen = (size_t)(e - p); if (blen >= sizeof blk) blen = sizeof blk - 1;
    memcpy(blk, p, blen); blk[blen] = 0;
    if (wh_json_str(blk, "titleName", out, cap) == 0 && out[0]) return;
  }
  if (wh_json_str(json, "titleName", out, cap) == 0 && out[0]) return;
  snprintf(out, cap, "%s", title_id);
}
static void json_escape_into(const char* s, char* out, size_t cap){
  size_t o = 0;
  for (; *s && o + 2 < cap; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\') { if (o + 3 >= cap) break; out[o++] = '\\'; out[o++] = (char)c; }
    else if (c == '\n' || c == '\r' || c == '\t') { out[o++] = ' '; }
    else if (c < 0x20) { /* skip other control bytes */ }
    else out[o++] = (char)c;
  }
  out[o] = 0;
}

/* Tries /system_data/priv/appmeta/<id>/<file>, then /user/appmeta/<id>/<file>
 * (see the comment on APPMETA_PATH_PRIV above for why). */
static int appmeta_find(const char* title_id, const char* file, char* out, size_t cap){
  char p[160];
  snprintf(p, sizeof p, "%s/%s/%s", APPMETA_PATH_PRIV, title_id, file);
  if (path_exists(p)) { snprintf(out, cap, "%s", p); return 1; }
  snprintf(p, sizeof p, "%s/%s/%s", APPMETA_PATH_USER, title_id, file);
  if (path_exists(p)) { snprintf(out, cap, "%s", p); return 1; }
  return 0;
}

int wh_app_icon_path(const char* title_id, char* out, size_t cap){
  if (!wh_name_ok(title_id, strlen(title_id))) return 0;
  return appmeta_find(title_id, "icon0.png", out, cap);
}

int wh_apps_json(char** out, size_t* outlen){
  sqlite3* db = appdb_open_ro();
  if (!db) return -1;
  static AppRow rows[WH_APPS_MAX];
  int n = apps_collect(db, rows, WH_APPS_MAX);
  sqlite3_close(db);
  if (n < 0) return -1;
  wh_flog("applist", "app.db: %d titleId candidate(s) with dispLocation=%d", n, APPDB_DISP_GAMES);

  size_t cap = 64 * 1024;
  char* buf = (char*)malloc(cap);
  if (!buf) return -1;
  size_t o = 0;
  buf[o++] = '[';
  int wrote = 0;
  int skip_mount = 0, skip_param = 0, logged = 0;
  for (int i = 0; i < n; i++) {
    char mount[160], param[160];
    snprintf(mount, sizeof mount, "/user/app/%s/mount.lnk", rows[i].id);
    int mount_ok = path_exists(mount);
    int param_ok = appmeta_find(rows[i].id, "param.json", param, sizeof param);
    if (!mount_ok || !param_ok) {
      if (!mount_ok) skip_mount++;
      if (!param_ok) skip_param++;
      if (logged < 5) {   /* a handful of concrete examples is enough to diagnose a wrong path assumption */
        wh_flog("applist", "%s: skipped (mount.lnk %s, param.json %s)", rows[i].id,
          mount_ok ? "found" : "MISSING", param_ok ? "found" : "MISSING");
        logged++;
      }
      continue;   /* known to app.db, not actually installed (or our path assumption is wrong) */
    }
    char* json = 0; size_t jlen = 0;
    char title[256] = "", esc[300];
    if (wh_read_file(param, &json, &jlen) == 0) {
      pick_title(json, "fr-FR", rows[i].id, title, sizeof title);
      free(json);
    } else {
      snprintf(title, sizeof title, "%s", rows[i].id);
    }
    json_escape_into(title, esc, sizeof esc);
    char icon[160];
    int has_icon = wh_app_icon_path(rows[i].id, icon, sizeof icon);
    int n_written = snprintf(buf + o, cap - o, "%s{\"id\":\"%s\",\"title\":\"%s\",\"icon\":%s}",
      wrote ? "," : "", rows[i].id, esc, has_icon ? "true" : "false");
    if (n_written < 0 || (size_t)n_written >= cap - o) break;    /* ran out of room: stop, keep what we have */
    o += (size_t)n_written; wrote++;
  }
  buf[o++] = ']';
  buf[o] = 0;
  *out = buf; *outlen = o;
  wh_flog("applist", "/__apps: %d/%d listed (%d skipped: mount.lnk missing x%d, param.json missing x%d)",
    wrote, n, n - wrote, skip_mount, skip_param);
  return 0;
}
