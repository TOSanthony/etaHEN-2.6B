#include "http_server.h"
#include "gallery_page.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "plugin_config.h"

#define HTTP_REQUEST_MAX     8192
#define HTTP_TARGET_MAX      1024
#define HTTP_METHOD_MAX      16
#define HTTP_HEADER_MAX      256
#define HTTP_STREAM_CHUNK    (64 * 1024)
#define HTTP_MAX_CONNECTIONS 16
#define HTTP_ACCEPT_POLL_MS  500
#define HTTP_IO_TIMEOUT_SEC  30
#define HTTP_DEFAULT_LIMIT   100
#define HTTP_MAX_LIMIT       500

typedef struct http_connection {
    http_service *service;
    int           fd;
} http_connection;

typedef struct http_request {
    http_service *service;
    int           fd;
    const char   *raw;
    char          method[HTTP_METHOD_MAX];
    char          target[HTTP_TARGET_MAX];
} http_request;

/* ── Response buffer ─────────────────────────────────────────────────────── */

int http_buffer_init(http_buffer *buffer) {
    if (!buffer) return 0;
    buffer->capacity = 4096;
    buffer->length = 0;
    buffer->data = malloc(buffer->capacity);
    if (!buffer->data) {
        buffer->capacity = 0;
        return 0;
    }
    buffer->data[0] = '\0';
    return 1;
}

void http_buffer_free(http_buffer *buffer) {
    if (!buffer) return;
    free(buffer->data);
    buffer->data = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
}

static int buffer_reserve(http_buffer *buffer, size_t extra) {
    if (buffer->length + extra + 1 <= buffer->capacity) return 1;

    size_t capacity = buffer->capacity ? buffer->capacity : 4096;
    while (capacity < buffer->length + extra + 1) capacity *= 2;

    char *data = realloc(buffer->data, capacity);
    if (!data) return 0;
    buffer->data = data;
    buffer->capacity = capacity;
    return 1;
}

int http_buffer_append(http_buffer *buffer, const char *text) {
    if (!buffer || !text) return 0;
    const size_t length = strlen(text);
    if (!buffer_reserve(buffer, length)) return 0;
    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return 1;
}

int http_buffer_appendf(http_buffer *buffer, const char *format, ...) {
    if (!buffer || !format) return 0;

    char stack[512];
    va_list arguments;
    va_start(arguments, format);
    const int needed = vsnprintf(stack, sizeof(stack), format, arguments);
    va_end(arguments);
    if (needed < 0) return 0;
    if ((size_t)needed < sizeof(stack)) return http_buffer_append(buffer, stack);

    if (!buffer_reserve(buffer, (size_t)needed)) return 0;
    va_start(arguments, format);
    vsnprintf(buffer->data + buffer->length, (size_t)needed + 1, format,
              arguments);
    va_end(arguments);
    buffer->length += (size_t)needed;
    return 1;
}

int http_buffer_append_json_string(http_buffer *buffer, const char *text) {
    static const char HEX[] = "0123456789abcdef";
    if (!http_buffer_append(buffer, "\"")) return 0;

    for (const unsigned char *cursor = (const unsigned char *)text;
         cursor && *cursor; ++cursor) {
        const unsigned char character = *cursor;
        if (character == '"' || character == '\\') {
            char pair[3] = {'\\', (char)character, '\0'};
            if (!http_buffer_append(buffer, pair)) return 0;
        } else if (character == '\n' || character == '\r' ||
                   character == '\t') {
            const char escape[3] = {'\\',
                                    character == '\n'   ? 'n'
                                    : character == '\r' ? 'r'
                                                        : 't',
                                    '\0'};
            if (!http_buffer_append(buffer, escape)) return 0;
        } else if (character < 0x20) {
            char escape[7] = {'\\', 'u', '0', '0', HEX[(character >> 4) & 0xF],
                              HEX[character & 0xF], '\0'};
            if (!http_buffer_append(buffer, escape)) return 0;
        } else {
            const char single[2] = {(char)character, '\0'};
            if (!http_buffer_append(buffer, single)) return 0;
        }
    }
    return http_buffer_append(buffer, "\"");
}

/* ── Request parsing helpers ─────────────────────────────────────────────── */

