/* Copyright (C) 2026 etaHEN / LightningMods
 *
 * Shared /system_tmp namespace for etaHEN runtime state.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#if defined(ETA_HOST_TEST)
#define ETA_SYSTEM_TMP_ROOT "/tmp/etahen"
#else
#define ETA_SYSTEM_TMP_ROOT "/system_tmp/etahen"
#endif

#define ETA_SYSTEM_TMP_IPC_ROOT ETA_SYSTEM_TMP_ROOT "/ipc"
#define ETA_SYSTEM_TMP_READY_ROOT ETA_SYSTEM_TMP_ROOT "/ready"
#define ETA_SYSTEM_TMP_PID_ROOT ETA_SYSTEM_TMP_ROOT "/pid"

#define ETA_SYSTEM_TMP_CRIT_SOCKET ETA_SYSTEM_TMP_IPC_ROOT "/crit_service"
#define ETA_SYSTEM_TMP_UTIL_SOCKET ETA_SYSTEM_TMP_IPC_ROOT "/util_service"
#define ETA_SYSTEM_TMP_PLUGIN_SOCKET ETA_SYSTEM_TMP_IPC_ROOT "/plugin_service"
#define ETA_SYSTEM_TMP_SHELLUI_PLUGIN_SOCKET                                  \
  ETA_SYSTEM_TMP_IPC_ROOT "/shellui_plugin_bridge"

#define ETA_SYSTEM_TMP_ELFLDR_STATE                                      \
  ETA_SYSTEM_TMP_PID_ROOT "/eta_elfldr_9020.PID"
#define ETA_SYSTEM_TMP_ELFLDR_BUSY                                       \
  ETA_SYSTEM_TMP_PID_ROOT "/eta_elfldr_9020.busy"
#define ETA_SYSTEM_TMP_APP_LAUNCHED ETA_SYSTEM_TMP_ROOT "/app_launched"
#define ETA_SYSTEM_TMP_PATCH_PLUGIN ETA_SYSTEM_TMP_ROOT "/patch_plugin"
#define ETA_SYSTEM_TMP_FPS_SAMPLE ETA_SYSTEM_TMP_ROOT "/fps_sample"

static inline bool eta_system_tmp_pid_path(char *out, size_t out_sz,
                                             const char *key) {
  if (!out || out_sz == 0) {
    return false;
  }
  int n = snprintf(out, out_sz, "%s/%s.PID", ETA_SYSTEM_TMP_PID_ROOT,
                   key ? key : "");
  return n > 0 && (size_t)n < out_sz;
}
