#include "downloader.h"

#include <curl/curl.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "plugin_config.h"

__asm__(".section .rodata\n"
        ".balign 16\n"
        ".global eta_curl_ca_bundle_start\n"
        "eta_curl_ca_bundle_start:\n"
        ".incbin \"" ETA_CURL_CA_BUNDLE "\"\n"
        ".global eta_curl_ca_bundle_end\n"
        "eta_curl_ca_bundle_end:\n"
        ".byte 0\n"
        ".previous\n");
extern const unsigned char eta_curl_ca_bundle_start[];
extern const unsigned char eta_curl_ca_bundle_end[];

#define MIN_ELF_SIZE (16u * 1024u)
#define MAX_ELF_SIZE (24u * 1024u * 1024u)

typedef struct sink {
    FILE *file;
    size_t bytes;
    int too_big;
    int io_error;
} sink;

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *user) {
    sink *s = (sink *)user;
    size_t n = size * nmemb;
    if (s->bytes + n > MAX_ELF_SIZE) { s->too_big = 1; return 0; }
    if (fwrite(ptr, 1, n, s->file) != n) { s->io_error = 1; return 0; }
    s->bytes += n;
    return n;
}

typedef struct membuf {
    char *data;
    size_t len;
    size_t cap;
} membuf;

static size_t mem_cb(char *ptr, size_t size, size_t nmemb, void *user) {
    membuf *m = (membuf *)user;
    size_t n = size * nmemb;
    if (m->len + n + 1 > (1u << 20)) return 0; /* 1 MiB cap */
    if (m->len + n + 1 > m->cap) {
        size_t cap = m->cap ? m->cap * 2 : 16384;
        while (cap < m->len + n + 1) cap *= 2;
        char *p = (char *)realloc(m->data, cap);
        if (!p) return 0;
        m->data = p;
        m->cap = cap;
    }
    memcpy(m->data + m->len, ptr, n);
    m->len += n;
    m->data[m->len] = '\0';
    return n;
}

typedef struct progress_ctx {
    dl_progress_fn fn;
    void *user;
    unsigned last;
} progress_ctx;

static int xferinfo_cb(void *user, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal,
                       curl_off_t ulnow) {
    (void)ultotal;
    (void)ulnow;
    progress_ctx *p = (progress_ctx *)user;
    if (!p->fn || dltotal <= 0) return 0;
    unsigned pct = (unsigned)((dlnow * 100) / dltotal);
    if (pct > 100) pct = 100;
    if (pct != p->last && (pct >= p->last + 2 || pct == 100)) {
        p->last = pct;
        p->fn(p->user, pct, (size_t)dlnow, (size_t)dltotal);
    }
    return 0;
}

static int g_curl_ready = 0;

static void set_detail(char *d, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void set_detail(char *d, size_t n, const char *fmt, ...) {
    if (!d || n == 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(d, n, fmt, ap);
    va_end(ap);
}

static int has_marker(const unsigned char *buf, size_t size) {
    static const char key[] = ".eta_plugin";
    const size_t kn = sizeof(key) - 1;
    if (size < kn) return 0;
    for (size_t i = 0; i + kn <= size; ++i)
        if (buf[i] == (unsigned char)key[0] && memcmp(buf + i, key, kn) == 0) return 1;
    return 0;
}

static int valid_id(const char *id) {
    if (!id || strlen(id) != 9) return 0;
    for (int i = 0; i < 4; i++)
        if (!((id[i] >= 'A' && id[i] <= 'Z') || (id[i] >= 'a' && id[i] <= 'z'))) return 0;
    for (int i = 4; i < 9; i++)
        if (id[i] < '0' || id[i] > '9') return 0;
    return 1;
}

/* The ELF must start with the ELF magic and carry the .eta_plugin section. */
static int looks_like_plugin(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long size = ftell(f);
    if (size < (long)MIN_ELF_SIZE || size > (long)MAX_ELF_SIZE) { fclose(f); return 0; }
    rewind(f);
    unsigned char *buf = (unsigned char *)malloc((size_t)size);
    if (!buf) { fclose(f); return 0; }
    int ok = fread(buf, 1, (size_t)size, f) == (size_t)size &&
             memcmp(buf, "\x7f" "ELF", 4) == 0 &&
             has_marker(buf, (size_t)size);
    free(buf);
    fclose(f);
    return ok;
}

static int curl_ready(void) {
    if (!g_curl_ready) {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) return 0;
        g_curl_ready = 1;
    }
    return 1;
}

static void common_setup(CURL *curl, const char *url, struct curl_blob *ca, long timeout) {
    ca->data = (void *)eta_curl_ca_bundle_start;
    ca->len = (size_t)(eta_curl_ca_bundle_end - eta_curl_ca_bundle_start);
    ca->flags = CURL_BLOB_NOCOPY;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "etaHEN-plugin-updater");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, ca);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
}

/* Finds the next "key":"value" pair at or after *pos. */
static int json_next_string(const char *buf, const char **pos, const char *key, char *out,
                            size_t out_size) {
    (void)buf;
    char pat[48];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = *pos;
    for (;;) {
        p = strstr(p, pat);
        if (!p) return 0;
        p += strlen(pat);
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (*p != ':') continue;
        p++;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (*p != '"') continue;
        p++;
        size_t n = 0;
        while (*p && *p != '"' && n + 1 < out_size) out[n++] = *p++;
        out[n] = '\0';
        *pos = p;
        return 1;
    }
}

