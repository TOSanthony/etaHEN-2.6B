#include "server.h"
#include "install.h"
#include "prelude.h"
#include "applist.h"
#include "tiles.h"
#include "util.h"

static void wh_send_all(int c, const void* b, size_t n){
  const char* p = (const char*)b;
  while (n){ ssize_t w = write(c, p, n); if (w <= 0) return; p += w; n -= (size_t)w; }
}
static void wh_reply(int c, const char* status){
  klog_printf("[wc] assets: %s\n", status);
  char h[160]; int n = snprintf(h, sizeof h, "HTTP/1.0 %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", status);
  wh_send_all(c, h, (size_t)n);
}
static int wh_hexv(char c){ return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; }
static void wh_urldecode(const char* in, char* out, size_t cap){
  size_t o = 0;
  for (; *in && *in != '&' && o + 1 < cap; in++) {
    char c = *in;
    if (c == '+') c = ' ';
    else if (c == '%' && wh_hexv(in[1]) >= 0 && wh_hexv(in[2]) >= 0) { c = (char)(wh_hexv(in[1])*16 + wh_hexv(in[2])); in += 2; }
    out[o++] = ((unsigned char)c < 32 || c == 127) ? ' ' : c;
  }
  out[o] = 0;
}
/* GET /__log?w=<widget>&m=<msg> from the hub loader -> one line in wh.log.
 * Append-only, 400-char cap, control chars stripped, <=120 lines/minute. */