int http_parse_range(const char *value, uint64_t size, uint64_t *start,
                     uint64_t *end) {
    if (!value || !start || !end || size == 0) return 0;
    if (strncasecmp(value, "bytes=", 6) != 0) return 0;

    const char *cursor = value + 6;
    if (*cursor == '\0') return 0;

    /* Suffix form: "bytes=-N" means the last N bytes. */
    if (*cursor == '-') {
        char *tail = NULL;
        const long long suffix = strtoll(cursor + 1, &tail, 10);
        if (tail == cursor + 1 || *tail != '\0' || suffix <= 0) return 0;
        if ((uint64_t)suffix >= size) {
            *start = 0;
        } else {
            *start = size - (uint64_t)suffix;
        }
        *end = size - 1;
        return 1;
    }

    char *tail = NULL;
    const long long first = strtoll(cursor, &tail, 10);
    if (tail == cursor || first < 0) return 0;
    if ((uint64_t)first >= size) return -1;

    uint64_t last = size - 1;
    if (*tail == '-') {
        if (tail[1] != '\0') {
            char *end_ptr = NULL;
            const long long second = strtoll(tail + 1, &end_ptr, 10);
            if (end_ptr == tail + 1 || *end_ptr != '\0' || second < 0) return 0;
            last = (uint64_t)second;
            if (last >= size) last = size - 1;
        }
    } else if (*tail != '\0') {
        return 0;
    }

    if (last < (uint64_t)first) return -1;
    *start = (uint64_t)first;
    *end = last;
    return 1;
}

