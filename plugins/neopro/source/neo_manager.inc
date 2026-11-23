/* GPL-3.0-only. Extracted unchanged from NeoEnabler src/manager.c (gasb07/NeoEnabler): console model, game
   detection, clock restore, launch questions, 8 GiB and 120 Hz bookkeeping and previous-run detection. The web
   panel is not part of this plugin. Included by neo_core.c. */
/* 120 Hz video output: a flag of ps-patch-system itself (apply_120hz_videoout in its !_settings.xml), not a NeoEnabler
   setting, so it is read from and written to that file and nothing is kept in /data/neo-settings. Only that
   attribute changes; the file is replaced in one step so a failed write leaves the old one. */
static char *video_xml_read(size_t *n) {
    size_t len = 0; char *raw = ps_read(HZ120_XML, 64 << 10, &len);
    if (!raw) {
        static int logged;
        if (!logged) { logged = 1; log_line("120 Hz: cannot read %s", HZ120_XML); }
        return NULL;
    }
    char *t = malloc(len + 1);
    if (t) { memcpy(t, raw, len); t[len] = 0; }
    free(raw); *n = len;
    return t;
}
/* 1 on, 0 off, -1 unavailable (file missing/unreadable or no recognisable flag). */
static int video_120hz_state(void) {
    size_t n; char *t = video_xml_read(&n);
    if (!t) return -1;
    int r = hz120_get(t); free(t);
    return r;
}
static int video_120hz_set(int on) {
    size_t n; char *t = video_xml_read(&n);
    if (!t) return -1;
    size_t cap = n + 64; char *out = malloc(cap);
    int len = out ? hz120_set(t, on, out, cap) : -1;
    free(t);
    int rc = -1;
    if (len >= 0 && !write_file(HZ120_XML ".neo.tmp", out, (size_t)len)) rc = rename(HZ120_XML ".neo.tmp", HZ120_XML);
    free(out);
    return rc;
}

/* 120 Hz video output for a launch, like 8GB Mode: turned on when the game being started has an enabled entry that
   needs it, and turned off again once no game runs, unless it was already on (the user's own setting). */
static struct { int active, seen_game; char tid[10]; time_t since; } hz_launch;

static void hz120_launch(const char *tid, int needs) {
    if (!(needs & NEEDS_120HZ)) {
        if (hz_launch.active && !video_120hz_set(0)) log_line("120 Hz: %s does not need it, turned off", tid);
        hz_launch.active = 0;
        return;
    }
    int state = video_120hz_state();
    if (state == 0) {
        if (video_120hz_set(1)) { log_line("120 Hz: %s needs the 120 Hz video output, could not turn it on", tid); return; }
        log_line("120 Hz: %s needs the 120 Hz video output, turned on for this launch", tid);
    } else if (state < 0) { log_line("120 Hz: %s needs the 120 Hz video output, ps-patch-system settings not found", tid); return; }
    if (state == 0 || hz_launch.active) {
        hz_launch.active = 1; hz_launch.seen_game = 0; hz_launch.since = time(NULL);
        snprintf(hz_launch.tid, sizeof hz_launch.tid, "%s", tid);
    }
}

/* Console model: PS5 Pro model numbers are CFI-7xxx (CFI-7000, CFI-7100); every regular PS5 is CFI-1xxx or CFI-2xxx.
   The model string comes from machdep.icc.hw_model. Anything unreadable or unrecognised counts as a regular PS5,
   so the GPU target stays capped at 2200 MHz unless a Pro is positively identified. */
static int console_is_pro;
static void console_detect_model(void) {
    char model[64] = {0}; size_t n = sizeof(model) - 1;
    if (sysctlbyname("machdep.icc.hw_model", model, &n, NULL, 0)) n = 0;
    model[n < sizeof(model) ? n : sizeof(model) - 1] = 0;
    for (char *p = model; *p; p++) if (*p < 32 || *p > 126) *p = ' ';
    console_is_pro = strstr(model, "CFI-7") != NULL;
    gpu_set_model(console_is_pro);
    log_line("console: model \"%s\" -> %s, GPU target limit %u MHz", model[0] ? model : "unknown",
             console_is_pro ? "PS5 Pro" : "PS5", gpu_model_max);
}

/* Advanced overrides: Pro mode for every PS4 game, and 8 GiB kept on. */

