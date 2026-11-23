// GPL-3.0-only. Glue between the etaHEN plugin and the embedded ps-patch-system engine
// (third_party/pspatch, Apache-2.0): the ShellCore hook installer (patch/patch_shellcore.c) and the
// patch server that ShellCore talks to for every launched process (patch-server/*).
// The two programs of ps-patch-system (patch.elf, patch-server.elf) run here as one plugin: the hook is
// installed by pspatch_attach() and the server runs on two threads of the plugin process.
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

extern "C" {
bool is_ps4 = false;
void patchShellcore(const pid_t pid);
}

pid_t g_pid = 0;

void setup_server_thread(void);
void setup_main_thread(void);

static void* signal_thread(void*)
{
    setup_main_thread();  // SIGUSR1/SIGUSR2 consumer, returns on SIGTERM
    return nullptr;
}

extern "C" int pspatch_server_start(void)
{
    static bool started = false;
    if (started)
    {
        return 0;
    }
    started = true;
    g_pid = getpid();
    setup_server_thread();
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 512 * 1024);
    pthread_t tid = 0;
    const int rc = pthread_create(&tid, &attr, signal_thread, nullptr);
    pthread_attr_destroy(&attr);
    return rc;
}

extern "C" int pspatch_attach(const pid_t shellcore_pid)
{
    patchShellcore(shellcore_pid);
    return 0;
}