static int hex_value(int character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

int http_url_decode(const char *input, char *output, size_t capacity) {
    if (!input || !output || capacity == 0) return 0;

    size_t written = 0;
    for (const char *cursor = input; *cursor != '\0'; ++cursor) {
        if (written + 1 >= capacity) return 0;
        if (*cursor == '%' && cursor[1] != '\0' && cursor[2] != '\0') {
            const int high = hex_value(cursor[1]);
            const int low = hex_value(cursor[2]);
            /* A well-formed escape is decoded; anything else is copied
             * verbatim. Rejecting the whole value would silently drop a query
             * filter and turn a client typo into an unfiltered result set,
             * which is a worse failure than passing the raw bytes through.
             * Decoded values are only ever compared against index fields, never
             * used as paths, so this cannot reach the filesystem. */
            if (high >= 0 && low >= 0) {
                output[written++] = (char)((high << 4) | low);
                cursor += 2;
                continue;
            }
        }
        output[written++] = *cursor;
    }
    output[written] = '\0';
    return 1;
}

int http_query_param(const char *target, const char *name, char *out,
                     size_t capacity) {
    if (!target || !name || !out || capacity == 0) return 0;

    const char *query = strchr(target, '?');
    if (!query) return 0;
    query++;

    const size_t name_length = strlen(name);
    const char *cursor = query;
    while (*cursor != '\0') {
        const char *separator = strchr(cursor, '&');
        const char *end =
            separator ? separator : cursor + strlen(cursor);
        const char *equals = memchr(cursor, '=', (size_t)(end - cursor));

        if (equals != NULL && (size_t)(equals - cursor) == name_length &&
            strncmp(cursor, name, name_length) == 0) {
            const size_t value_length = (size_t)(end - equals - 1);
            char encoded[HTTP_HEADER_MAX];
            if (value_length >= sizeof(encoded)) return 0;
            memcpy(encoded, equals + 1, value_length);
            encoded[value_length] = '\0';
            return http_url_decode(encoded, out, capacity);
        }

        if (!separator) break;
        cursor = separator + 1;
    }
    return 0;
}

int http_path_segment(const char *target, const char *prefix, char *out,
                      size_t capacity) {
    if (!target || !prefix || !out || capacity == 0) return 0;

    const size_t prefix_length = strlen(prefix);
    if (strncmp(target, prefix, prefix_length) != 0) return 0;

    const char *segment = target + prefix_length;
    const char *query = strchr(segment, '?');
    const size_t length = query ? (size_t)(query - segment) : strlen(segment);
    if (length == 0 || length >= capacity) return 0;

    memcpy(out, segment, length);
    out[length] = '\0';
    return strchr(out, '/') == NULL;
}

static int http_header_value(const char *request, const char *name, char *out,
                             size_t capacity) {
    if (!request || !name || !out || capacity == 0) return 0;

    const size_t name_length = strlen(name);
    const char *line = strstr(request, "\r\n");
    if (!line) return 0;
    line += 2;

    while (*line != '\0') {
        const char *end = strstr(line, "\r\n");
        if (!end || end == line) break;

        if (strncasecmp(line, name, name_length) == 0 &&
            line[name_length] == ':') {
            const char *value = line + name_length + 1;
            while (value < end && (*value == ' ' || *value == '\t')) value++;
            const char *tail = end;
            while (tail > value && (tail[-1] == ' ' || tail[-1] == '\t')) tail--;
            const size_t length = (size_t)(tail - value);
            if (length == 0 || length >= capacity) return 0;
            memcpy(out, value, length);
            out[length] = '\0';
            return 1;
        }
        line = end + 2;
    }
    return 0;
}

/* ── Socket writes ───────────────────────────────────────────────────────── */

static int send_all(int fd, const void *data, size_t length) {
    const char *cursor = data;
    size_t remaining = length;
    while (remaining > 0) {
        const ssize_t sent = send(fd, cursor, remaining, 0);
        if (sent <= 0) return 0;
        cursor += sent;
        remaining -= (size_t)sent;
    }
    return 1;
}

static void send_headers(int fd, int status, const char *status_text,
                         const char *content_type, size_t length,
                         const char *extra) {
    char header[768];
    const int written = snprintf(
        header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Headers: Range, Content-Type\r\n"
        "Access-Control-Expose-Headers: Content-Range, Accept-Ranges, "
        "Content-Length\r\n"
        "Cache-Control: no-store\r\n"
        "%s"
        "Connection: close\r\n"
        "\r\n",
        status, status_text, content_type, length, extra ? extra : "");
    if (written <= 0 || (size_t)written >= sizeof(header)) return;
    (void)send_all(fd, header, (size_t)written);
}

static void send_body(int fd, int status, const char *status_text,
                      const char *content_type, const char *body,
                      size_t length) {
    send_headers(fd, status, status_text, content_type, length, NULL);
    if (length > 0 && body) (void)send_all(fd, body, length);
}

static void send_json(int fd, int status, const char *status_text,
                      const http_buffer *body) {
    send_body(fd, status, status_text, "application/json; charset=utf-8",
              body->data ? body->data : "", body->length);
}

static void send_error(int fd, int status, const char *status_text,
                       const char *code, const char *message) {
    http_buffer body;
    if (!http_buffer_init(&body)) {
        static const char FALLBACK[] = "{\"error\":{\"code\":\"internal\"}}";
        send_body(fd, 500, "Internal Server Error",
                  "application/json; charset=utf-8", FALLBACK,
                  sizeof(FALLBACK) - 1);
        return;
    }
    (void)http_buffer_append(&body, "{\"error\":{\"code\":");
    (void)http_buffer_append_json_string(&body, code);
    (void)http_buffer_append(&body, ",\"message\":");
    (void)http_buffer_append_json_string(&body, message ? message : "");
    (void)http_buffer_append(&body, "}}");
    send_json(fd, status, status_text, &body);
    http_buffer_free(&body);
}

/* ── Route helpers ───────────────────────────────────────────────────────── */

static int media_path_allowed(const char *path) {
    if (!path) return 0;
    const size_t root_length = strlen(PLUGIN_MEDIA_ROOT);
    if (strncmp(path, PLUGIN_MEDIA_ROOT "/", root_length + 1) != 0) return 0;
    if (strstr(path, "/../") != NULL) return 0;
    if (strstr(path, "/./") != NULL) return 0;
    return 1;
}

static int media_thumb_path(const char *media_path, char *out,
                            size_t capacity) {
    const char *dot = strrchr(media_path, '.');
    const char *slash = strrchr(media_path, '/');
    if (dot && slash && dot < slash) dot = NULL;

    const size_t base = dot ? (size_t)(dot - media_path) : strlen(media_path);
    if (base + sizeof(".jxr") >= capacity) return 0;

    memcpy(out, media_path, base);
    memcpy(out + base, ".jxr", sizeof(".jxr"));
    return 1;
}

static const char *content_type_for(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0)
        return "image/jpeg";
    if (strcasecmp(dot, ".png") == 0) return "image/png";
    if (strcasecmp(dot, ".jxr") == 0) return "image/jxr";
    if (strcasecmp(dot, ".webm") == 0) return "video/webm";
    if (strcasecmp(dot, ".mp4") == 0) return "video/mp4";
    return "application/octet-stream";
}

/* ── Handlers ────────────────────────────────────────────────────────────── */

