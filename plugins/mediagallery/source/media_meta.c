#include "media_meta.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Longest candidate key we look for, plus quotes and NUL. */
#define KEY_PATTERN_MAX 64

/* Upper bound on how far we scan inside an array value. */
#define ARRAY_SCAN_MAX 512

static int key_pattern(const char *key, char *out, size_t capacity) {
    const int length = snprintf(out, capacity, "\"%s\"", key);
    return length > 0 && (size_t)length < capacity;
}

/* Positions *cursor just past `"key" <ws> : <ws>`. */
static int seek_value(const char *json, const char *key, const char **cursor) {
    char pattern[KEY_PATTERN_MAX];
    if (!json || !key || !cursor) return 0;
    if (!key_pattern(key, pattern, sizeof(pattern))) return 0;

    const char *found = strstr(json, pattern);
    if (!found) return 0;
    found += strlen(pattern);

    while (*found == ' ' || *found == '\t' || *found == '\n' || *found == '\r') {
        found++;
    }
    if (*found != ':') return 0;
    found++;
    while (*found == ' ' || *found == '\t' || *found == '\n' || *found == '\r') {
        found++;
    }

    *cursor = found;
    return 1;
}

int media_meta_get_string(const char *json, const char *key, char *out,
                          size_t capacity) {
    const char *cursor = NULL;
    if (!out || capacity == 0) return 0;
    if (!seek_value(json, key, &cursor)) return 0;
    if (*cursor != '"') return 0;
    cursor++;

    size_t written = 0;
    while (*cursor != '\0' && *cursor != '"' && written + 1 < capacity) {
        if (*cursor == '\\' && cursor[1] != '\0') cursor++;
        out[written++] = *cursor++;
    }
    out[written] = '\0';
    return written > 0;
}

int media_meta_get_int(const char *json, const char *key, long long *out) {
    const char *cursor = NULL;
    if (!out) return 0;
    if (!seek_value(json, key, &cursor)) return 0;

    const char *start = cursor;
    if (*start == '"') start++;

    char *end = NULL;
    const long long value = strtoll(start, &end, 10);
    if (end == start) return 0;

    *out = value;
    return 1;
}

int media_meta_get_int_in_array(const char *json, const char *key,
                                long long *out) {
    const char *cursor = NULL;
    if (!out) return 0;
    if (!seek_value(json, key, &cursor)) return 0;

    if (*cursor != '[') return media_meta_get_int(json, key, out);

    /* mediaTime is observed as [[0, <duration>]]. Take the largest integer in
     * the array so the leading zero does not win. */
    const char *scan = cursor + 1;
    const char *limit = cursor + ARRAY_SCAN_MAX;
    int found = 0;
    long long best = 0;

    while (*scan != '\0' && scan < limit) {
        if (*scan == ']') break;
        if (isdigit((unsigned char)*scan) ||
            (*scan == '-' && isdigit((unsigned char)scan[1]))) {
            char *end = NULL;
            const long long value = strtoll(scan, &end, 10);
            if (end == scan) break;
            if (!found || value > best) best = value;
            found = 1;
            scan = end;
            continue;
        }
        scan++;
    }

    if (!found) return 0;
    *out = best;
    return 1;
}

static int digits_to_int(const char *text, int count) {
    int value = 0;
    for (int i = 0; i < count; ++i) {
        if (!isdigit((unsigned char)text[i])) return -1;
        value = value * 10 + (text[i] - '0');
    }
    return value;
}

int64_t media_meta_time_from_name(const char *name) {
    /* YYYYMMDD_HHMMSS_xxxxxxxx.ext */
    if (!name || strlen(name) < 15) return 0;
    if (name[8] != '_') return 0;

    const int year = digits_to_int(name, 4);
    const int month = digits_to_int(name + 4, 2);
    const int day = digits_to_int(name + 6, 2);
    const int hour = digits_to_int(name + 9, 2);
    const int minute = digits_to_int(name + 11, 2);
    const int second = digits_to_int(name + 13, 2);

    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 ||
        second > 60) {
        return 0;
    }

    struct tm parts;
    memset(&parts, 0, sizeof(parts));
    parts.tm_year = year - 1900;
    parts.tm_mon = month - 1;
    parts.tm_mday = day;
    parts.tm_hour = hour;
    parts.tm_min = minute;
    parts.tm_sec = second;
    parts.tm_isdst = -1;

    const time_t stamp = mktime(&parts);
    return stamp > 0 ? (int64_t)stamp : 0;
}

/* Candidate keys, in priority order. Only unambiguous millisecond keys are
 * used for duration: a bare "duration" could be seconds or milliseconds and
 * guessing wrong is worse than reporting 0. */
static const char *const TIME_KEYS[] = {"start", "ctime", "createdAt",
                                        "captureTime", "timestamp"};
static const char *const DURATION_KEYS[] = {"mediaTime", "durationMs",
                                            "duration_ms"};

void media_meta_parse(const char *json, int64_t *out_captured_at,
                      uint32_t *out_duration_ms) {
    int64_t captured_at = 0;
    uint32_t duration_ms = 0;

    if (json) {
        for (size_t i = 0; i < sizeof(TIME_KEYS) / sizeof(TIME_KEYS[0]); ++i) {
            long long value = 0;
            if (!media_meta_get_int(json, TIME_KEYS[i], &value)) continue;
            /* Values above 1e11 are milliseconds, not seconds. */
            if (value > 100000000000LL) value /= 1000;
            if (value > 0) {
                captured_at = (int64_t)value;
                break;
            }
        }

        for (size_t i = 0; i < sizeof(DURATION_KEYS) / sizeof(DURATION_KEYS[0]);
             ++i) {
            long long value = 0;
            if (!media_meta_get_int_in_array(json, DURATION_KEYS[i], &value))
                continue;
            if (value > 0) {
                duration_ms = value > 0xFFFFFFFFLL ? 0xFFFFFFFFu
                                                   : (uint32_t)value;
                break;
            }
        }
    }

    if (out_captured_at) *out_captured_at = captured_at;
    if (out_duration_ms) *out_duration_ms = duration_ms;
}

/* Replaces the final extension with ".meta". */
static int meta_path_for(const char *media_path, char *out, size_t capacity) {
    const char *dot = strrchr(media_path, '.');
    const char *slash = strrchr(media_path, '/');
    if (dot && slash && dot < slash) dot = NULL;

    const size_t base = dot ? (size_t)(dot - media_path) : strlen(media_path);
    if (base + sizeof(".meta") >= capacity) return 0;

    memcpy(out, media_path, base);
    memcpy(out + base, ".meta", sizeof(".meta"));
    return 1;
}

void media_meta_read_file(const char *media_path, int64_t *out_captured_at,
                          uint32_t *out_duration_ms) {
    if (out_captured_at) *out_captured_at = 0;
    if (out_duration_ms) *out_duration_ms = 0;
    if (!media_path) return;

    char sidecar[512];
    if (!meta_path_for(media_path, sidecar, sizeof(sidecar))) return;

    FILE *file = fopen(sidecar, "r");
    if (!file) return;

    char text[MEDIA_META_MAX_BYTES];
    const size_t read = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[read] = '\0';

    media_meta_parse(text, out_captured_at, out_duration_ms);
}