/* A process that can be a PS4 game: the main eboot.bin, or the game's own executable when the title starts one
   (collections such as the Devil May Cry HD Collection run dmc1.elf, dmc2.elf and dmc3.elf from a launcher).
   Only the name is tested here; callers still require an app info with a title ID and a PS4 graphics driver. */
static int game_proc_name(const char *comm) {
    size_t l = strlen(comm);
    return !strcmp(comm, "eboot.bin") || (l > 4 && !strcmp(comm + l - 4, ".elf"));
}

/* A process counts as a PS4 game when it is eboot.bin, or has a non-system app title ID (not NPXS) and either an .elf
   executable name or a loaded PS4 graphics driver (base, Pro or Compat1). The second test finds games
   started from a launcher under another executable name. System processes (Sce*) are skipped without a query. */
static int game_proc(const struct kinfo_proc *ki) {
    if (!strcmp(ki->ki_comm, "eboot.bin")) return 1;
    if (!strncmp(ki->ki_comm, "Sce", 3)) return 0;
    app_info_t info = {0}; uint32_t h = 0;
    if (sceKernelGetAppInfo(ki->ki_pid, &info) || !info.title_id[0]) return 0;
    /* payloads (*.elf) can run under a system app ID (NPXS...): never a game */
    if (!strncmp(info.title_id, "NPXS", 4)) return 0;
    if (game_proc_name(ki->ki_comm)) return 1;
    return !kernel_dynlib_handle(ki->ki_pid, "libSceGnmDriverForNeoMode.sprx", &h) ||
           !kernel_dynlib_handle(ki->ki_pid, "libSceGnmDriver.sprx", &h) ||
           !kernel_dynlib_handle(ki->ki_pid, "libSceGnmDriverCompat1.sprx", &h);
}

/* Title ID of the open PS4 game (an eboot.bin process with one); 1 when there is one. */
static int open_game(char tid[10]) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0}; size_t n = 0; int found = 0;
    tid[0] = 0;
    if (sysctl(mib, 4, NULL, &n, NULL, 0) || !n || n > 32 * 1024 * 1024) return 0;
    unsigned char *buf = malloc(n);
    if (!buf || sysctl(mib, 4, buf, &n, NULL, 0)) { free(buf); return 0; }
    for (size_t off = 0; !found && off + sizeof(struct kinfo_proc) <= n;) {
        struct kinfo_proc *ki = (void *)(buf + off);
        if (ki->ki_structsize < (int)sizeof(*ki) || (size_t)ki->ki_structsize > n - off) break;
        app_info_t info = {0};
        if (game_proc(ki) && !sceKernelGetAppInfo(ki->ki_pid, &info) && info.title_id[0]) {
            snprintf(tid, 10, "%.9s", info.title_id);
            found = 1;
        }
        off += (size_t)ki->ki_structsize;
    }
    free(buf);
    return found;
}

/* A unique PS4 game is the only automatic clock target: its eboot.bin, or its own .elf when a launcher eboot.bin
 * starts it (Devil May Cry HD Collection). The launcher and the game it starts share one title ID, and then the
 * .elf is the target. Native games, game processes of different titles, failed scans never trigger boost. */
