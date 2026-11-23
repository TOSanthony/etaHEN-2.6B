/* Runs the Welcome Hub bridge (hook into SceShellUI, widget asset server and
 * the built-in PSN "signed in" patch) inside the plugin process. Logic taken
 * from TOSanthony/dashboard main.c, minus tile visibility (handled by the
 * plugin settings) and the NTP clock sync (separate NTP plugin). */
#include "wh_runner.h"

#include <pthread.h>
#include <stdio.h>

#include "cave.h"
#include "common.h"
#include "install.h"
#include "prelude.h"
#include "server.h"
#include "util.h"

static pthread_t g_thread;
static volatile int g_started = 0;
static int g_restart_shell = 0;
static int g_restart_delay = 10;

extern int sceKernelTerminateProcess(int pid, int *ret);
#define DECID_AUTH_ID UINT64_C(0x4800000000000022)

/* Same method as the etaHEN daemon (ForceKillProc): temporarily raise the
 * authid so sceKernelTerminateProcess is allowed; the system respawns the
 * home menu afterwards. */
static int restart_shellui(pid_t pid) {
  if (pid <= 1 || pid == getpid()) return -1;
  uint64_t old = kernel_get_ucred_authid(getpid());
  kernel_set_ucred_authid(getpid(), DECID_AUTH_ID);
  int ret = 0;
  int rc = sceKernelTerminateProcess(pid, &ret);
  kernel_set_ucred_authid(getpid(), old);
  if (rc != 0) { LOG("restart: sceKernelTerminateProcess(%d) failed, SIGKILL fallback\n", (int)pid); rc = kill(pid, SIGKILL); }
  return rc;
}

/* etaHEN publishes /system_tmp/etahen/ready/toolbox = pid of the SceShellUI it
 * finished hooking. Touching the shell (ptrace / restart) while etaHEN is still
 * installing its own hooks makes both fail, so wait for that marker. */
#define ETA_TOOLBOX_READY "/system_tmp/etahen/ready/toolbox"

static int toolbox_ready_pid(void) {
    FILE *f = fopen(ETA_TOOLBOX_READY, "r");
    if (!f) return -1;
    int v = -1;
    if (fscanf(f, "%d", &v) != 1) v = -1;
    fclose(f);
    return v;
}

/* pid <= 0: any ready marker is fine. Returns 1 when ready, 0 on timeout. */
static int wait_toolbox_ready(pid_t pid, int timeout_s) {
    for (int i = 0; i < timeout_s; i++) {
        int rp = toolbox_ready_pid();
        if (rp > 0 && (pid <= 0 || rp == (int)pid)) { sleep(3); return 1; } /* settle */
        sleep(1);
    }
    return 0;
}