unsigned downloader_list_plugins(int channel, char ids[][DL_ID_LEN], unsigned max,
                                 char *detail, size_t detail_size) {
    if (!curl_ready()) { set_detail(detail, detail_size, "curl init failed"); return 0; }
    char url[256];
    if (channel) snprintf(url, sizeof url, "https://api.github.com/repos/%s/releases/latest", UPDATE_REPO);
    else snprintf(url, sizeof url, "https://api.github.com/repos/%s/releases/tags/nightly", UPDATE_REPO);

    CURL *curl = curl_easy_init();
    if (!curl) { set_detail(detail, detail_size, "curl_easy_init failed"); return 0; }
    struct curl_blob ca;
    membuf m = {0, 0, 0};
    common_setup(curl, url, &ca, 25L);
    struct curl_slist *hdr = curl_slist_append(NULL, "Accept: application/vnd.github+json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, mem_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &m);
    CURLcode rc = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_slist_free_all(hdr);
    curl_easy_cleanup(curl);

    unsigned count = 0;
    if (rc != CURLE_OK) {
        set_detail(detail, detail_size, "network: %s", curl_easy_strerror(rc));
    } else if (http != 200 || !m.data) {
        set_detail(detail, detail_size, "HTTP %ld (release not found?)", http);
    } else {
        const char *pos = strstr(m.data, "\"assets\"");
        if (!pos) pos = m.data;
        char name[96];
        while (count < max && json_next_string(m.data, &pos, "name", name, sizeof name)) {
            if (strlen(name) != 13 || strcmp(name + 9, ".elf") != 0) continue; /* <ID>.elf */
            char id[DL_ID_LEN];
            memcpy(id, name, 9);
            id[9] = '\0';
            if (!valid_id(id) || strcmp(id, PLUGIN_ID) == 0) continue;
            int dup = 0;
            for (unsigned i = 0; i < count; i++) if (strcmp(ids[i], id) == 0) dup = 1;
            if (!dup) { memcpy(ids[count], id, 10); count++; }
        }
        if (count == 0) set_detail(detail, detail_size, "no plugin ELF in this release");
    }
    free(m.data);
    return count;
}

dl_result downloader_install_plugin(const char *plugin_id, int channel, dl_progress_fn progress,
                                    void *progress_user, char *detail, size_t detail_size) {
    if (!valid_id(plugin_id)) {
        set_detail(detail, detail_size, "invalid plugin id");
        return DL_ERR_INVALID;
    }
    if (!curl_ready()) {
        set_detail(detail, detail_size, "curl init failed");
        return DL_ERR_NETWORK;
    }

    char url[256], tmp[256], dst[256];
    if (channel) {
        snprintf(url, sizeof url, "https://github.com/%s/releases/latest/download/%s.elf",
                 UPDATE_REPO, plugin_id);
    } else {
        snprintf(url, sizeof url, "https://github.com/%s/releases/download/nightly/%s.elf",
                 UPDATE_REPO, plugin_id);
    }
    snprintf(tmp, sizeof tmp, "%s/%s.elf.dl", PLUGINS_DIR, plugin_id);
    snprintf(dst, sizeof dst, "%s/%s.elf", PLUGINS_DIR, plugin_id);

    sink s;
    memset(&s, 0, sizeof s);
    s.file = fopen(tmp, "wb");
    if (!s.file) {
        set_detail(detail, detail_size, "cannot write %s", tmp);
        return DL_ERR_IO;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        fclose(s.file);
        unlink(tmp);
        set_detail(detail, detail_size, "curl_easy_init failed");
        return DL_ERR_NETWORK;
    }
    struct curl_blob ca;
    common_setup(curl, url, &ca, 180L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    progress_ctx pctx = {progress, progress_user, 101};
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &pctx);

    CURLcode rc = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);

    int flushed = (fflush(s.file) == 0) && (fsync(fileno(s.file)) == 0);
    fclose(s.file);

    if (rc != CURLE_OK) {
        unlink(tmp);
        if (s.too_big) set_detail(detail, detail_size, "file too large");
        else set_detail(detail, detail_size, "network: %s", curl_easy_strerror(rc));
        return DL_ERR_NETWORK;
    }
    if (http != 200) {
        unlink(tmp);
        set_detail(detail, detail_size, "HTTP %ld (not found in release?)", http);
        return DL_ERR_HTTP;
    }
    if (!flushed || s.io_error) {
        unlink(tmp);
        set_detail(detail, detail_size, "write error");
        return DL_ERR_IO;
    }
    if (!looks_like_plugin(tmp)) {
        unlink(tmp);
        set_detail(detail, detail_size, "downloaded file is not a valid plugin ELF");
        return DL_ERR_INVALID;
    }
    if (rename(tmp, dst) != 0) {
        unlink(tmp);
        set_detail(detail, detail_size, "cannot install: %s", strerror(errno));
        return DL_ERR_IO;
    }

    /* Tell etaHEN not to overwrite this version with its built-in copy. */
    char keep[256];
    snprintf(keep, sizeof keep, "%s/%s.elf.keep", PLUGINS_DIR, plugin_id);
    FILE *k = fopen(keep, "w");
    if (k) fclose(k);
    return DL_OK;
}

void downloader_forget_plugin(const char *plugin_id) {
    if (!valid_id(plugin_id)) return;
    char keep[256];
    snprintf(keep, sizeof keep, "%s/%s.elf.keep", PLUGINS_DIR, plugin_id);
    unlink(keep);
}