static int gpu_game_pid(void) {
    int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0}; size_t n = 0;
    if (sysctl(mib, 4, NULL, &n, NULL, 0) || !n || n > 32 * 1024 * 1024) return -1;
    unsigned char *buf = malloc(n);
    if (!buf) return -1;
    if (sysctl(mib, 4, buf, &n, NULL, 0)) { free(buf); return -1; }
    int candidate = 0, candidate_elf = 0, valid = 1; size_t off = 0;
    char first[10] = "";
    while (off + sizeof(struct kinfo_proc) <= n) {
        struct kinfo_proc *ki = (void *)(buf + off);
        if (ki->ki_structsize < (int)sizeof(*ki) || (size_t)ki->ki_structsize > n - off) { valid = 0; break; }
        if (ki->ki_pid != getpid() && game_proc(ki)) {
            app_info_t info = {0}; uint32_t h = 0;
            int is_elf = strcmp(ki->ki_comm, "eboot.bin") != 0;
            int have_info = !sceKernelGetAppInfo(ki->ki_pid, &info) && info.title_id[0];
            /* Only a process with a title ID and a PS4 graphics driver counts. Payloads (payload.elf, kstuff.elf, ...)
             * and system .elf processes (NPXS titles) match the name test but are not games and must not take part
             * in the title comparison below. */
            int ps4 = have_info &&
                (!kernel_dynlib_handle(ki->ki_pid, "libSceGnmDriverForNeoMode.sprx", &h) ||
                 !kernel_dynlib_handle(ki->ki_pid, "libSceGnmDriver.sprx", &h) ||
                 !kernel_dynlib_handle(ki->ki_pid, "libSceGnmDriverCompat1.sprx", &h));
            if (ps4) {
                if (!first[0]) snprintf(first, sizeof first, "%.9s", info.title_id);
                else if (strncmp(first, info.title_id, 9)) valid = 0;   /* two different titles open */
                if (!candidate || (is_elf && !candidate_elf)) { candidate = ki->ki_pid; candidate_elf = is_elf; }
                else if (is_elf == candidate_elf) valid = 0;            /* two game processes of the same kind */
            } else if (!is_elf && !have_info) {
                valid = 0;      /* an eboot.bin we cannot identify is still ambiguous */
            }
        }
        off += (size_t)ki->ki_structsize;
    }
    if (off != n) valid = 0;
    free(buf); return valid ? candidate : -1;
}

/* Restore both clocks; both are always attempted. Nonzero when either failed. */
static int clocks_restore(void) {
    int pid = gpu_game_pid(); int g = gpu_request(0, gpu.target, pid);
    int c = cpu_request(0, cpu.target, pid);
    if (c) c = cpu_request(0, cpu.target, pid);   /* the second try gives up cleanly: a stuck CPU restore never blocks Quit */
    if (cpu.baseline_valid == 0 && cpu.message[0] && strstr(cpu.message, "Could not restore")) log_line("CPU: %s", cpu.message);
    return g || c;
}

static void gpu_tick(void) {
    static unsigned previous_actions;
    static int previous_result = 9999;
    int result = gpu_poll(gpu_game_pid());
    if (result != previous_result || gpu.actions != previous_actions) {
        log_line("GPU: %s (result %d)", gpu.message, result);
        previous_actions = gpu.actions; previous_result = result;
    }
}

static void cpu_tick(void) {
    static unsigned previous_actions;
    static int previous_result = 9999;
    int result = cpu_poll(gpu_game_pid());
    if (result != previous_result || cpu.actions != previous_actions) {
        log_line("CPU: %s (result %d)", cpu.message, result);
        previous_actions = cpu.actions; previous_result = result;
    }
}

/* Launch questions. The hook posts the title ID of every PS4 launch and waits up to LAUNCH_WAIT_S seconds; the
   manager answers Pro mode when an enabled patch of that game needs it, after switching the 8 GiB budget to what
   the launch needs (the kernel reads it when it creates the game process). 8 GiB switched on for a launch returns
   to the original budget once the game has closed. */
#define LAUNCH_WAIT_S 5
static shell_t live;
static int live_ok;
static uint32_t last_seq;
static uint64_t tsc_hz = 1600000000ULL;
static struct { int active, needs; uint32_t seq, attr_off; char tid[10]; time_t since; } pending;
static struct { char tid[10]; uint32_t offset; } last_attr;   /* last detected ATTRIBUTE offset, for /status */
static struct { int active, seen_game; char tid[10]; time_t since; } auto8;

static uint64_t rdtsc(void) { uint32_t lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo; }

static uint64_t tsc_rate(void) {
    struct timespec a, c;
    clock_gettime(CLOCK_MONOTONIC, &a); uint64_t t0 = rdtsc();
    usleep(100000);
    clock_gettime(CLOCK_MONOTONIC, &c); uint64_t t1 = rdtsc();
    double sec = (double)(c.tv_sec - a.tv_sec) + (double)(c.tv_nsec - a.tv_nsec) / 1e9;
    return sec > 0.05 && t1 > t0 ? (uint64_t)((double)(t1 - t0) / sec) : 1600000000ULL;
}

/* A patch that needs 8 GiB cannot work while 8 GiB is not available (no hypervisor exploit, or an unsupported
   firmware): turns off the enabled ones, of one game or of every game, and returns how many. */
