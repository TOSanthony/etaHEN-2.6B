/* util.h -- log file, notifications, process lookup, byte/JSON helpers. */
#ifndef WH_UTIL_H
#define WH_UTIL_H

#include "common.h"

#define WH_LOG_FILE  "/data/wh-widgets/wh.log"
#define WH_LOG_OLD   "/data/wh-widgets/wh.log.1"
#define WH_LOG_MAX   (256*1024)

/* Appends one line to wh.log (rotating it past WH_LOG_MAX), tagged and
 * timestamped. Prefer the LOG(...) macro below over calling this directly. */
void wh_flog(const char* tag, const char* fmt, ...);

/* Logs to the PS5 kernel log, stdout, and wh.log all at once. */
#define LOG(...) do { char _lb[600]; snprintf(_lb, sizeof _lb, __VA_ARGS__); \
  klog_printf("[wc] %s", _lb); printf("[wc] %s", _lb); wh_flog("installer", "%s", _lb); } while (0)

/* Little-endian 64-bit read/write into a byte buffer (used to lay out the
 * code cave -- see cave.h). */
void put64(uint8_t *b, uint64_t v);
uint64_t get64(const uint8_t *b);

/* FNV-1a 64-bit hash, used to fingerprint the embedded bridge. */
uint64_t fnv1a64(const uint8_t *p, size_t n);

/* Sends a PS5 OS notification popup. */
void notify(const char *fmt, ...);

/* Finds the pid of the first running process whose name matches `comm`
 * (SceShellUI, typically). Returns -1 if not found. */
pid_t find_pid(const char *comm);

/* Validates a widget-folder-relative file name: no path traversal, only
 * [A-Za-z0-9._-]. Returns 1 if safe to serve/reference, 0 otherwise. */
int wh_name_ok(const char* s, size_t n);

/* Returns the MIME type for a glyph/image file name (by extension), or NULL
 * if it's not an image type this payload will serve. */
const char* wh_img_type(const char* f);

/* Reads a whole file into a malloc'd, NUL-terminated buffer. Returns 0 on
 * success (caller frees *buf), -1 on failure. */
int wh_read_file(const char* path, char** buf, size_t* len);

/* Minimal JSON helpers for reading values out of a widget's config.json
 * without a full JSON parser. */
int wh_json_str(const char* j, const char* key, char* out, size_t cap);
double wh_json_num(const char* j, const char* key, double def);

#endif /* WH_UTIL_H */
