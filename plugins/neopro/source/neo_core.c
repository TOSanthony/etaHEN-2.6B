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
#include "plugin_lang.h"
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
#include "attr_detect.h"
#include "video_settings.h"

#define BM_LOG(...) log_line(__VA_ARGS__)
#include "../third_party/pspatch/shared/app_info.h"
#include "gpu_control.h"
#include "cpu_control.h"
#include "memory_control.h"

int pspatch_server_start(void);
int pspatch_attach(pid_t shellcore_pid);

static int force_neo, force_8gib;
static uint32_t fw_raw_version(void) {
    uint32_t v = 0; size_t n = sizeof(v);
    sysctlbyname("machdep.upd_version", &v, &n, NULL, 0);
    return v;
}
static void notify(const char *msg) { log_line("%s", msg); }
static int settings_save(void) { return 0; } /* the plugin saves its own settings file */
static void log_state(void);
static int memory_supported(void) { uint32_t fw = fw_raw_version(); return mb_find_layout(fw) != NULL || mbd_find_layout(fw) != NULL; }
static int memory_available(void) { mb_snapshot m; return memory_supported() && !memory_check(&m); }
static int gpu_available(void) { unsigned mhz; return gpu_backend_supported() && !gpu_backend_read(&mhz); }
static int cpu_available(void) { return cpu_backend_supported() && cpu_backend_responds(); }
#include "neo_manager.inc"

static void log_state(void) {
    shell_t s;
    int st = hook_state(&s);
    log_line("state: hook %s, 8GB Mode: %s, GPU: %s, CPU: %s", state_text(st), memory_message, gpu.message, cpu.message);
}

/* ---- game patches -------------------------------------------------------------------------------------------- */

/* Same flow as NeoEnabler's patch_sync(), but the XML files come from this ELF instead of GitHub. */
int neo_hz120_state(void) { return video_120hz_state(); }

int neo_hz120_set(int on) {
    hz_launch.active = 0;
    return video_120hz_set(on);
}

int neo_core_sync(char *msg, size_t cap) {
    ps_result r;
    memset(&r, 0, sizeof(r));
    ps_rename_count = 0;
    if (ps_mkdirs(PS_DIR)) {
        snprintf(msg, cap, TR("Dossier %s inaccessible", "Folder %s inaccessible"), PS_DIR);
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
    snprintf(msg, cap, TR("%d jeux, %d fichiers mis a jour, %d crees, %d inchanges%s", "%d games, %d files updated, %d created, %d unchanged%s"), r.games, r.files_updated,
             r.files_created, r.files_unchanged, r.errors ? TR(" (erreurs, voir le log)", " (errors, see the log)") : "");
    log_line("patches: %s", msg);
    return r.errors ? -1 : 0;
}

static volatile int mem_ok_cached; /* 8 GiB usable, refreshed by the worker (kernel access stays on its thread) */

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
        o->pro_on = o->on8 = o->has8 = 0;
        for (int k = 0; k < g[i].count; k++) {
            const gp_patch *p = &g[i].patches[k];
            int on = gp_setting(settings, sl, p->hash) == 1;
            if (p->needs_pro && !p->needs_8gib && on) o->pro_on = 1;
            if (p->needs_8gib && mem_ok_cached) o->has8 = 1;
            if (p->needs_8gib && on) o->on8 = 1;
        }
    }
    free(settings);
    games_free(g, count);
    return n;
}

/* mode 0: off, 1: PS4 Pro (first resolution entry without 8 GiB), 2: PS4 Pro with 8 GiB. */
int neo_game_set(const char *tid, int mode, char *msg, size_t cap) {
    char name[128], ver[16];
    if (!game_tid(tid) || game_info(tid, name, sizeof(name), ver, sizeof(ver))) {
        snprintf(msg, cap, TR("Jeu inconnu", "Unknown game"));
        return -1;
    }
    int n;
    gp_patch *p = game_patches(tid, ver, &n);
    if (!p || n <= 0) { free(p); snprintf(msg, cap, TR("Aucun correctif pour %s", "No patch for %s"), tid); return -1; }
    int rc = -1;
    if (mode) {
        const int want8 = mode == 2;
        int pick = -1;
        for (int i = 0; i < n && pick < 0; i++)
            if (p[i].resolution && p[i].needs_pro && !p[i].needs_8gib == !want8 && gp_main_elf(&p[i])) pick = i;
        for (int i = 0; i < n && pick < 0; i++)
            if (p[i].needs_pro && !p[i].needs_8gib == !want8) pick = i;
        if (pick < 0) snprintf(msg, cap, TR("Pas de correctif %s pour %s", "No %s patch for %s"), want8 ? "8 Go" : "Pro", tid);
        else rc = game_set_patch(tid, p[pick].hash, 1, mem_ok_cached, msg, cap);
    } else {
        size_t sl;
        char *settings = settings_read(&sl);
        rc = 0;
        for (int i = 0; i < n; i++) {
            if (!(p[i].needs_pro || p[i].needs_8gib) || gp_setting(settings, sl, p[i].hash) != 1) continue;
            if (game_set_patch(tid, p[i].hash, 0, mem_ok_cached, msg, cap)) rc = -1;
        }
        if (!rc) snprintf(msg, cap, "%s: mode Pro desactive", tid);
        free(settings);
    }
    free(p);
    return rc;
}

