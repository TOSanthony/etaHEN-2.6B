#include "util.h"

void wh_flog(const char* tag, const char* fmt, ...) {
  char msg[600]; va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
  size_t n = strlen(msg); while (n && (msg[n-1] == '\n' || msg[n-1] == '\r')) msg[--n] = 0;
  for (size_t i = 0; i < n; i++) if ((unsigned char)msg[i] < 32) msg[i] = ' ';   /* one line per entry */
  struct stat st;
  if (!stat(WH_LOG_FILE, &st) && st.st_size > WH_LOG_MAX) rename(WH_LOG_FILE, WH_LOG_OLD);
  int fd = open(WH_LOG_FILE, O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW, 0644);
  if (fd < 0) return;
  char ts[32] = "(no clock)"; time_t t = time(0); struct tm tmv;
  if (localtime_r(&t, &tmv)) strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tmv);
  char line[720]; int l = snprintf(line, sizeof line, "%s [%s] %s\n", ts, tag, msg);
  if (l > 0) { ssize_t w = write(fd, line, (size_t)(l < (int)sizeof line ? l : (int)sizeof line - 1)); (void)w; }
  close(fd);
}

void put64(uint8_t *b, uint64_t v) { memcpy(b, &v, 8); }
uint64_t get64(const uint8_t *b) { uint64_t v; memcpy(&v, b, 8); return v; }
uint64_t fnv1a64(const uint8_t *p, size_t n) {
  uint64_t h = 0xcbf29ce484222325ull;
  while (n--) { h ^= *p++; h *= 0x100000001b3ull; }
  return h;
}

typedef struct notify_request { char u[45]; char message[3075]; } notify_request_t;
int sceKernelSendNotificationRequest(int, notify_request_t*, size_t, int);
void notify(const char *fmt, ...) {
  notify_request_t req; va_list ap; bzero(&req, sizeof req);
  va_start(ap, fmt); vsnprintf(req.message, sizeof req.message, fmt, ap); va_end(ap);
  sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}

#define KP_PID_OFF 72
static int scan_needle(const uint8_t *p, int size, const char *n, size_t nl) {
  for (int i = 0; i + (int)nl + 1 <= size; i++)
    if (p[i] == n[0] && !memcmp(p + i, n, nl) && p[i + nl] == '\0') return i;
  return -1;
}
pid_t find_pid(const char *comm) {
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
  pid_t me = getpid(), found = -1; size_t sz, nl = strlen(comm); uint8_t *buf;
  if (sysctl(mib, 4, NULL, &sz, NULL, 0)) return -1;
  if (!(buf = malloc(sz))) return -1;
  if (sysctl(mib, 4, buf, &sz, NULL, 0)) { free(buf); return -1; }
  for (uint8_t *p = buf; p < buf + sz; ) {
    int ss = *(int *)p; if (ss <= 0 || p + ss > buf + sz) break;
    pid_t pid = *(pid_t *)(p + KP_PID_OFF);
    if (scan_needle(p, ss, comm, nl) >= 0 && pid != me) found = pid;
    p += ss;
  }
  free(buf); return found;
}

int wh_name_ok(const char* s, size_t n){
  if (!n || n > 64 || s[0] == '.') return 0;
  for (size_t i = 0; i < n; i++){
    char c = s[i];
    if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-')) return 0;
    if (c == '.' && i+1 < n && s[i+1] == '.') return 0;
  }
  return 1;
}
const char* wh_img_type(const char* f){
  const char* e = strrchr(f, '.');
  if (!e) return 0;
  if (!strcmp(e,".png")) return "image/png";
  if (!strcmp(e,".jpg") || !strcmp(e,".jpeg")) return "image/jpeg";
  return 0;
}

int wh_read_file(const char* path, char** buf, size_t* len){
  int fd = open(path, O_RDONLY);
  if (fd < 0) return -1;
  struct stat st;
  if (fstat(fd, &st) || st.st_size <= 0) { close(fd); return -1; }
  size_t n = (size_t)st.st_size;
  char* b = (char*)malloc(n + 1);
  if (!b) { close(fd); return -1; }
  size_t got = 0; ssize_t r;
  while (got < n && (r = read(fd, b + got, n - got)) > 0) got += (size_t)r;
  close(fd);
  b[got] = 0; *buf = b; *len = got; return 0;
}
int wh_json_str(const char* j, const char* key, char* out, size_t cap){
  char pat[80]; snprintf(pat, sizeof pat, "\"%s\"", key);
  const char* p = strstr(j, pat); if (!p) return -1;
  p += strlen(pat);
  while (*p && *p != ':') p++;
  if (*p != ':') return -1; p++;
  while (*p==' '||*p=='\t'||*p=='\n'||*p=='\r') p++;
  if (*p != '"') return -1; p++;
  size_t i = 0;
  while (*p && *p != '"' && i < cap-1) { if (*p=='\\' && p[1]) p++; out[i++] = *p++; }
  out[i] = 0; return 0;
}
double wh_json_num(const char* j, const char* key, double def){
  char pat[80]; snprintf(pat, sizeof pat, "\"%s\"", key);
  const char* p = strstr(j, pat); if (!p) return def;
  p += strlen(pat);
  while (*p && *p != ':') p++;
  if (*p != ':') return def; p++;
  return strtod(p, 0);
}
