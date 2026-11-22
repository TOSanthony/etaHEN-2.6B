/* Copyright (C) 2025 etaHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#include "ipc.hpp"
#include "cheats/cheat_service.hpp"
#include "util_language.h"
#include "util_toolbox.h"
#include <eta/settings.hpp>
#include <eta/log_settings.hpp>
#include <eta/platform.h>
#include <eta/ucred.h>
#include <eta/proc_query.h>
#include <eta/ready.h>
extern "C" {
#include "freebsd-helper.h"
}
#include <cstdint>
#include <hijacker/hijacker.hpp>
#include <sys/_pthreadtypes.h>
#include <sys/syscall.h>
#include <unistd.h>

typedef struct app_info {
    uint32_t app_id;
    uint64_t unknown1;
    uint32_t app_type;
    char     title_id[10];
    char     unknown2[0x3c];
} app_info_t;

extern "C" {

    #include "faulthandler.h"
  #include "common_utils.h"
  #include <ps5/payload.h>

  int sceKernelGetAppInfo(pid_t pid, app_info_t * info);
  int sceKernelGetProcessName(int pid, char * out);
  int _sceApplicationGetAppId(int pid, uint32_t * appId);
  // set_proc_authid / get_proc_by_pid: libeta_proc
}

extern bool is_handler_enabled;

eta::SettingsStore g_settings;
void start_ip_thread(void);
void* IPC_loop(void* args);
bool shellui_patch(void);

jmp_buf g_catch_buf;
uintptr_t kernel_base = 0;
void* __stack_chk_guard = (void*)0xdeadbeef;

static void cleanup(void) {
    eta_notify(true, "notify.util.crash_recovering");
    exit(1);
}

void __stack_chk_fail(void) {
    LOG_DEBUG("Stack smashing detected.");
}

bool LoadSettings() {
    eta::Settings s{};
    if (!eta::settings_load(&s)) {
        LOG_ERROR("config.ini missing; using defaults (path primary=%s)",
                     eta::kConfigPathPrimary);
    } else {
        LOG_INFO("Loaded settings from %s", eta::settings_last_loaded_path());
    }

    const eta_log_level effective = eta::apply_log_settings(s);
    if (effective != static_cast<eta_log_level>(s.log_level)) {
        LOG_WARN("log level '%s' unavailable in this build; using '%s'",
                     eta_log_level_name(static_cast<eta_log_level>(s.log_level)),
                     eta_log_level_name(effective));
    }

    g_settings.store(s);
    util_apply_ui_language(s.ui_lang);

    /* Missing file is not an error — defaults were applied. */
    return true;
}

/** Initializes the utility service and publishes readiness after IPC startup. */
int main(void) {
    /* Raw elfldr uploads default to "payload.elf"; publish our stable name. */
    (void)syscall(SYS_thr_set_name, -1, "eta_util.elf");

    pthread_t ipc_server = 0;
    
    sceNetCtlInit();
    sceUserServiceInitialize(NULL);
    eta_log_configure(
        "etaHEN utils", "/data/etaHEN/etaHEN_util_daemon.log");
    /* Real linked kernel export (not a dlsym function-pointer variable). */
    eta_notify_set_send(reinterpret_cast<eta_notify_send_fn>(
        sceKernelSendNotificationRequest));
    LOG_INFO("util daemon entered");

    if (setjmp(g_catch_buf) == 0)
        LOG_DEBUG("jump has been set");
    else
        eta_notify(true, "notify.crash.resolved");

    LOG_DEBUG("Registering signal handler...");
    fault_handler_init(cleanup);
    LOG_DEBUG("   Success!");

    payload_args_t* args = payload_get_args();
    kernel_base = args->kdata_base_addr;
    /* Preserve the launch credential context for the SystemService query;
       util keeps PTRACE_AUTHID for its remaining lifetime. */
    (void)util_refresh_system_language();
    /* pt_* / code-cave require PTRACE_AUTHID (not DEBUG_AUTHID). */
    set_ucred_to_ptrace();

    unlink("/data/etaHEN/etaHEN_util_crash.log");

    LOG_INFO("=========== starting etaHEN Utilities... ===========");

    LoadSettings();

    start_ip_thread();
    const int ipc_start_result =
        pthread_create(&ipc_server, NULL, IPC_loop, NULL);
    if (ipc_start_result != 0) {
        LOG_ERROR("failed to start utility IPC server thread rc=%d",
                  ipc_start_result);
        eta_ready_clear(ETA_READY_UTIL);
        return 1;
    }
    /* The IPC thread exists; publish readiness only after thread creation. */
    if (!eta_ready_signal_pid(ETA_READY_UTIL, getpid())) {
        LOG_ERROR("failed to publish utility readiness pid=%d", (int)getpid());
        eta_ready_clear(ETA_READY_UTIL);
        return 1;
    }

    if (eta_ready_is_set(ETA_FLAG_UTIL_BOOTED)) {
        /* eta_util.elf restarted mid-session (crash recover / re-launch) — not rest. */
        LOG_WARN("util already booted once — toolbox reinject (not rest)");
        toolbox_reinject();
    }
    /* Mark that util completed cold start (typed flag; replaces util_first_boot file). */
    eta_ready_signal(ETA_FLAG_UTIL_BOOTED);

    LOG_INFO("Initializing cheat engine...");
    eta::cheats::CheatService::instance().ensureDir();

    for (;;) {
        sleep(1);
    }

    return 0;
}