/* ---- worker -------------------------------------------------------------------------------------------------- */

static struct {
    neo_config cfg;           /* requested by the UI thread */
    volatile int dirty, stop, running;
    pthread_mutex_t lock;
    char status[200], detail[3][200];
    pthread_t thread;
} core = {.lock = PTHREAD_MUTEX_INITIALIZER};

void neo_core_summary(char *out, size_t cap) {
    pthread_mutex_lock(&core.lock);
    snprintf(out, cap, "%s|%s|%s|%s", core.status, core.detail[0], core.detail[1], core.detail[2]);
    pthread_mutex_unlock(&core.lock);
}

void neo_core_text(int which, char *out, size_t cap) {
    pthread_mutex_lock(&core.lock);
    snprintf(out, cap, "%s", which < 0 ? core.status : core.detail[which % 3]);
    pthread_mutex_unlock(&core.lock);
}

void neo_core_limits(neo_limits *l) {
    l->gpu_min = GPU_MIN_MHZ;
    l->gpu_max = gpu_model_max;
    l->cpu_min = CPU_MIN_MHZ;
    l->cpu_max = CPU_MAX_MHZ;
    l->memory_supported = memory_supported();
}

static void toolbox_wait(void) {
    /* etaHEN creates this marker once its own ShellCore/ShellUI setup is done. */
    for (int i = 0; i < 120 && !core.stop; i++) {
        if (access("/system_tmp/etahen/ready/toolbox", F_OK) == 0) return;
        sleep(1);
    }
}

static void publish(void) {
    shell_t s;
    int st = hook_state(&s);
    pthread_mutex_lock(&core.lock);
    if (!core.cfg.enabled) snprintf(core.status, sizeof core.status, TR("Desactive", "Disabled"));
    else if (st == HOOK_ON) snprintf(core.status, sizeof core.status, TR("Actif : %s", "Active: %s"),
                                     force_neo ? TR("tous les jeux PS4 en mode Pro", "all PS4 games in Pro mode") : TR("jeux avec correctif active", "games with an enabled patch"));
    else snprintf(core.status, sizeof core.status, TR("Hook ShellCore : %s", "ShellCore hook: %s"), state_text(st));
    snprintf(core.detail[0], sizeof core.detail[0], "%.190s", memory_supported() ? memory_message : TR("Mode 8 Go non supporte sur ce firmware", "8 GB mode not supported on this firmware"));
    snprintf(core.detail[1], sizeof core.detail[1], "%.190s", gpu.message);
    snprintf(core.detail[2], sizeof core.detail[2], "%.190s", cpu.message);
    pthread_mutex_unlock(&core.lock);
}

/* Applies what the UI asked for. Runs on the worker thread: kernel pipes are shared state. */
static void apply_config(int first) {
    neo_config c;
    pthread_mutex_lock(&core.lock);
    c = core.cfg;
    pthread_mutex_unlock(&core.lock);
    static neo_config applied;
    static int have;
    if (have && !memcmp(&applied, &c, sizeof c)) return;

    int all_changed = !have || applied.all_games != c.all_games;
    force_neo = c.all_games;
    if (!have || applied.force_8gib != c.force_8gib) {
        force_8gib = c.force_8gib && memory_supported();
        if (!first) {
            if (c.force_8gib && !memory_available()) { force_8gib = 0; log_line("8 Go force ignore : mode 8 Go indisponible"); }
            if (memory_available()) memory_request(force_8gib);
        }
    }
    if (!have || applied.gpu_on != c.gpu_on || applied.gpu_mhz != c.gpu_mhz) {
        if (first) gpu_configure(c.gpu_on, c.gpu_mhz);
        else {
            int rc = gpu_request(c.gpu_on, c.gpu_mhz, gpu_game_pid());
            log_line("GPU: %s (rc %d)", gpu.message, rc);
        }
    }
    if (!have || applied.cpu_on != c.cpu_on || applied.cpu_mhz != c.cpu_mhz) {
        if (first) cpu_configure(c.cpu_on, c.cpu_mhz);
        else {
            int rc = cpu_request(c.cpu_on, c.cpu_mhz, gpu_game_pid());
            log_line("CPU: %s (rc %d)", cpu.message, rc);
        }
    }
    if (have && applied.enabled != c.enabled) {
        shell_t s;
        int st = hook_state(&s);
        if (!c.enabled && st == HOOK_ON) {
            launch_attach(0);
            log_line("NEO hook removal rc=%d", hook_set(0));
            live_ok = 0;
        }
    }
    if (all_changed && c.enabled) launch_attach(1);
    applied = c;
    have = 1;
}

