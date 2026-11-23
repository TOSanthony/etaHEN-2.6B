#include "progress_file.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PROGRESS_FILE "/data/etaHEN/plugin_progress.txt"

static unsigned long long g_seq;
static char g_title[64];

static void clean(char *dst, size_t size, const char *src) {
    size_t n = 0;
    for (; src && *src && n + 1 < size; ++src)
        dst[n++] = (*src == '\n' || *src == '\r') ? ' ' : *src;
    dst[n] = '\0';
}

static void write_state(const char *state, unsigned percent, const char *phase) {
    char tmp[] = PROGRESS_FILE ".tmp";
    char ph[160];
    clean(ph, sizeof ph, phase);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "seq=%llu\nstate=%s\ntitle=%s\nphase=%s\npercent=%u\n", g_seq, state, g_title, ph,
            percent > 100 ? 100u : percent);
    int bad = fflush(f) != 0;
    fclose(f);
    if (bad || rename(tmp, PROGRESS_FILE) != 0) unlink(tmp);
}

void progress_begin(const char *title, const char *phase) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    unsigned long long seq = (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)(ts.tv_nsec / 1000000);
    g_seq = seq > g_seq ? seq : g_seq + 1; /* always increasing */
    clean(g_title, sizeof g_title, title);
    write_state("running", 0, phase);
}

void progress_update(unsigned percent, const char *phase) { write_state("running", percent, phase); }

void progress_end(int ok, const char *phase) { write_state(ok ? "ok" : "error", ok ? 100u : 0u, phase); }