static int patches_drop_8gib(const char *only_tid) {
    if (memory_available()) return 0;
    int dropped = games_drop_8gib(only_tid, memory_message);
    if (dropped) {
        char note[200];
        snprintf(note, sizeof note, "%d patch%s that need 8GB Mode turned off: 8GB Mode is not available", dropped, dropped == 1 ? "" : "es");
        notify(note);
    }
    return dropped;
}

/* A launch that wanted 8 GiB was released without it: its 8 GiB patches are turned off before ps-patch-system reads
   the settings for that game (it does when the game process starts, after the mount). */
static void memory_launch_refused(const char *tid) {
    if (!game_tid(tid)) return;
    int dropped = games_drop_8gib(tid, "8GB Mode was not applied to this launch");
    if (dropped) {
        char note[200];
        snprintf(note, sizeof note, "8GB Mode could not be applied to %s: %d patch%s that need it turned off", tid, dropped, dropped == 1 ? "" : "es");
        notify(note);
    }
}

/* Patches can also be turned on in the ps-patch-system Patch Manager: when its settings file changes, 8 GiB patches
   are turned off again while 8 GiB is not available. */
static void settings_watch(void) {
    static time_t mtime; static off_t size = -1;
    struct stat st;
    if (stat(SETTINGS_XML, &st) || (st.st_mtime == mtime && st.st_size == size)) return;
    mtime = st.st_mtime; size = st.st_size;
    patches_drop_8gib(NULL);
}

/* "Keep 8GB Mode on" is a request for the budget. Without 8 GiB (no hypervisor exploit, or an unsupported firmware) there
   is nothing to request: it is cleared and saved, so it does not stay on and come back at the next unlock. */
static void drop_8gib_request(void) {
    if (!force_8gib || memory_available()) return;
    force_8gib = 0;
    int saved = !settings_save();
    log_line("Keep 8GB Mode on: cleared, 8GB Mode is not available (needs firmware 13.20 or 13.60, 10.01, 12.20 and 12.70 experimental, with a validated memory launch hook)%s",
             saved ? "" : " (settings not saved)");
}

/* Called after the hook changes and every two seconds: questions are only read from a live hook. */
static void launch_attach(int ask) {
    int was = live_ok;
    live_ok = shim_live(&live);
    if (!live_ok) return;
    if (!was) {
        char tid[10]; uint64_t info;
        if (shim_question(&live, &last_seq, tid, &info)) { live_ok = 0; return; }   /* earlier questions are not ours */
        if (ask < 0) log_line("launch: questions enabled");   /* hook installed after the start */
    }
    if (!was || ask >= 0) shim_configure(&live, force_neo, ask == 0 ? 0 : tsc_hz * LAUNCH_WAIT_S);
}

/* ATTRIBUTE offset in the app info of this launch (0: the shim uses its table offset). Logs the choice, and the
   app info around the known offsets when the table offset is not the one found, so a report shows the layout. */
static uint32_t launch_attr_offset(const char *tid, uint64_t info) {
    static unsigned char buf[AD_INFO_SIZE];
    char path[96]; size_t n = 0;
    uint32_t attr, attr2 = 0;
    char *sfo = ps_read(appmeta_file(tid, "param.sfo", path, sizeof path), 64 << 10, &n);
    int has_attr = sfo && !ad_sfo_int((unsigned char *)sfo, n, "ATTRIBUTE", &attr);
    int has_attr2 = sfo && !ad_sfo_int((unsigned char *)sfo, n, "ATTRIBUTE2", &attr2);
    free(sfo);
    if (!has_attr) { log_line("launch: %s: ATTRIBUTE not found in %s, using the table offset 0x%x", tid, path, live.attr_off); return 0; }
    if (!info || kernel_proc_copyout(live.pid, (uintptr_t)info, buf, sizeof buf)) {
        log_line("launch: %s: app info at %llx unreadable, using the table offset 0x%x", tid, (unsigned long long)info, live.attr_off);
        return 0;
    }
    ad_result r; ad_pick(buf, sizeof buf, attr, has_attr2, attr2, live.attr_off, &r);
    char list[AD_MAX_CANDIDATES * 7 + 8] = ""; size_t used = 0;
    for (int i = 0; i < r.count && i < AD_MAX_CANDIDATES; i++)
        used += (size_t)snprintf(list + used, sizeof list - used, "%s0x%x", i ? " " : "", r.candidates[i]);
    log_line("launch: %s: ATTRIBUTE 0x%x%s, matches at [%s]%s, %s: %s 0x%x (table 0x%x)", tid, attr,
             has_attr2 ? "" : " (no ATTRIBUTE2)", list, r.count > AD_MAX_CANDIDATES ? " and more" : "", r.why,
             r.offset ? "using" : "keeping", r.offset ? r.offset : live.attr_off, live.attr_off);
    if (r.offset != live.attr_off) {
        char dump[400];   /* log lines hold 512 characters */
        for (size_t from = 0x1c0; from < 0x2a0; from += 0x70) {
            ad_dump(buf, sizeof buf, from, from + 0x70, dump, sizeof dump);
            log_line("launch: %s: app info %s", tid, dump);
        }
    }
    snprintf(last_attr.tid, sizeof last_attr.tid, "%s", tid);
    last_attr.offset = r.offset;
    return r.offset;
}