static void handle_health(const http_request *request) {
    const media_store *store = request->service->store;
    media_index *index = media_store_acquire(request->service->store);

    const size_t indexed = index ? index->count : 0;
    const size_t photos = index ? index->photo_count : 0;
    const size_t videos = index ? index->video_count : 0;
    const unsigned skipped = index ? index->skipped_dirs : 0;
    const int truncated = index ? index->truncated : 0;
    const int64_t indexed_at = index ? index->indexed_at : 0;
    media_store_release(request->service->store);

    const int running = http_service_running(request->service);
    const int privileged = store->privileged;

    http_buffer body;
    if (!http_buffer_init(&body)) {
        send_error(request->fd, 500, "Internal Server Error", "internal",
                   "out of memory");
        return;
    }

    (void)http_buffer_append(&body, "{\"status\":");
    (void)http_buffer_append_json_string(
        &body, (running && privileged) ? "ok" : "degraded");
    (void)http_buffer_append(&body, ",\"plugin\":");
    (void)http_buffer_append_json_string(&body, PLUGIN_ID);
    (void)http_buffer_append(&body, ",\"version\":");
    (void)http_buffer_append_json_string(&body, PLUGIN_VERSION);
    (void)http_buffer_appendf(&body, ",\"privileged\":%s",
                              privileged ? "true" : "false");
    (void)http_buffer_appendf(&body, ",\"running\":%s",
                              running ? "true" : "false");
    (void)http_buffer_appendf(&body, ",\"indexed\":%zu", indexed);
    (void)http_buffer_appendf(&body, ",\"photos\":%zu", photos);
    (void)http_buffer_appendf(&body, ",\"videos\":%zu", videos);
    (void)http_buffer_appendf(&body, ",\"truncated\":%s",
                              truncated ? "true" : "false");
    (void)http_buffer_appendf(&body, ",\"skipped_dirs\":%u", skipped);
    (void)http_buffer_appendf(&body, ",\"indexed_at\":%lld",
                              (long long)indexed_at);
    (void)http_buffer_append(&body, ",\"media_root\":");
    (void)http_buffer_append_json_string(&body, PLUGIN_MEDIA_ROOT);
    (void)http_buffer_append(&body, "}");

    send_json(request->fd, 200, "OK", &body);
    http_buffer_free(&body);
}

enum {
    SORT_DATE_DESC = 0,
    SORT_DATE_ASC,
    SORT_NAME_ASC,
    SORT_SIZE_DESC
};

static int parse_sort_mode(const char *value) {
    if (strcmp(value, "date_asc") == 0) return SORT_DATE_ASC;
    if (strcmp(value, "name_asc") == 0) return SORT_NAME_ASC;
    if (strcmp(value, "size_desc") == 0) return SORT_SIZE_DESC;
    return SORT_DATE_DESC;
}

static int parse_bounded_size(const char *value, size_t maximum,
                              size_t *out) {
    char *end = NULL;
    errno = 0;
    const long long parsed = strtoll(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed < 0) return 0;
    *out = (size_t)parsed > maximum ? maximum : (size_t)parsed;
    return 1;
}

static int compare_date_asc(const void *left, const void *right) {
    const media_entry *a = *(const media_entry *const *)left;
    const media_entry *b = *(const media_entry *const *)right;
    if (a->captured_at != b->captured_at)
        return a->captured_at < b->captured_at ? -1 : 1;
    return strcmp(a->name, b->name);
}

static int compare_name_asc(const void *left, const void *right) {
    const media_entry *a = *(const media_entry *const *)left;
    const media_entry *b = *(const media_entry *const *)right;
    return strcmp(a->name, b->name);
}

static int compare_size_desc(const void *left, const void *right) {
    const media_entry *a = *(const media_entry *const *)left;
    const media_entry *b = *(const media_entry *const *)right;
    if (a->size != b->size) return a->size > b->size ? -1 : 1;
    return strcmp(a->name, b->name);
}

