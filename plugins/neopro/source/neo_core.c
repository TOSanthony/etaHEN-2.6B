/* GPL-3.0-only. Engine of the PS4 Pro mode plugin.
 *
 * Three parts run in this single ELF:
 *  - ps-patch-system (Apache-2.0): hooks SceShellCore and applies the XML patches of every launched game.
 *  - the game patches of NeoEnabler (gasb07, GPL-3.0), embedded and merged into /data/user/patches/xml_orbis.
 *  - the NeoEnabler launch shim (GPL-3.0): sets the PS4 Pro attribute bit when ShellCore starts a PS4 title.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "neo_core.h"
#include "plugin_config.h"
#include "xml_bundle.h"

#define VERSION "PS4 Pro Mode " PLUGIN_VERSION
#define PS_NO_HTTP 1
#define NEO_LOG_DIR "/data/etaHEN"

static void log_line(const char *fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    printf("[%s] %s\n", PLUGIN_ID, line);
    fflush(stdout);
    FILE *f = fopen(PLUGIN_LOG_PATH, "a");
    if (f) {
        fprintf(f, "[%s] %s\n", PLUGIN_ID, line);
        fclose(f);
    }
}

static int write_file(const char *path, const void *data, size_t size) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return -1;
    const char *p = data;
    size_t left = size;
    while (left) {
        ssize_t w = write(fd, p, left);
        if (w <= 0) { close(fd); return -1; }
        p += w; left -= (size_t)w;
    }
    return close(fd);
}

/* The game patches are embedded, nothing is downloaded. */
static char *ps_get(const char *url, size_t *len);
#include "xml_merge.h"
#include "game_patches.h"
#include "patch_sync.h"
static char *ps_get(const char *url, size_t *len) { (void)url; (void)len; return NULL; }
#include "games.h"
#include "shellcore_hook.h"

int pspatch_server_start(void);
int pspatch_attach(pid_t shellcore_pid);

static struct {
    volatile int enabled, all_games, stop, running;
    pthread_mutex_t lock;
    char status[160];
    pid_t shell_pid;
    int hook_ok;
    pthread_t thread;
} core = {.lock = PTHREAD_MUTEX_INITIALIZER};

static void set_status(const char *fmt, ...) {
    va_list ap;
    pthread_mutex_lock(&core.lock);
    va_start(ap, fmt);
    vsnprintf(core.status, sizeof(core.status), fmt, ap);
    va_end(ap);
    pthread_mutex_unlock(&core.lock);
}

void neo_core_status(char *out, size_t cap) {
    pthread_mutex_lock(&core.lock);
    snprintf(out, cap, "%s", core.status[0] ? core.status : "Demarrage");
    pthread_mutex_unlock(&core.lock);
}

/* ---- game patches -------------------------------------------------------------------------------------------- */

/* Same flow as NeoEnabler's patch_sync(), but the XML files come from this ELF instead of GitHub. */
int neo_core_sync(char *msg, size_t cap) {
    ps_result r;
    memset(&r, 0, sizeof(r));
    ps_rename_count = 0;
    if (ps_mkdirs(PS_DIR)) {
        snprintf(msg, cap, "Dossier %s inaccessible", PS_DIR);
        log_line("patches: cannot create %s (errno %d)", PS_DIR, errno);
        return -1;
    }
    static ps_file files[PS_MAX_FILES];
    int count = ps_scan(PS_DIR, files, PS_MAX_FILES);
    if (count < 0) count = 0;
    for (unsigned i = 0; i < neo_xml_bundle_count; i++) {
        const neo_xml_entry *e = &neo_xml_bundle[i];
        ps_apply(&r, PS_DIR, files, count, e->name, e->begin, (size_t)(e->end - e->begin));
    }
    ps_move_settings(&r, PS_DIR);
    snprintf(msg, cap, "%d jeux, %d fichiers mis a jour, %d crees, %d inchanges%s", r.games, r.files_updated,
             r.files_created, r.files_unchanged, r.errors ? " (erreurs, voir le log)" : "");
    log_line("patches: %s", msg);
    return r.errors ? -1 : 0;
}

int neo_games_list(neo_game_info *out, int cap) {
    int n = 0, count = 0;
    game_t *g = games_scan(&count);
    if (!g) return 0;
    size_t sl;
    char *settings = settings_read(&sl);
    for (int i = 0; i < count && n < cap; i++) {
        neo_game_info *o = &out[n++];
        snprintf(o->tid, sizeof(o->tid), "%s", g[i].tid);
        snprintf(o->name, sizeof(o->name), "%s", g[i].name);
        o->pro_on = 0;
        for (int k = 0; k < g[i].count; k++)
            if (g[i].patches[k].needs_pro && !g[i].patches[k].needs_8gib &&
                gp_setting(settings, sl, g[i].patches[k].hash) == 1)
                o->pro_on = 1;
    }
    free(settings);
    games_free(g, count);
    return n;
}