/* The hook can only be installed after ps-patch-system has redirected the getAppInfo call. When NeoEnabler starts
   first (payloads sent together or again), the install fails, so every 2 s, while the hook is not live, it is
   installed as soon as the call points to the ps-patch-system hook (or to an older NeoEnabler shim). */
static void hook_retry(void) {
    static int last_rc = -1;
    if (live_ok) return;
    shell_t s; int st = hook_state(&s);
    if (st != HOOK_OFF && st != HOOK_OLD) return;
    int rc = hook_set(1);
    if (rc) {
        if (rc != last_rc) log_line("hook retry: failed to enable (rc=%d, state: %s)", rc, state_text(st));
        last_rc = rc;
        return;
    }
    last_rc = -1;
    notify("NeoEnabler enabled: ps-patch-system finished loading after NeoEnabler started");
    log_state();   /* questions start at the next check: right after the install ShellCore can still be resuming */
}

static void launch_poll(void) {
    if (!live_ok) return;
    if (!pending.active) {
        uint32_t seq; char tid[10]; uint64_t info;
        if (shim_question(&live, &seq, tid, &info) || seq == last_seq) return;
        last_seq = seq;
        pending.active = 1; pending.seq = seq; pending.since = time(NULL);
        snprintf(pending.tid, sizeof pending.tid, "%s", tid);
        pending.attr_off = launch_attr_offset(tid, info);
        patches_drop_8gib(tid);
        pending.needs = game_needs(tid);
        hz120_launch(tid, pending.needs);
        if (force_neo) pending.needs |= NEEDS_PRO;
        log_line("launch: %s needs %s%s", tid, pending.needs & NEEDS_PRO ? "Pro mode" : "base mode",
                 pending.needs & NEEDS_8GIB ? " and 8GB Mode" : "");
    }
    int pro = (pending.needs & NEEDS_PRO) != 0, ok = 1;
    int mem_ok = memory_available();
    int want8 = mem_ok && (force_8gib || (pending.needs & NEEDS_8GIB));
    /* the budget only applies to Pro mode processes */
    if (pro && mem_ok) {
        int rc = memory_request(want8);
        ok = rc == 0;
        if (!ok && time(NULL) - pending.since < LAUNCH_WAIT_S - 1) return;   /* e.g. the previous game still closing */
        if (!ok) {
            mb_snapshot m;   /* keep the budget as it is instead of switching it later, after this game */
            if (!memory_check(&m)) memory_target = m.state == MB_8GIB;
            log_line("launch: %s: memory not switched (%s)", pending.tid, memory_message);
        }
        else if (want8 && !force_8gib) {
            auto8.active = 1; auto8.seen_game = 0; auto8.since = time(NULL);
            snprintf(auto8.tid, sizeof auto8.tid, "%s", pending.tid);
        } else if (!want8) auto8.active = 0;
    }
    shim_answer(&live, pending.seq, (unsigned char)pro, pending.attr_off);
    pending.active = 0;
    log_line("launch: %s starts in %s mode%s", pending.tid, pro ? "Pro" : "base",
             pro && mem_ok && ok ? (want8 ? " with 8GB Mode" : " with the original memory") : "");
}