static void handle_media_list(const http_request *request) {
    char value[HTTP_HEADER_MAX];

    int kind = -1;
    if (http_query_param(request->target, "type", value, sizeof(value))) {
        if (strcmp(value, "photo") == 0) kind = MEDIA_KIND_PHOTO;
        else if (strcmp(value, "video") == 0) kind = MEDIA_KIND_VIDEO;
        else if (strcmp(value, "all") != 0) {
            send_error(request->fd, 400, "Bad Request", "bad_request",
                       "type must be one of all, photo, video");
            return;
        }
    }

    char game[MEDIA_GAME_ID_MAX] = {0};
    if (http_query_param(request->target, "game", value, sizeof(value))) {
        snprintf(game, sizeof(game), "%s", value);
    }

    size_t limit = HTTP_DEFAULT_LIMIT;
    if (http_query_param(request->target, "limit", value, sizeof(value)) &&
        !parse_bounded_size(value, HTTP_MAX_LIMIT, &limit)) {
        send_error(request->fd, 400, "Bad Request", "bad_request",
                   "limit must be a non-negative integer");
        return;
    }

    size_t offset = 0;
    if (http_query_param(request->target, "offset", value, sizeof(value)) &&
        !parse_bounded_size(value, (size_t)-1, &offset)) {
        send_error(request->fd, 400, "Bad Request", "bad_request",
                   "offset must be a non-negative integer");
        return;
    }

    int sort = SORT_DATE_DESC;
    if (http_query_param(request->target, "sort", value, sizeof(value))) {
        sort = parse_sort_mode(value);
    }

    media_index *index = media_store_acquire(request->service->store);
    if (!index) {
        media_store_release(request->service->store);
        send_error(request->fd, 503, "Service Unavailable", "not_ready",
                   "the media index is not ready yet");
        return;
    }

    /* A filtered pointer array keeps the sort uniform across modes. The index
     * itself is pre-sorted by date descending, which the default mode reuses. */
    const media_entry **matches = NULL;
    if (index->count > 0) {
        matches = malloc(index->count * sizeof(*matches));
        if (!matches) {
            media_store_release(request->service->store);
            send_error(request->fd, 500, "Internal Server Error", "internal",
                       "out of memory");
            return;
        }
    }

    size_t total = 0;
    for (size_t i = 0; i < index->count; ++i) {
        const media_entry *entry = &index->entries[i];
        if (kind >= 0 && entry->kind != (uint8_t)kind) continue;
        if (game[0] != '\0' && strcmp(entry->game_id, game) != 0) continue;
        matches[total++] = entry;
    }

    if (sort != SORT_DATE_DESC && total > 1) {
        int (*compare)(const void *, const void *) =
            sort == SORT_DATE_ASC  ? compare_date_asc
            : sort == SORT_NAME_ASC ? compare_name_asc
                                    : compare_size_desc;
        qsort(matches, total, sizeof(*matches), compare);
    }

    http_buffer body;
    if (!http_buffer_init(&body)) {
        free(matches);
        media_store_release(request->service->store);
        send_error(request->fd, 500, "Internal Server Error", "internal",
                   "out of memory");
        return;
    }

    (void)http_buffer_append(&body, "{\"total\":");
    (void)http_buffer_appendf(&body, "%zu", total);
    (void)http_buffer_appendf(&body, ",\"offset\":%zu", offset);
    (void)http_buffer_appendf(&body, ",\"limit\":%zu", limit);
    (void)http_buffer_append(&body, ",\"items\":[");

    size_t emitted = 0;
    for (size_t i = offset; i < total && emitted < limit; ++i, ++emitted) {
        const media_entry *entry = matches[i];

        int has_thumb = entry->kind == MEDIA_KIND_PHOTO;
        if (!has_thumb) {
            char thumb[MEDIA_PATH_MAX];
            struct stat info;
            if (media_thumb_path(entry->path, thumb, sizeof(thumb)) &&
                stat(thumb, &info) == 0) {
                has_thumb = 1;
            }
        }

        if (emitted > 0) (void)http_buffer_append(&body, ",");
        (void)http_buffer_append(&body, "{\"id\":");
        (void)http_buffer_append_json_string(&body, entry->id);
        (void)http_buffer_append(&body, ",\"type\":");
        (void)http_buffer_append_json_string(
            &body, entry->kind == MEDIA_KIND_PHOTO ? "photo" : "video");
        (void)http_buffer_append(&body, ",\"game_id\":");
        (void)http_buffer_append_json_string(&body, entry->game_id);
        (void)http_buffer_append(&body, ",\"app_id\":");
        (void)http_buffer_append_json_string(&body, entry->app_id);
        (void)http_buffer_append(&body, ",\"name\":");
        (void)http_buffer_append_json_string(&body, entry->name);
        (void)http_buffer_appendf(&body, ",\"size\":%llu",
                                  (unsigned long long)entry->size);
        (void)http_buffer_appendf(&body, ",\"captured_at\":%lld",
                                  (long long)entry->captured_at);
        (void)http_buffer_appendf(&body, ",\"duration_ms\":%u",
                                  (unsigned)entry->duration_ms);
        (void)http_buffer_appendf(&body, ",\"has_thumb\":%s",
                                  has_thumb ? "true" : "false");
        (void)http_buffer_append(&body, ",\"thumb_url\":");
        (void)http_buffer_appendf(&body, "\"/api/v1/thumb/%s\"", entry->id);
        (void)http_buffer_append(&body, ",\"file_url\":");
        (void)http_buffer_appendf(&body, "\"/api/v1/file/%s\"", entry->id);
        (void)http_buffer_append(&body, "}");
    }
    (void)http_buffer_append(&body, "]}");

    free(matches);
    media_store_release(request->service->store);

    send_json(request->fd, 200, "OK", &body);
    http_buffer_free(&body);
}