static void wh_hexdecode(const char* in, char* out, size_t cap){
  size_t o = 0;
  while (in[0] && in[0] != '&' && in[1] && in[1] != '&' && o + 1 < cap) {
    int hi = wh_hexv(in[0]), lo = wh_hexv(in[1]);
    if (hi < 0 || lo < 0) break;
    char ch = (char)(hi * 16 + lo); in += 2;
    out[o++] = ((unsigned char)ch < 32 || ch == 127) ? ' ' : ch;
  }
  out[o] = 0;
}
static char g_fseen[16][72]; static int g_fns = 0;   /* widgets already notified this hub load */
static void wh_fail_notify(const char* w, const char* m){
  char (*seen)[72] = g_fseen; int ns = g_fns;
  const char* key = w[0] ? w : "*";
  for (int i = 0; i < ns; i++) if (!strcmp(seen[i], key)) return;
  if (ns >= 16) return;
  snprintf(seen[ns], sizeof seen[0], "%s", key); g_fns = ns + 1;
  if (w[0]) notify("Widget failed: %s\n%.160s\nDetails: " WH_WIDGETS_DIR "/wh.log", w, m);
  else notify("Widgets failed to load\n%.160s\nDetails: " WH_WIDGETS_DIR "/wh.log", m);
}
static void wh_log_endpoint(int c, char* q){
  static time_t win = 0; static int cnt = 0, dropped = 0;
  time_t now = time(0);
  if (now - win >= 60) { if (dropped) wh_flog("assets", "log rate limit: dropped %d lines", dropped); win = now; cnt = 0; dropped = 0; }
  if (++cnt > 120) { dropped++; wh_reply(c, "204 No Content"); return; }
  char w[72] = "", m[401] = ""; int fatal = 0;
  for (char* t = q; t && *t; ) {
    if (!strncmp(t, "f=1", 3)) fatal = 1;
    if (!strncmp(t, "w=", 2)) wh_hexdecode(t + 2, w, sizeof w);      /* hex (loader) */
    else if (!strncmp(t, "h=", 2)) wh_hexdecode(t + 2, m, sizeof m);  /* hex (loader) */
    else if (!strncmp(t, "m=", 2)) wh_urldecode(t + 2, m, sizeof m);  /* url-encoded (manual) */
    t = strchr(t, '&'); if (t) t++;
  }
  char tag[96] = "hub";
  if (w[0] && wh_name_ok(w, strlen(w))) snprintf(tag, sizeof tag, "widget:%s", w);
  if (m[0]) wh_flog(tag, "%s", m);
  if (!strncmp(m, "loader ready in the hub runtime", 31)) g_fns = 0;   /* new hub load: notify again */
  if (fatal && m[0]) wh_fail_notify(w[0] && wh_name_ok(w, strlen(w)) ? w : "", m);
  wh_reply(c, "204 No Content");
}
#define WH_SET_MAX 2048
#define WH_SET_FILE ".settings.json"
/* GET /__wget?w=<id> -> that widget's saved settings JSON (loopback only). */
static void wh_settings_get(int c, char* q){
  char w[72] = "";
  for (char* t = q; t && *t; ) {
    if (!strncmp(t, "w=", 2)) { size_t o = 0; for (char* u = t + 2; *u && *u != '&' && o + 1 < sizeof w; u++) w[o++] = *u; w[o] = 0; }
    t = strchr(t, '&'); if (t) t++;
  }
  char body[WH_SET_MAX + 1] = "{}"; long blen = 2;
  if (w[0] && wh_name_ok(w, strlen(w))) {
    char fp[256]; snprintf(fp, sizeof fp, "%s/%s/%s", WH_WIDGETS_DIR, w, WH_SET_FILE);
    int fd = open(fp, O_RDONLY | O_NOFOLLOW);
    if (fd >= 0) {
      struct stat st;
      if (!fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_size > 0 && st.st_size <= WH_SET_MAX) {
        ssize_t r = read(fd, body, (size_t)st.st_size); if (r > 0) { body[r] = 0; blen = r; }
      }
      close(fd);
    }
  }
  char h[200]; int n = snprintf(h, sizeof h,
    "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n", blen);
  wh_send_all(c, h, (size_t)n); wh_send_all(c, body, (size_t)blen);
}
/* GET /__wset?w=<id>&h=<hex json> -> write the widget's settings (atomic, <=2 KB, 120/min). */
static void wh_settings_set(int c, char* q){
  static time_t win = 0; static int cnt = 0;
  time_t now = time(0);
  if (now - win >= 60) { win = now; cnt = 0; }
  if (++cnt > 120) { wh_reply(c, "204 No Content"); return; }
  char w[72] = ""; char body[WH_SET_MAX + 1] = "";
  for (char* t = q; t && *t; ) {
    if (!strncmp(t, "w=", 2)) { size_t o = 0; for (char* u = t + 2; *u && *u != '&' && o + 1 < sizeof w; u++) w[o++] = *u; w[o] = 0; }
    else if (!strncmp(t, "h=", 2)) wh_hexdecode(t + 2, body, sizeof body);
    t = strchr(t, '&'); if (t) t++;
  }
  if (!w[0] || !wh_name_ok(w, strlen(w)) || !body[0] || body[0] != '{') { wh_reply(c, "204 No Content"); return; }
  char fp[256], tp[300];
  snprintf(fp, sizeof fp, "%s/%s/%s", WH_WIDGETS_DIR, w, WH_SET_FILE);
  snprintf(tp, sizeof tp, "%s.tmp", fp);
  int fd = open(tp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
  if (fd >= 0) {
    size_t bl = strlen(body); int ok = (write(fd, body, bl) == (ssize_t)bl);
    close(fd);
    if (ok && rename(tp, fp) == 0) wh_flog("assets", "saved settings for %s (%zuB)", w, bl);
    else { unlink(tp); wh_flog("assets", "settings save failed for %s", w); }
  }
  wh_reply(c, "204 No Content");
}
/* Sends a 200 with the given Content-Type and the whole content of `fd`
 * (already known-good: regular file, size st_size), then closes fd. Shared
 * by the widget-asset path below and the app-icon endpoint. */
static void wh_send_file(int c, int fd, const char* type, long size){
  char h[200]; int n = snprintf(h, sizeof h,
    "HTTP/1.0 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\nCache-Control: max-age=60\r\nConnection: close\r\n\r\n",
    type, size);
  wh_send_all(c, h, (size_t)n);
  char buf[8192]; ssize_t r;
  while ((r = read(fd, buf, sizeof buf)) > 0) wh_send_all(c, buf, (size_t)r);
  close(fd);
}
/* GET /__apps -> JSON list of installed games (see applist.h). */
static void wh_apps_endpoint(int c){
  char* body = 0; size_t blen = 0;
  if (wh_apps_json(&body, &blen) != 0) {
    wh_flog("assets", "refused /__apps: app.db unreadable");
    wh_reply(c, "500 Internal Server Error");
    return;
  }
  char h[200]; int n = snprintf(h, sizeof h,
    "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", blen);
  klog_printf("[wc] assets: 200 /__apps (%zuB)\n", blen);
  wh_send_all(c, h, (size_t)n); wh_send_all(c, body, blen);
  free(body);
}
/* GET /__app_icon?id=<titleId> -> that title's icon0.png, straight off disk
 * (see applist.h for the path and how confident we are in it). */
static void wh_app_icon_endpoint(int c, char* q){
  char id[72] = "";
  for (char* t = q; t && *t; ) {
    if (!strncmp(t, "id=", 3)) { size_t o = 0; for (char* u = t + 3; *u && *u != '&' && o + 1 < sizeof id; u++) id[o++] = *u; id[o] = 0; }
    t = strchr(t, '&'); if (t) t++;
  }
  char path[160];
  if (!id[0] || !wh_app_icon_path(id, path, sizeof path)) {
    wh_flog("assets", "refused /__app_icon: no icon for id=%.60s", id);
    wh_reply(c, "404 Not Found");
    return;
  }
  int fd = open(path, O_RDONLY | O_NOFOLLOW);
  if (fd < 0) { wh_reply(c, "404 Not Found"); return; }
  struct stat st;
  if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > WH_ASSET_MAX) {
    close(fd); wh_reply(c, "404 Not Found"); return;
  }
  klog_printf("[wc] assets: 200 /__app_icon?id=%s %ldB\n", id, (long)st.st_size);
  wh_send_file(c, fd, "image/png", (long)st.st_size);
}
static void wh_asset_conn(int c){
  char req[1024]; size_t got = 0;
  while (got < sizeof req - 1){
    ssize_t r = read(c, req + got, sizeof req - 1 - got);
    if (r <= 0) break;
    got += (size_t)r; req[got] = 0;
    if (strstr(req, "\r\n")) break;          /* only the request line matters */
  }
  req[got] = 0;
  char* eol = strstr(req, "\r\n");
  if (!eol) { wh_reply(c, "400 Bad Request"); return; }
  *eol = 0;
  if (strncmp(req, "GET /", 5)) { wh_reply(c, "405 Method Not Allowed"); return; }
  char* path = req + 5;                      /* after "GET /" */
  char* sp = strchr(path, ' '); if (sp) *sp = 0;
  if (!strncmp(path, "__log?", 6)) { wh_log_endpoint(c, path + 6); return; }
  if (!strncmp(path, "__wget?", 7)) { wh_settings_get(c, path + 7); return; }
  if (!strncmp(path, "__wset?", 7)) { wh_settings_set(c, path + 7); return; }
  if (!strcmp(path, "__apps")) { wh_apps_endpoint(c); return; }
  if (!strncmp(path, "__app_icon?", 11)) { wh_app_icon_endpoint(c, path + 11); return; }
  char shown[100]; snprintf(shown, sizeof shown, "%s", path);   /* for refused-request log lines */
  char* q = strchr(path, '?');  if (q) *q = 0;
  char* sl = strchr(path, '/');
  if (!sl || strchr(sl + 1, '/')) { wh_flog("assets", "refused /%s: path is not /<widget>/<file>", shown); wh_reply(c, "404 Not Found"); return; }
  size_t dl = (size_t)(sl - path), fl = strlen(sl + 1);
  if (!wh_name_ok(path, dl) || !wh_name_ok(sl + 1, fl)) { wh_flog("assets", "refused /%s: invalid characters in name", shown); wh_reply(c, "404 Not Found"); return; }
  const char* type = wh_img_type(sl + 1);
  if (!type) { wh_flog("assets", "refused /%s: not a .png/.jpg image", shown); wh_reply(c, "404 Not Found"); return; }
  *sl = 0;
  char fp[256]; snprintf(fp, sizeof fp, "%s/%s/%s", WH_WIDGETS_DIR, path, sl + 1);
  int fd = open(fp, O_RDONLY | O_NOFOLLOW);
  if (fd < 0) { wh_flog("assets", "refused /%s: file not found", shown); wh_reply(c, "404 Not Found"); return; }
  struct stat st;
  if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > WH_ASSET_MAX) {
    close(fd); wh_flog("assets", "refused /%s: not a regular file or over 1 MB", shown); wh_reply(c, "404 Not Found"); return;
  }
  klog_printf("[wc] assets: 200 /%s/%s %ldB\n", path, sl + 1, (long)st.st_size);
  wh_send_file(c, fd, type, (long)st.st_size);
}
static unsigned wh_gen_read(void){
  char b[32] = {0}; int fd = open(WH_GEN_FILE, O_RDONLY | O_NOFOLLOW);
  if (fd < 0) return 0;
  ssize_t r = read(fd, b, sizeof b - 1); close(fd);
  return r > 0 ? (unsigned)strtoul(b, 0, 10) : 0;
}
static unsigned wh_gen_bump(void){
  unsigned g = wh_gen_read() + 1;
  int fd = open(WH_GEN_FILE, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
  if (fd < 0) return 0;                      /* can't write: never retire on a read */
  char b[32]; int n = snprintf(b, sizeof b, "%u\n", g);
  ssize_t w = write(fd, b, (size_t)n); close(fd);
  return w == n ? g : 0;
}
/* Bind the asset port. Returns the listening socket, or -1 with *why set. */
int wh_asset_open(unsigned* gen, const char** why){
  *gen = 0; *why = "failed";
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) { klog_printf("[wc] assets: socket failed\n"); return -1; }
  int one = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  struct sockaddr_in a; memset(&a, 0, sizeof a);
  a.sin_family = AF_INET; a.sin_port = htons(WH_ASSET_PORT); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  unsigned my = wh_gen_bump();               /* tells an older server to retire */
  *gen = my;
  int bound = 0;
  for (int t = 0; t < 30 && !bound; t++) {   /* ~6 s for it to notice (it polls 1/s) */
    if (!bind(s, (struct sockaddr*)&a, sizeof a) && !listen(s, 8)) bound = 1;
    else usleep(200000);
  }
  if (!bound) {
    LOG("assets: 127.0.0.1:%d still busy, not serving\n", WH_ASSET_PORT);
    close(s); *why = "port busy"; return -1;
  }
  LOG("assets: serving %s on 127.0.0.1:%d (gen %u)\n", WH_WIDGETS_DIR, WH_ASSET_PORT, my);
  return s;
}
/* Cheap "did a widget change?" check: builds the prelude (pure file I/O --
 * no ptrace) just to read off its generation hash, then throws the JS text
 * away. Safe to call every few seconds from the resident loop. */
