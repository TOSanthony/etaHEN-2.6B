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
