#pragma once

#include <stddef.h>
#include <stdint.h>

#include "http_service.h"

/* Growable text buffer used to assemble responses before any socket write, so
 * the media store lock is never held across client I/O. */
typedef struct http_buffer {
    char  *data;
    size_t length;
    size_t capacity;
} http_buffer;

int http_buffer_init(http_buffer *buffer);
void http_buffer_free(http_buffer *buffer);
int http_buffer_append(http_buffer *buffer, const char *text);
int http_buffer_appendf(http_buffer *buffer, const char *format, ...);
int http_buffer_append_json_string(http_buffer *buffer, const char *text);

/* --- Pure helpers, exercised by the host tests --- */

/* Parses a single-range "bytes=start-end" header against a known size.
 * Returns 1 and fills start/end on success, 0 when the header is absent or
 * malformed, -1 when the range cannot be satisfied (HTTP 416). */
int http_parse_range(const char *value, uint64_t size, uint64_t *start,
                     uint64_t *end);

/* Percent-decodes into `output`. Returns 0 only when the input does not fit.
 * A '%' that is not followed by two hex digits is copied verbatim rather than
 * treated as an error — see the note in http_server.c. */
int http_url_decode(const char *input, char *output, size_t capacity);

/* Extracts a query parameter from a request target. Returns 1 when present. */
int http_query_param(const char *target, const char *name, char *out,
                     size_t capacity);

/* Extracts the single path segment following `prefix`, rejecting anything with
 * a further slash. Returns 1 on a match. */
int http_path_segment(const char *target, const char *prefix, char *out,
                      size_t capacity);

/* --- Serving --- */

/* Blocks in an accept loop until the service is asked to stop.
 * Returns 0 on a clean stop, -1 when the listener could not be bound. */
int http_server_serve(http_service *service);
void http_server_wake(uint16_t port);
