#include "media_index.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include "media_meta.h"
#include "plugin_config.h"

typedef struct scan_state {
    media_index *index;
    size_t       photo_seq;
    size_t       video_seq;
} scan_state;

static int is_media_file(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    return strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0 ||
           strcasecmp(dot, ".png") == 0 || strcasecmp(dot, ".webm") == 0 ||
           strcasecmp(dot, ".mp4") == 0;
}

static int index_reserve(media_index *index) {
    if (index->count < index->capacity) return 1;

    const size_t capacity = index->capacity ? index->capacity * 2 : 256;
    media_entry *entries =
        realloc(index->entries, capacity * sizeof(media_entry));
    if (!entries) return 0;

    index->entries = entries;
    index->capacity = capacity;
    return 1;
}

static void index_push(scan_state *state, const char *path, const char *name,
                       const struct stat *info, media_kind kind,
                       const char *app_id, const char *game_id) {
    media_index *index = state->index;
    if (index->count >= MEDIA_MAX_ENTRIES) {
        index->truncated = 1;
        return;
    }
    if (!index_reserve(index)) {
        index->truncated = 1;
        return;
    }

    media_entry *entry = &index->entries[index->count];
    memset(entry, 0, sizeof(*entry));

    snprintf(entry->path, sizeof(entry->path), "%s", path);
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    snprintf(entry->app_id, sizeof(entry->app_id), "%s", app_id);
    snprintf(entry->game_id, sizeof(entry->game_id), "%s", game_id);
    entry->size = (uint64_t)info->st_size;
    entry->kind = (uint8_t)kind;

    const size_t sequence = kind == MEDIA_KIND_PHOTO ? state->photo_seq++
                                                     : state->video_seq++;
    snprintf(entry->id, sizeof(entry->id), "%c%06u",
             kind == MEDIA_KIND_PHOTO ? 'p' : 'v', (unsigned)sequence);

    /* Timestamp: authoritative sidecar, then the filename, then mtime. */
    int64_t captured_at = 0;
    uint32_t duration_ms = 0;
    media_meta_read_file(path, &captured_at, &duration_ms);
    if (captured_at == 0) captured_at = media_meta_time_from_name(name);
    if (captured_at == 0) captured_at = (int64_t)info->st_mtime;
    entry->captured_at = captured_at;
    entry->duration_ms = duration_ms;

    index->count++;
    if (kind == MEDIA_KIND_PHOTO) {
        index->photo_count++;
    } else {
        index->video_count++;
    }
}

/* depth 0 = photo|video, 1 = AppId, 2 = GameId, 3 = hash bucket, 4 = files.
 * app_id/game_id are carried down the recursion so the title id is read from
 * its known position instead of being guessed by pattern-matching a path
 * segment (which misclassifies NPXS###### style application ids). */
static void scan_dir(scan_state *state, const char *dir, int depth,
                     media_kind kind, const char *app_id, const char *game_id) {
    if (depth > MEDIA_MAX_DEPTH) return;
    if (state->index->count >= MEDIA_MAX_ENTRIES) {
        state->index->truncated = 1;
        return;
    }

    DIR *handle = opendir(dir);
    if (!handle) {
        state->index->skipped_dirs++;
        return;
    }

    struct dirent *item;
    while ((item = readdir(handle)) != NULL) {
        if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0)
            continue;

        char child[MEDIA_PATH_MAX];
        const int length =
            snprintf(child, sizeof(child), "%s/%s", dir, item->d_name);
        if (length < 0 || (size_t)length >= sizeof(child)) continue;

        struct stat info;
        if (stat(child, &info) != 0) continue;

        if (S_ISDIR(info.st_mode)) {
            const char *next_app = depth == 0 ? item->d_name : app_id;
            const char *next_game = depth == 1 ? item->d_name : game_id;
            scan_dir(state, child, depth + 1, kind, next_app, next_game);
            continue;
        }

        /* .meta / .jxr sidecars are skipped here by extension. */
        if (!is_media_file(item->d_name)) continue;
        index_push(state, child, item->d_name, &info, kind, app_id, game_id);
    }

    closedir(handle);
}

static int compare_date_desc(const void *left, const void *right) {
    const media_entry *a = (const media_entry *)left;
    const media_entry *b = (const media_entry *)right;
    if (a->captured_at != b->captured_at)
        return a->captured_at > b->captured_at ? -1 : 1;
    return strcmp(a->name, b->name);
}

media_index *media_index_build_at(const char *root) {
    if (!root) return NULL;

    media_index *index = calloc(1, sizeof(*index));
    if (!index) return NULL;

    static const struct {
        const char *name;
        media_kind  kind;
    } SOURCES[] = {
        {"photo", MEDIA_KIND_PHOTO},
        {"video", MEDIA_KIND_VIDEO},
    };

    scan_state state;
    state.index = index;
    state.photo_seq = 0;
    state.video_seq = 0;

    for (size_t i = 0; i < sizeof(SOURCES) / sizeof(SOURCES[0]); ++i) {
        char dir[MEDIA_PATH_MAX];
        const int length =
            snprintf(dir, sizeof(dir), "%s/%s", root, SOURCES[i].name);
        if (length < 0 || (size_t)length >= sizeof(dir)) continue;
        scan_dir(&state, dir, 0, SOURCES[i].kind, "", "");
    }

    if (index->count > 1) {
        qsort(index->entries, index->count, sizeof(media_entry),
              compare_date_desc);
    }
    index->indexed_at = (int64_t)time(NULL);
    return index;
}

void media_index_destroy(media_index *index) {
    if (!index) return;
    free(index->entries);
    free(index);
}

const media_entry *media_index_find_id(const media_index *index,
                                       const char *id) {
    if (!index || !id) return NULL;
    for (size_t i = 0; i < index->count; ++i) {
        if (strcmp(index->entries[i].id, id) == 0) return &index->entries[i];
    }
    return NULL;
}

void media_store_init(media_store *store, const char *root) {
    if (!store) return;
    memset(store, 0, sizeof(*store));
    pthread_mutex_init(&store->lock, NULL);
    if (root) snprintf(store->root, sizeof(store->root), "%s", root);
}

void media_store_destroy(media_store *store) {
    if (!store) return;
    pthread_mutex_lock(&store->lock);
    media_index_destroy(store->index);
    store->index = NULL;
    pthread_mutex_unlock(&store->lock);
    pthread_mutex_destroy(&store->lock);
}

int media_store_rescan(media_store *store) {
    if (!store) return -1;

    media_index *fresh = media_index_build_at(store->root);
    if (!fresh) return -1;

    pthread_mutex_lock(&store->lock);
    media_index *previous = store->index;
    store->index = fresh;
    pthread_mutex_unlock(&store->lock);

    media_index_destroy(previous);
    return 0;
}

media_index *media_store_acquire(media_store *store) {
    if (!store) return NULL;
    pthread_mutex_lock(&store->lock);
    return store->index;
}

void media_store_release(media_store *store) {
    if (store) pthread_mutex_unlock(&store->lock);
}