static int resolve_entry(http_service *service, const char *id, char *path,
                         size_t capacity, int *out_kind) {
    media_index *index = media_store_acquire(service->store);
    const media_entry *entry = media_index_find_id(index, id);
    int found = 0;
    if (entry && media_path_allowed(entry->path)) {
        snprintf(path, capacity, "%s", entry->path);
        if (out_kind) *out_kind = entry->kind;
        found = 1;
    }
    media_store_release(service->store);
    return found;
}

static void handle_thumb(const http_request *request, const char *id) {
    char path[MEDIA_PATH_MAX];
    int kind = MEDIA_KIND_PHOTO;
    if (!resolve_entry(request->service, id, path, sizeof(path), &kind)) {
        send_error(request->fd, 404, "Not Found", "not_found",
                   "no such media id");
        return;
    }

    struct stat info;
    if (stat(path, &info) != 0 || !S_ISREG(info.st_mode)) {
        send_error(request->fd, 404, "Not Found", "not_found",
                   "media file is no longer present");
        return;
    }

    /* Screenshots are already small enough to serve directly. Video clips get
     * the system-written .jxr sidecar, which browsers cannot render — that is
     * a client-side decoding problem, not a server one. */
    if (kind == MEDIA_KIND_VIDEO) {
        char thumb[MEDIA_PATH_MAX];
        if (!media_thumb_path(path, thumb, sizeof(thumb)) ||
            stat(thumb, &info) != 0) {
            send_error(request->fd, 404, "Not Found", "no_thumbnail",
                       "no .jxr thumbnail sidecar for this clip");
            return;
        }
        snprintf(path, sizeof(path), "%s", thumb);
    }

    const int fd = open(path, O_RDONLY);
    if (fd < 0) {
        send_error(request->fd, 404, "Not Found", "not_found",
                   "media file could not be opened");
        return;
    }
    if (fstat(fd, &info) != 0) {
        close(fd);
        send_error(request->fd, 500, "Internal Server Error", "internal",
                   "media file could not be inspected");
        return;
    }

    const size_t length = (size_t)info.st_size;
    send_headers(request->fd, 200, "OK", content_type_for(path), length, NULL);

    char chunk[HTTP_STREAM_CHUNK];
    size_t remaining = length;
    while (remaining > 0) {
        const size_t wanted = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
        const ssize_t got = read(fd, chunk, wanted);
        if (got <= 0) break;
        if (!send_all(request->fd, chunk, (size_t)got)) break;
        remaining -= (size_t)got;
    }
    close(fd);
}

static void handle_file(const http_request *request, const char *id) {
    char path[MEDIA_PATH_MAX];
    if (!resolve_entry(request->service, id, path, sizeof(path), NULL)) {
        send_error(request->fd, 404, "Not Found", "not_found",
                   "no such media id");
        return;
    }

    const int fd = open(path, O_RDONLY);
    if (fd < 0) {
        send_error(request->fd, 404, "Not Found", "not_found",
                   "media file could not be opened");
        return;
    }

    struct stat info;
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) {
        close(fd);
        send_error(request->fd, 404, "Not Found", "not_found",
                   "media file is no longer present");
        return;
    }

    const uint64_t total = (uint64_t)info.st_size;
    if (total == 0) {
        send_headers(request->fd, 200, "OK", content_type_for(path), 0, NULL);
        close(fd);
        return;
    }

    uint64_t start = 0;
    uint64_t end = total - 1;
    int status = 200;
    char extra[128] = {0};

    char range[HTTP_HEADER_MAX];
    if (http_header_value(request->raw, "Range", range, sizeof(range))) {
        const int parsed = http_parse_range(range, total, &start, &end);
        if (parsed < 0) {
            char unsatisfied[128];
            snprintf(unsatisfied, sizeof(unsatisfied),
                     "Content-Range: bytes */%llu\r\n",
                     (unsigned long long)total);
            send_headers(request->fd, 416, "Range Not Satisfiable",
                         content_type_for(path), 0, unsatisfied);
            close(fd);
            return;
        }
        if (parsed > 0) {
            status = 206;
            snprintf(extra, sizeof(extra),
                     "Content-Range: bytes %llu-%llu/%llu\r\nAccept-Ranges: bytes\r\n",
                     (unsigned long long)start, (unsigned long long)end,
                     (unsigned long long)total);
        }
    }
    if (status == 200) {
        snprintf(extra, sizeof(extra), "Accept-Ranges: bytes\r\n");
    }

    const uint64_t length = end - start + 1;
    send_headers(request->fd, status,
                 status == 206 ? "Partial Content" : "OK",
                 content_type_for(path), (size_t)length, extra);

    if (lseek(fd, (off_t)start, SEEK_SET) == (off_t)-1) {
        close(fd);
        return;
    }

    char chunk[HTTP_STREAM_CHUNK];
    uint64_t remaining = length;
    while (remaining > 0) {
        const size_t wanted =
            remaining < sizeof(chunk) ? (size_t)remaining : sizeof(chunk);
        const ssize_t got = read(fd, chunk, wanted);
        if (got <= 0) break;
        if (!send_all(request->fd, chunk, (size_t)got)) break;
        remaining -= (uint64_t)got;
    }
    close(fd);
}

