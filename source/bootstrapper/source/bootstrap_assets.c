/* Copyright (C) 2026 etaHEN / LightningMods */

#include "bootstrap_assets.h"

#include <eta/fs.h>
#include <eta/notify.h>

#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

extern uint8_t dpiv2_start[];
extern const unsigned int dpiv2_size;
extern uint8_t sicon_start[];
extern const unsigned int sicon_size;

#define ETA_ICON(name)                                                       \
  extern uint8_t name##_start[];                                               \
  extern const unsigned int name##_size;
#include "icon_manifest.inc"
#undef ETA_ICON

typedef struct EmbeddedIcon {
  const char *name;
  const uint8_t *data;
  const unsigned int *size;
} EmbeddedIcon;

static const EmbeddedIcon kEmbeddedIcons[] = {
#define ETA_ICON(name) {#name, name##_start, &name##_size},
#include "icon_manifest.inc"
#undef ETA_ICON
};

bool bootstrap_assets_write(void) {
  mkdir("/data/etaHEN", 0777);
  mkdir("/data/etaHEN/assets", 0777);
  mkdir("/user/data", 0777);
  mkdir("/user/data/etaHEN", 0777);

  const bool startup_icon_ready = write_file_atomic(
      ETA_NOTIFY_ICON_PATH, sicon_start, sicon_size);

  for (size_t i = 0; i < sizeof(kEmbeddedIcons) / sizeof(kEmbeddedIcons[0]);
       ++i) {
    char path[256];
    snprintf(path, sizeof(path), "/data/etaHEN/assets/%s.png",
             kEmbeddedIcons[i].name);
    (void)write_file_atomic(path, kEmbeddedIcons[i].data,
                            *kEmbeddedIcons[i].size);
  }

  /* DPI v2 (Direct Package Installer) ships inside etaHEN and is installed as a
   * regular plugin so the plugin manager discovers and starts it. */
  mkdir("/data/etaHEN/plugins", 0777);
  (void)write_file_atomic("/data/etaHEN/plugins/DPIV00001.elf", dpiv2_start,
                          dpiv2_size);

  mkdir("/system_ex/vsh_asset", 0777);
  (void)write_file_atomic("/system_ex/vsh_asset/etahenui.png", sicon_start,
                          sicon_size);
  return startup_icon_ready;
}