static uint64_t wh_watch_rehash(void){
  char* tmp = 0; size_t tlen = 0; uint64_t gen = 0;
  build_prelude(&tmp, &tlen, &gen);
  free(tmp);
  return gen;
}
/* Re-finds SceShellUI (it can respawn) and re-runs the full install so the
 * fresh widget content actually reaches it. Logs/notifies either way; never
 * fatal to the asset server if it fails (next tick tries again). */
static void wh_watch_reinstall(uint64_t new_gen, uint64_t* cur_gen){
  pid_t pid = find_pid("SceShellUI");
  if (pid < 0) { wh_flog("watch", "widgets changed but SceShellUI not found, will retry"); return; }
  int reload = 0; uint64_t installed_gen = 0;
  int rc = wh_install_once_g(pid, 0, &reload, &installed_gen);
  if (rc == 0) {
    *cur_gen = installed_gen;
    wh_flog("watch", "widgets changed on disk: re-installed (gen %016llx)", (unsigned long long)installed_gen);
    notify("WHBridge: widgets refreshed\nChange detected and re-installed automatically.");
  } else {
    wh_flog("watch", "widgets changed on disk but re-install failed (rc=%d), will retry", rc);
    /* Don't update *cur_gen: keep retrying every tick until it succeeds,
     * rather than silently giving up on this change. new_gen is unused on
     * this path on purpose. */
    (void)new_gen;
  }
}
/* Serve until a newer install bumps the generation; never returns. */
void wh_asset_loop(int s, unsigned my, pid_t target, uint64_t start_gen){
  signal(SIGPIPE, SIG_IGN);
  fflush(stdout); fflush(stderr);
  int dn = open("/dev/null", O_RDWR);        /* detach stdio so the loader connection ends */
  if (dn >= 0) { dup2(dn, 0); dup2(dn, 1); dup2(dn, 2); if (dn > 2) close(dn); }
  struct timeval tv = { 2, 0 };
  (void)target;   /* kept in the signature for clarity; the watcher re-resolves the pid itself */
  uint64_t cur_gen = start_gen;
  time_t last_watch = time(0);
  for (;;) {
    struct pollfd pf = { s, POLLIN, 0 };
    int pr = poll(&pf, 1, 1000);
    if (my && wh_gen_read() > my) {          /* a newer install took over */
      klog_printf("[wc] assets: gen %u superseded, retiring\n", my);
      wh_flog("assets", "server gen %u handed over to a newer install, exiting", my);
      close(s); _exit(0);
    }
    time_t now = time(0);
    if (now - last_watch >= WH_WATCH_SECS) {
      last_watch = now;
      uint64_t h = wh_watch_rehash();
      if (h != cur_gen) wh_watch_reinstall(h, &cur_gen);
      wh_orbit_watch_tick();
    }
    if (pr <= 0) continue;
    int c = accept(s, 0, 0);
    if (c < 0) { if (errno == EINTR) continue; usleep(100000); continue; }
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    wh_asset_conn(c);
    close(c);
  }
}
