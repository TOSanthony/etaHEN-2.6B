/* Runs the Welcome Hub bridge (hook into SceShellUI, widget asset server and
 * the built-in PSN "signed in" patch) inside the plugin process. Logic taken
 * from TOSanthony/dashboard main.c, minus tile visibility (handled by the
 * plugin settings) and the NTP clock sync (separate NTP plugin). */
#include "wh_runner.h"

#include <pthread.h>

#include "cave.h"
#include "common.h"
#include "install.h"
#include "prelude.h"
#include "server.h"
#include "util.h"

static pthread_t g_thread;
static volatile int g_started = 0;
static int g_restart_shell = 0;

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

static void *bridge_thread(void *arg) {
  (void)arg;
  wh_flog("installer", "---- WHBridge %s start (etaHEN plugin) ----", WH_VERSION);
  pid_t pid = find_pid("SceShellUI");
  /* At boot the plugin can start before the shell is up: wait up to ~60 s. */
  for (int i = 0; pid < 0 && i < 60; i++) { sleep(1); pid = find_pid("SceShellUI"); }
  if (pid < 0) { notify("WHBridge failed: ShellUI not found"); return NULL; }
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

int wh_runner_start(int restart_shell) {
  if (g_started) return 0;
  g_restart_shell = restart_shell;
  if (pthread_create(&g_thread, NULL, bridge_thread, NULL) != 0) return -1;
  pthread_detach(g_thread);
  g_started = 1;
  return 0;
}
