#pragma once

#include <stddef.h>
#include <stdint.h>

/* Cap on how much of a .meta sidecar we are willing to read. */
#define MEDIA_META_MAX_BYTES 16384

/* Tolerant JSON lookups. These deliberately avoid a JSON library: the SDK build
 * has no third-party dependencies and the sidecar schema is not yet confirmed.
 * All of them return 1 on success and 0 when the key is absent or malformed. */
int media_meta_get_string(const char *json, const char *key, char *out,
                          size_t capacity);
int media_meta_get_int(const char *json, const char *key, long long *out);

/* Reads the largest integer inside an array value, e.g. mediaTime: [[0, 12345]]
 * yields 12345. Falls back to media_meta_get_int for scalar values. */
int media_meta_get_int_in_array(const char *json, const char *key,
                                long long *out);

/* Parses YYYYMMDD_HHMMSS from a PS5 capture filename into a Unix timestamp.
 * Returns 0 when the name does not follow the pattern.
 * NOTE: the filename carries console-local time; the value is produced with
 * mktime() and therefore depends on the process time zone. */
int64_t media_meta_time_from_name(const char *name);

/* Parses already-loaded sidecar text. Unset fields are reported as 0. */
void media_meta_parse(const char *json, int64_t *out_captured_at,
                      uint32_t *out_duration_ms);

/* Derives "<media>.meta", reads it and parses it. Any failure leaves both
 * outputs at 0 — callers fall back to the filename or st_mtime. */
void media_meta_read_file(const char *media_path, int64_t *out_captured_at,
                          uint32_t *out_duration_ms);