static void *bridge_thread(void *arg) {
  (void)arg;
  wh_flog("installer", "---- WHBridge %s start (etaHEN plugin) ----", WH_VERSION);
  pid_t pid = find_pid("SceShellUI");
  /* At boot the plugin can start before the shell is up: wait up to ~60 s. */
  for (int i = 0; pid < 0 && i < 60; i++) { sleep(1); pid = find_pid("SceShellUI"); }
  if (pid < 0) { notify("WHBridge failed: ShellUI not found"); return NULL; }
  if (!wait_toolbox_ready(pid, 120))
    LOG("etaHEN toolbox not ready after 120 s, continuing anyway\n");
  { pid_t cur = find_pid("SceShellUI"); if (cur > 0) pid = cur; }
  int want_dump = (access(WH_DUMP_FLAG, F_OK) == 0);
  int reload = 0;
  uint64_t widgets_gen = 0;
  int rc = wh_install_once_g(pid, want_dump, &reload, &widgets_gen);
  const char *hook = rc == 0 ? (reload ? "WHBridge reloaded" : "WHBridge loaded")
                  : rc == 15 ? "WHBridge failed: hook owned by something else" : "WHBridge failed";
  int plain = (rc == 0 || rc == 15);
  char wsum[300] = "";
  if (rc == 0) {
    if (g_wbad) snprintf(wsum, sizeof wsum, "\nWidgets: %d loaded, %d failed (%s%s)", g_wok, g_wbad, g_wfail, g_wmore ? ", ..." : "");
    else snprintf(wsum, sizeof wsum, "\nWidgets: %d loaded", g_wok);
  }
  if (rc == 0 && g_restart_shell) {
    LOG("restart: waiting %d s before restarting SceShellUI (pid %d)\n", g_restart_delay, (int)pid);
    sleep((unsigned)g_restart_delay);
    {
      pid_t cur = find_pid("SceShellUI");
      if (cur > 0) pid = cur; /* the shell may have been restarted meanwhile */
    }
    if (!wait_toolbox_ready(pid, 120))
      LOG("restart: etaHEN toolbox still not ready for pid %d, restarting anyway\n", (int)pid);
    LOG("restart: restarting SceShellUI (pid %d) after injection\n", (int)pid);
    if (restart_shellui(pid) == 0) {
      pid_t np = -1;
      /* wait for the old shell to disappear and a new one to appear */
      for (int i = 0; i < 90; i++) {
        sleep(1);
        np = find_pid("SceShellUI");
        if (np > 0 && np != pid) break;
        np = -1;
      }
      if (np > 0) {
        sleep(8); /* let the new shell finish loading before hooking it */
        if (!wait_toolbox_ready(np, 120))
          LOG("restart: etaHEN did not re-hook the new shell (pid %d) within 120 s\n", (int)np);
        reload = 0;
        rc = wh_install_once_g(np, want_dump, &reload, &widgets_gen);
        pid = np;
        LOG("restart: re-injected into new SceShellUI pid %d rc=%d\n", (int)np, rc);
        hook = rc == 0 ? "WHBridge reloaded after shell restart" : "WHBridge failed after shell restart";
        plain = 1;
      } else {
        LOG("restart: new SceShellUI not found after 90s\n");
      }
    } else {
      LOG("restart: could not terminate SceShellUI\n");
    }
  }
  unsigned gen = 0; const char *why = "";
  int as = wh_asset_open(&gen, &why);
  if (as >= 0) {
    if (plain) notify("%s  v%s%s\nAsset server: running", hook, WH_VERSION, wsum);
    else notify("%s (rc=%d)  v%s\nAsset server: running", hook, rc, WH_VERSION);
    wh_asset_loop(as, gen, pid, widgets_gen); /* does not return */
  }
  if (plain) notify("%s  v%s%s\nAsset server: not running (%s)", hook, WH_VERSION, wsum, why);
  else notify("%s (rc=%d)  v%s\nAsset server: not running (%s)", hook, rc, WH_VERSION, why);
  return NULL;
}

/* "Apply now": restart SceShellUI right away (the Welcome Hub needs a fresh
 * shell to load), wait for etaHEN to hook the new one, then inject again. */
static volatile int g_restart_busy = 0;

static void *restart_now_thread(void *arg) {
  (void)arg;
  pid_t pid = find_pid("SceShellUI");
  if (pid < 0) { notify("Dashboard: ShellUI not found"); g_restart_busy = 0; return NULL; }
  if (!wait_toolbox_ready(pid, 60)) LOG("apply: etaHEN toolbox not ready for pid %d, continuing\n", (int)pid);
  notify("Dashboard\nRedemarrage du shell pour le Welcome Hub...");
  sleep(2);
  LOG("apply: restarting SceShellUI (pid %d)\n", (int)pid);
  if (restart_shellui(pid) != 0) { LOG("apply: could not terminate SceShellUI\n"); g_restart_busy = 0; return NULL; }
  pid_t np = -1;
  for (int i = 0; i < 90; i++) {
    sleep(1);
    np = find_pid("SceShellUI");
    if (np > 0 && np != pid) break;
    np = -1;
  }
  if (np > 0) {
    sleep(8);
    if (!wait_toolbox_ready(np, 120)) LOG("apply: etaHEN did not re-hook the new shell (pid %d)\n", (int)np);
    int reload = 0; uint64_t gen = 0;
    int rc = wh_install_once_g(np, access(WH_DUMP_FLAG, F_OK) == 0, &reload, &gen);
    LOG("apply: re-injected into new SceShellUI pid %d rc=%d\n", (int)np, rc);
    notify(rc == 0 ? "Dashboard\nWelcome Hub charge" : "Dashboard\nWelcome Hub: echec (rc=%d)", rc);
  } else {
    LOG("apply: new SceShellUI not found after 90s\n");
  }
  g_restart_busy = 0;
  return NULL;
}

int wh_runner_is_started(void) { return g_started; }

int wh_runner_restart_now(int restart_delay_s) {
  (void)restart_delay_s;
  if (g_restart_busy) return 0;
  g_restart_busy = 1;
  pthread_t t;
  if (pthread_create(&t, NULL, restart_now_thread, NULL) != 0) { g_restart_busy = 0; return -1; }
  pthread_detach(t);
  return 0;
}

int wh_runner_start(int restart_shell, int restart_delay_s) {
  if (g_started) return 0;
  g_restart_shell = restart_shell;
  g_restart_delay = restart_delay_s >= 5 ? restart_delay_s : 10;
  if (pthread_create(&g_thread, NULL, bridge_thread, NULL) != 0) return -1;
  pthread_detach(g_thread);
  g_started = 1;
  return 0;
}