/* Turns the PS4 Pro patches of a game on (the first resolution entry that needs no 8 GiB) or off (all of them). */
int neo_game_set(const char *tid, int on, char *msg, size_t cap) {
    char name[128], ver[16];
    if (!game_tid(tid) || game_info(tid, name, sizeof(name), ver, sizeof(ver))) {
        snprintf(msg, cap, "Jeu inconnu");
        return -1;
    }
    int n;
    gp_patch *p = game_patches(tid, ver, &n);
    if (!p || n <= 0) { free(p); snprintf(msg, cap, "Aucun correctif pour %s", tid); return -1; }
    int rc = -1;
    if (on) {
        int pick = -1;
        for (int i = 0; i < n && pick < 0; i++)
            if (p[i].resolution && p[i].needs_pro && !p[i].needs_8gib && gp_main_elf(&p[i])) pick = i;
        for (int i = 0; i < n && pick < 0; i++)
            if (p[i].needs_pro && !p[i].needs_8gib) pick = i;
        if (pick < 0) snprintf(msg, cap, "Pas de correctif Pro compatible pour %s", tid);
        else rc = game_set_patch(tid, p[pick].hash, 1, 0, msg, cap);
    } else {
        size_t sl;
        char *settings = settings_read(&sl);
        rc = 0;
        for (int i = 0; i < n; i++) {
            if (!(p[i].needs_pro || p[i].needs_8gib) || gp_setting(settings, sl, p[i].hash) != 1) continue;
            if (game_set_patch(tid, p[i].hash, 0, 0, msg, cap)) rc = -1;
        }
        if (!rc) snprintf(msg, cap, "%s: mode Pro desactive", tid);
        free(settings);
    }
    free(p);
    return rc;
}

/* ---- worker -------------------------------------------------------------------------------------------------- */

#define ANSWER_WAIT_CYCLES UINT64_C(3000000000) /* about a second of TSC */

static void toolbox_wait(void) {
    /* etaHEN creates this marker once its own ShellCore/ShellUI setup is done. */
    for (int i = 0; i < 120 && !core.stop; i++) {
        if (access("/system_tmp/etahen/ready/toolbox", F_OK) == 0) return;
        sleep(1);
    }
}

/* One iteration: keeps the hook installed on the current ShellCore, answers pending launch questions. */
static void *worker(void *arg) {
    (void)arg;
    pid_t last_pid = -1;
    uint32_t answered = 0;
    int last_enabled = -1, last_all = -1, ticks = 0, hook_state_now = -99, logged_st = -99, no_ps = 0, attach_tries = 0;
    char msg[256];

    toolbox_wait();
    neo_core_sync(msg, sizeof(msg));
    if (pspatch_server_start() == 0) log_line("patch server started");
    else log_line("patch server failed to start");
    core.running = 1;

    while (!core.stop) {
        const int enabled = core.enabled, all = core.all_games;
        if (ticks++ % 500 == 0) { /* about every second */
            pid_t pid = find_proc("SceShellCore");
            if (pid > 0 && pid != last_pid) {
                log_line("SceShellCore pid %d: installing the ps-patch-system hook", pid);
                sleep(3);
                pspatch_attach(pid);
                last_pid = pid;
                last_enabled = -1; /* the shim must be installed again */
                attach_tries = 0;
                no_ps = 0;
                logged_st = -99;
            }
            if (last_pid > 0 && (enabled != last_enabled || all != last_all || hook_state_now != HOOK_ON)) {
                shell_t s;
                int st = hook_state(&s);
                if (enabled && st == HOOK_OFF) {
                    int rc = hook_set(1);
                    log_line("NEO hook install rc=%d", rc);
                    st = hook_state(&s);
                } else if (!enabled && st == HOOK_ON) {
                    int rc = hook_set(0);
                    log_line("NEO hook removal rc=%d", rc);
                    st = hook_state(&s);
                }
                if (st == HOOK_ON) {
                    /* all games: the shim forces Pro without asking; otherwise it asks us for every PS4 title */
                    int rc = shim_configure(&s, all, all ? 0 : ANSWER_WAIT_CYCLES);
                    if (rc) log_line("shim configure failed");
                }
                last_enabled = enabled;
                last_all = all;
                hook_state_now = st;
                if (st == HOOK_NO_PATCH_SYSTEM && enabled) {
                    /* ShellCore was not ready the first time: hook it again, a few times at most */
                    if (++no_ps >= 8 && attach_tries < 4) {
                        no_ps = 0;
                        attach_tries++;
                        log_line("ps-patch-system hook not found, trying again (%d)", attach_tries);
                        pspatch_attach(last_pid);
                    }
                } else {
                    no_ps = 0;
                }
                if (st == HOOK_ON)
                    set_status("Actif : %s", all ? "tous les jeux PS4 en mode Pro" : "jeux avec correctif active");
                else if (!enabled && st == HOOK_OFF) set_status("Desactive");
                else set_status("Hook ShellCore : %s", state_text(st));
                core.hook_ok = st == HOOK_ON;
                if (st != logged_st) {
                    logged_st = st;
                    log_line("state: %s (fw %x)", state_text(st), (unsigned)(kernel_get_fw_version() >> 16));
                }
            }
        }
        if (core.hook_ok && enabled && !all) {
            shell_t s;
            uint32_t seq; char tid[10]; uint64_t info;
            if (hook_state(&s) == HOOK_ON && !shim_question(&s, &seq, tid, &info) && seq != answered && seq) {
                int needs = game_needs(tid);
                unsigned char ans = (needs & (NEEDS_PRO | NEEDS_8GIB)) ? 1 : 0;
                int rc = shim_answer(&s, seq, ans, 0);
                answered = seq;
                log_line("launch %s: Pro %s (rc %d)", tid, ans ? "oui" : "non", rc);
            }
        }
        usleep(2000);
    }
    core.running = 0;
    return NULL;
}

int neo_core_start(int enabled, int all_games) {
    core.enabled = enabled;
    core.all_games = all_games;
    core.stop = 0;
    set_status("Demarrage");
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 512 * 1024);
    int rc = pthread_create(&core.thread, &attr, worker, NULL);
    pthread_attr_destroy(&attr);
    return rc;
}

void neo_core_set(int enabled, int all_games) {
    core.enabled = enabled;
    core.all_games = all_games;
}

void neo_core_stop(void) {
    core.stop = 1;
    pthread_join(core.thread, NULL);
}
