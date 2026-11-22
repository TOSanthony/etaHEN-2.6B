/* Copyright (C) 2026 etaHEN / LightningMods */

#include "bootstrap_notify.h"

#include <eta/notify.h>

#include <stdarg.h>

void bootstrap_notify(const char *text, ...) {
  va_list args;
  va_start(args, text);
  eta_notify_v(/*show_watermark=*/0, text, args);
  va_end(args);
}

void bootstrap_notify_starting(bool custom_icon_ready) {
  eta_notify_rich("notify.brand", "notify.boot.starting",
                    custom_icon_ready ? ETA_NOTIFY_ICON_PATH : "",
                    "download", "588193128");
}