static void handle_rescan(const http_request *request) {
    if (media_store_rescan(request->service->store) != 0) {
        send_error(request->fd, 500, "Internal Server Error", "internal",
                   "the media index could not be rebuilt");
        return;
    }

    media_index *index = media_store_acquire(request->service->store);
    const size_t count = index ? index->count : 0;
    const unsigned skipped = index ? index->skipped_dirs : 0;
    const int truncated = index ? index->truncated : 0;
    media_store_release(request->service->store);

    http_buffer body;
    if (!http_buffer_init(&body)) {
        send_error(request->fd, 500, "Internal Server Error", "internal",
                   "out of memory");
        return;
    }
    (void)http_buffer_append(&body, "{\"indexed\":");
    (void)http_buffer_appendf(&body, "%zu", count);
    (void)http_buffer_appendf(&body, ",\"skipped_dirs\":%u", skipped);
    (void)http_buffer_appendf(&body, ",\"truncated\":%s",
                              truncated ? "true" : "false");
    (void)http_buffer_append(&body, "}");
    send_json(request->fd, 200, "OK", &body);
    http_buffer_free(&body);
}

static void handle_root(const http_request *request) {
    send_body(request->fd, 200, "OK", "text/html; charset=utf-8", GALLERY_PAGE,
              sizeof(GALLERY_PAGE) - 1);
}

/* ── Connection handling ─────────────────────────────────────────────────── */

static int http_read_request(int fd, char *buffer, size_t capacity) {
    size_t used = 0;
    while (used + 1 < capacity) {
        const ssize_t received = recv(fd, buffer + used, capacity - 1 - used, 0);
        if (received <= 0) return 0;
        used += (size_t)received;
        buffer[used] = '\0';
        if (strstr(buffer, "\r\n\r\n") != NULL) return 1;
    }
    return 0;
}

static void http_dispatch(http_request *request) {
    char id[MEDIA_ID_MAX];

    if (strcmp(request->method, "OPTIONS") == 0) {
        send_body(request->fd, 204, "No Content", "text/plain", "", 0);
        return;
    }

    if (strcmp(request->method, "GET") == 0) {
        if (strcmp(request->target, "/api/v1/health") == 0) {
            handle_health(request);
            return;
        }
        if (strncmp(request->target, "/api/v1/media", 13) == 0 &&
            (request->target[13] == '\0' || request->target[13] == '?')) {
            handle_media_list(request);
            return;
        }
        if (http_path_segment(request->target, "/api/v1/thumb/", id,
                              sizeof(id))) {
            handle_thumb(request, id);
            return;
        }
        if (http_path_segment(request->target, "/api/v1/file/", id,
                              sizeof(id))) {
            handle_file(request, id);
            return;
        }
        if (strcmp(request->target, "/") == 0 ||
            strcmp(request->target, "/index.html") == 0) {
            handle_root(request);
            return;
        }
    }

    if (strcmp(request->method, "POST") == 0 &&
        strcmp(request->target, "/api/v1/rescan") == 0) {
        handle_rescan(request);
        return;
    }

    send_error(request->fd, 404, "Not Found", "not_found",
               "unknown endpoint");
}

