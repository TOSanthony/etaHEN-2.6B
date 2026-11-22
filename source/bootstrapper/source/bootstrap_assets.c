/* Copyright (C) 2026 etaHEN / LightningMods */

#include "bootstrap_assets.h"

#include <eta/fs.h>
#include <eta/notify.h>

#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

extern uint8_t updater_start[];
extern const unsigned int updater_size;
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

  /* Only the Plugin Updater ships inside etaHEN; every other plugin (DPI v2,
   * FTP, NTP, Dashboard, Media Gallery, ...) is downloaded with it from the
   * GitHub release. */
  mkdir("/data/etaHEN/plugins", 0777);

  /* Plugin Updater (UPDT00001): downloads plugin ELFs from GitHub releases.
   * Auto-start by default on first install only. */
  {
    const bool first_install =
        access("/data/etaHEN/plugins/UPDT00001.elf", F_OK) != 0;
    (void)write_file_atomic("/data/etaHEN/plugins/UPDT00001.elf", updater_start,
                            updater_size);
    if (first_install) {
      int fd = open("/data/etaHEN/plugins/UPDT00001.elf.auto_start",
                    O_WRONLY | O_CREAT, 0666);
      if (fd >= 0) close(fd);
    }
  }

  mkdir("/system_ex/vsh_asset", 0777);
  (void)write_file_atomic("/system_ex/vsh_asset/etahenui.png", sicon_start,
                          sicon_size);
  return startup_icon_ready;
}
