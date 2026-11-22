#pragma once

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#define MEDIA_PATH_MAX    512
#define MEDIA_NAME_MAX    128
#define MEDIA_ID_MAX      16
#define MEDIA_APP_ID_MAX  24
#define MEDIA_GAME_ID_MAX 16
#define MEDIA_MAX_ENTRIES 50000
#define MEDIA_MAX_DEPTH   6

typedef enum media_kind {
    MEDIA_KIND_PHOTO = 0,
    MEDIA_KIND_VIDEO = 1
} media_kind;

typedef struct media_entry {
    char     path[MEDIA_PATH_MAX]; /* absolute path; never sent to clients */
    char     id[MEDIA_ID_MAX];     /* opaque handle used in URLs */
    char     app_id[MEDIA_APP_ID_MAX];
    char     game_id[MEDIA_GAME_ID_MAX];
    char     name[MEDIA_NAME_MAX];
    uint64_t size;
    int64_t  captured_at;  /* Unix seconds, 0 when unknown */
    uint32_t duration_ms;  /* video only, 0 when unknown */
    uint8_t  kind;
} media_entry;

typedef struct media_index {
    media_entry *entries;
    size_t       count;
    size_t       capacity;
    size_t       photo_count;
    size_t       video_count;
    unsigned     skipped_dirs; /* directories we could not open */
    int          truncated;    /* hit MEDIA_MAX_ENTRIES */
    int64_t      indexed_at;
} media_index;

/* Owns the live index. A rescan builds a whole new index outside the lock and
 * swaps it in, so readers never observe a half-built table. */
typedef struct media_store {
    pthread_mutex_t lock;
    media_index    *index;
    char            root[MEDIA_PATH_MAX];
    /* Set once by main() before the listener starts: did credential elevation
     * succeed? Reported through /api/v1/health so a permission problem is
     * diagnosable from the client instead of showing up as an empty library. */
    int             privileged;
} media_store;

void media_store_init(media_store *store, const char *root);
void media_store_destroy(media_store *store);

/* Rebuilds the index. Returns 0 on success, -1 when the build failed. */
int media_store_rescan(media_store *store);

/* The caller must hold the lock only for as long as it touches the index —
 * never across socket or file I/O on a client connection. */
media_index *media_store_acquire(media_store *store);
void media_store_release(media_store *store);

/* `root` is injectable so host tests can point at a fixture tree. */
media_index *media_index_build_at(const char *root);
void media_index_destroy(media_index *index);

const media_entry *media_index_find_id(const media_index *index,
                                       const char *id);