static void *worker(void *arg) {
    (void)arg;
    pid_t last_pid = -1;
    char msg[256];

    toolbox_wait();
    neo_core_sync(msg, sizeof(msg));
    if (pspatch_server_start() == 0) log_line("patch server started");
    else log_line("patch server failed to start");

    console_detect_model();
    apply_config(1);

    /* wait for ShellCore and hook it with ps-patch-system, then install the NeoEnabler shim */
    for (int i = 0; i < 60 && !core.stop; i++) {
        pid_t pid = find_proc("SceShellCore");
        if (pid > 0) {
            log_line("SceShellCore pid %d: installing the ps-patch-system hook", pid);
            sleep(3);
            pspatch_attach(pid);
            last_pid = pid;
            break;
        }
        sleep(1);
    }
    core.running = 1;
    if (core.cfg.enabled) {
        shell_t s;
        for (int i = 0; i < 20 && hook_state(&s) != HOOK_OFF && !core.stop; i++) sleep(1);
        log_line("NEO hook install rc=%d", hook_set(1));
    }
    tsc_hz = tsc_rate();
    launch_attach(1);
    log_line("launch: questions %s (TSC %llu Hz, wait %d s)", live_ok ? "enabled" : "unavailable, hook not installed",
             (unsigned long long)tsc_hz, LAUNCH_WAIT_S);
    bm_reuse_allowed = previous_run_gone();
    memory_init(fw_raw_version(), tsc_hz * 3);
    record_run();
    memory_wants8 = memory_launch_wants;
    memory_refused8 = memory_launch_refused;
    if (bm.attached && !memory_data_blocked && bm_field(MS_ENABLED, 1)) {
        memory_data_blocked = 1;
        log_line("No-HV mount hook enable failed; reboot required");
    }
    apply_config(0);
    drop_8gib_request();
    if (force_8gib) memory_request(1);
    int memory_previous_result = memory_poll();
    log_line("8GB Mode: %s", memory_message);
    mem_ok_cached = memory_available();
    gpu_tick();
    cpu_tick();
    patches_drop_8gib(NULL);
    log_state();
    publish();

    time_t last_slow = time(NULL);
    while (!core.stop) {
        memory_data_tick();
        launch_poll();
        time_t now = time(NULL);
        if (now < last_slow || now - last_slow >= 2) {
            last_slow = now;
            pid_t pid = find_proc("SceShellCore");
            if (pid > 0 && pid != last_pid) {
                log_line("SceShellCore pid %d (restart): installing the ps-patch-system hook", pid);
                sleep(3);
                pspatch_attach(pid);
                last_pid = pid;
                live_ok = 0;
            }
            apply_config(0);
            launch_attach(-1);
            if (core.cfg.enabled) hook_retry();
            launch_tick();
            settings_watch();
            gpu_tick();
            cpu_tick();
            int memory_result = memory_poll();
            if (memory_result != memory_previous_result) {
                log_line("8GB Mode: %s (result %d)", memory_message, memory_result);
                drop_8gib_request();
                log_state();
                patches_drop_8gib(NULL);
            }
            memory_previous_result = memory_result;
            mem_ok_cached = memory_available();
            publish();
        }
        usleep(pending.active ? 2000 : 20000);
    }

    /* Stop: never ask again, give the clocks and the budget back. */
    launch_attach(0);
    for (int i = 0; i < 30; i++) {
        memory_data_tick();
        if (!memory_shutdown() && !clocks_restore()) break;
        sleep(1);
    }
    core.running = 0;
    return NULL;
}

int neo_core_start(const neo_config *cfg) {
    core.cfg = *cfg;
    core.stop = 0;
    snprintf(core.status, sizeof core.status, TR("Demarrage", "Starting"));
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1024 * 1024);
    int rc = pthread_create(&core.thread, &attr, worker, NULL);
    pthread_attr_destroy(&attr);
    return rc;
}

void neo_core_set(const neo_config *cfg) {
    pthread_mutex_lock(&core.lock);
    core.cfg = *cfg;
    pthread_mutex_unlock(&core.lock);
}

void neo_core_stop(void) {
    core.stop = 1;
    pthread_join(core.thread, NULL);
}