static void http_handle_connection(http_service *service, int fd) {
    char raw[HTTP_REQUEST_MAX];
    if (!http_read_request(fd, raw, sizeof(raw))) return;

    http_request request;
    memset(&request, 0, sizeof(request));
    request.service = service;
    request.fd = fd;
    request.raw = raw;

    if (sscanf(raw, "%15s %1023s", request.method, request.target) != 2) {
        send_error(fd, 400, "Bad Request", "bad_request",
                   "malformed request line");
        return;
    }

    http_log("%s %s", request.method, request.target);
    http_dispatch(&request);
}

static void *http_connection_main(void *argument) {
    http_connection *connection = (http_connection *)argument;

    http_handle_connection(connection->service, connection->fd);
    close(connection->fd);

    pthread_mutex_lock(&connection->service->mutex);
    connection->service->active_connections--;
    pthread_mutex_unlock(&connection->service->mutex);

    free(connection);
    return NULL;
}

static void configure_client_socket(int fd) {
    struct timeval timeout;
    timeout.tv_sec = HTTP_IO_TIMEOUT_SEC;
    timeout.tv_usec = 0;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

static void http_spawn_connection(http_service *service, int client_fd) {
    pthread_mutex_lock(&service->mutex);
    const int allowed = service->active_connections < HTTP_MAX_CONNECTIONS;
    if (allowed) service->active_connections++;
    pthread_mutex_unlock(&service->mutex);

    if (!allowed) {
        static const char BUSY[] =
            "{\"error\":{\"code\":\"busy\",\"message\":\"too many concurrent "
            "connections\"}}";
        send_body(client_fd, 503, "Service Unavailable",
                  "application/json; charset=utf-8", BUSY, sizeof(BUSY) - 1);
        close(client_fd);
        return;
    }

    http_connection *connection = malloc(sizeof(*connection));
    if (!connection) {
        pthread_mutex_lock(&service->mutex);
        service->active_connections--;
        pthread_mutex_unlock(&service->mutex);
        close(client_fd);
        return;
    }

    connection->service = service;
    connection->fd = client_fd;

    pthread_t thread;
    if (pthread_create(&thread, NULL, http_connection_main, connection) != 0) {
        pthread_mutex_lock(&service->mutex);
        service->active_connections--;
        pthread_mutex_unlock(&service->mutex);
        free(connection);
        close(client_fd);
        return;
    }

    pthread_detach(thread);
}

static void http_log(const char *format, ...) {
    FILE *log = fopen(PLUGIN_LOG_PATH, "a");
    if (!log) return;
    va_list args;
    va_start(args, format);
    fprintf(log, "[" PLUGIN_ID "] http: ");
    vfprintf(log, format, args);
    fprintf(log, "\n");
    va_end(args);
    fclose(log);
}

static int http_listen(uint16_t port, int local_only) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        http_log("socket() failed: %s", strerror(errno));
        return -1;
    }

    int reuse = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr =
        local_only ? htonl(INADDR_LOOPBACK) : htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        http_log("bind(%u) failed: %s", (unsigned)port, strerror(errno));
        close(fd);
        return -1;
    }
    if (listen(fd, 8) != 0) {
        http_log("listen(%u) failed: %s", (unsigned)port, strerror(errno));
        close(fd);
        return -1;
    }
    http_log("listening on %s:%u", local_only ? "127.0.0.1" : "0.0.0.0", (unsigned)port);
    return fd;
}

int http_server_serve(http_service *service) {
    if (!service || !service->store) return -1;

    const int listen_fd = http_listen(service->port, service->local_only);
    if (listen_fd < 0) return -1;

    pthread_mutex_lock(&service->mutex);
    service->listening = 1;
    pthread_mutex_unlock(&service->mutex);

    for (;;) {
        pthread_mutex_lock(&service->mutex);
        const int stop = service->stop_requested;
        pthread_mutex_unlock(&service->mutex);
        if (stop) break;

        struct pollfd descriptor;
        descriptor.fd = listen_fd;
        descriptor.events = POLLIN;
        descriptor.revents = 0;
        if (poll(&descriptor, 1, HTTP_ACCEPT_POLL_MS) <= 0) continue;

        const int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) {
            http_log("accept() failed: %s", strerror(errno));
            continue;
        }

        configure_client_socket(client_fd);
        http_spawn_connection(service, client_fd);
    }

    pthread_mutex_lock(&service->mutex);
    service->listening = 0;
    pthread_mutex_unlock(&service->mutex);

    close(listen_fd);
    return 0;
}
