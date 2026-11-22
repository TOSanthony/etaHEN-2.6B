/* Copyright (C) 2025 etaHEN / LightningMods
 *
 * ShellUI notify(const char *, ...) — no watermark bool (historical UI API).
 */

#include "hooked_funcs.hpp"
#include "ipc.hpp"
#include <eta/notify.h>
#include <cstdarg>
#include <cstdio>

void notify(const char *text, ...) {
  va_list args{};
  va_start(args, text);
  // show_watermark ignored by format; still prefixes [etaHEN] like daemons.
  // Send goes through eta_notify_set_send trampoline (dlsym pointer safe).
  eta_notify_v(/*show_watermark=*/0, text, args);
  va_end(args);
  LOG_DEBUG("Notify send returned (eta_notify path)");
}