/* 8 GiB switched on for a launch goes back to the original budget when no game runs any more. */
static void launch_tick(void) {
    if (hz_launch.active) {
        int games = mb_games();
        if (games > 0) hz_launch.seen_game = 1;
        else if (games == 0 && (hz_launch.seen_game || time(NULL) - hz_launch.since >= 60)) {
            hz_launch.active = 0;
            log_line("120 Hz: %s closed: %s", hz_launch.tid, video_120hz_set(0) ? "could not turn it off" : "turned off");
        }
    }
    if (!auto8.active || force_8gib) return;
    int games = mb_games();
    if (games > 0) { auto8.seen_game = 1; return; }
    if (games < 0 || (!auto8.seen_game && time(NULL) - auto8.since < 60)) return;
    auto8.active = 0;
    memory_request(0);
    log_line("8GB Mode: %s closed: %s", auto8.tid, memory_message);
}

static int memory_launch_wants(const char *tid) {
    if (!game_tid(tid)) return 0;
    int needs = game_needs(tid);
    return force_8gib || (needs & NEEDS_8GIB); /* actual Pro mode is checked in HELD */
}

/* Start time of a running process (KERN_PROC_PROC enumeration, the tested query on this target); 0 when found. */
static int proc_start(pid_t pid, struct timeval *start) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0}; size_t n = 0; int found = 0;
    if (sysctl(mib, 4, NULL, &n, NULL, 0) || !n || n > 32 * 1024 * 1024) return -1;
    unsigned char *buf = malloc(n);
    if (!buf || sysctl(mib, 4, buf, &n, NULL, 0)) { free(buf); return -1; }
    for (size_t off = 0; !found && off + sizeof(struct kinfo_proc) <= n;) {
        struct kinfo_proc *ki = (void *)(buf + off);
        if (ki->ki_structsize < (int)sizeof(*ki) || (size_t)ki->ki_structsize > n - off) break;
        if (ki->ki_pid == pid) { *start = ki->ki_start; found = 1; }
        off += (size_t)ki->ki_structsize;
    }
    free(buf);
    return found ? 0 : 1;
}

/* The previous run can stay alive without a working panel (rest mode), still owning its launch hook. Each run
   records its PID and process start time; a process with both values is that run and nothing else, even after a
   reboot reuses the PID. Returns 1 when the previous run is confirmed gone (exited, quit, or killed here), 0 when
   it is unknown (no record: first run or 0.4.0 and older), and passes on the slot it recorded. */
#define MANAGER_PID_FILE NEO_LOG_DIR "/manager.pid"
static int previous_run_gone(void) {
    int gone = 0; long pid = 0, sec = 0, usec = 0; unsigned long slot = 0;
    FILE *f = fopen(MANAGER_PID_FILE, "r");
    if (f) {
        if (fscanf(f, "%ld %ld %ld %lx", &pid, &sec, &usec, &slot) >= 3 && pid > 0 && pid != getpid()) {
            struct timeval st;
            int r = proc_start((pid_t)pid, &st);
            if (r == 1 || (!r && (st.tv_sec != sec || st.tv_usec != usec))) {
                gone = 1;
                log_line("previous run (PID %ld) has exited", pid);
            } else if (!r && open_game((char[10]){0})) {
                /* it may own an 8GB Mode transaction that it restores when the game closes */
                log_line("previous run (PID %ld) is still running and a game is open; left running", pid);
            } else if (!r) {
                log_line("previous run (PID %ld) is still running without its panel; stopping it", pid);
                if (!kill((pid_t)pid, SIGKILL))
                    for (int i = 0; i < 50 && !gone; i++) {
                        usleep(100000);
                        if (proc_start((pid_t)pid, &st) == 1) gone = 1;
                    }
                log_line("previous run (PID %ld) %s", pid, gone ? "stopped" : "did not stop; its launch hook is left alone");
            }
        }
        fclose(f);
    }
    if (gone) bm_reuse_slot = (uint32_t)slot;
    return gone;
}

/* Records this run (PID, start time, launch-hook slot offset) for the next start; see previous_run_gone. */
static void record_run(void) {
    struct timeval me;
    if (proc_start(getpid(), &me)) return;
    char line[96];
    int n = snprintf(line, sizeof(line), "%ld %ld %ld %lx\n", (long)getpid(), (long)me.tv_sec, (long)me.tv_usec,
                     bm.attached ? (unsigned long)(bm.cave - bm.base) : 0UL);
    int fd = open(MANAGER_PID_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    if (n > 0 && write(fd, line, (size_t)n) != n) log_line("process record: write failed");
    close(fd);
}
