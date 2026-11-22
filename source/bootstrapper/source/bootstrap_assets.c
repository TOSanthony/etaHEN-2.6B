/* Copyright (C) 2026 etaHEN / LightningMods */

#include "bootstrap_assets.h"

#include <eta/fs.h>
#include <eta/notify.h>

#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

extern uint8_t dpiv2_start[];
extern const unsigned int dpiv2_size;
extern uint8_t ftpsrv_start[];
extern const unsigned int ftpsrv_size;
extern uint8_t ntp_start[];
extern const unsigned int ntp_size;
extern uint8_t dashplug_start[];
extern const unsigned int dashplug_size;
extern uint8_t mgallery_start[];
extern const unsigned int mgallery_size;
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
  /* FTP server ships inside etaHEN as a plugin as well (FTPS00001). */
  (void)write_file_atomic("/data/etaHEN/plugins/FTPS00001.elf", ftpsrv_start,
                          ftpsrv_size);
  /* NTP time-sync plugin (NTPS00001), European servers by default. */
  (void)write_file_atomic("/data/etaHEN/plugins/NTPS00001.elf", ntp_start,
                          ntp_size);

  /* Dashboard plugin (DASH00001): show/hide Store, Plus and Welcome Hub tiles. */
  {
    /* Auto-start by default on first install only (the user can still turn it
     * off from the plugin page; the marker is then left alone). */
    const bool first_install =
        access("/data/etaHEN/plugins/DASH00001.elf", F_OK) != 0;
    (void)write_file_atomic("/data/etaHEN/plugins/DASH00001.elf", dashplug_start,
                            dashplug_size);
    if (first_install) {
      int fd = open("/data/etaHEN/plugins/DASH00001.elf.auto_start",
                    O_WRONLY | O_CREAT, 0666);
      if (fd >= 0) close(fd);
    }
  }

  /* Media Gallery plugin (MGAL00001): HTTP access to screenshots/clips. */
  (void)write_file_atomic("/data/etaHEN/plugins/MGAL00001.elf", mgallery_start,
                          mgallery_size);

  mkdir("/system_ex/vsh_asset", 0777);
  (void)write_file_atomic("/system_ex/vsh_asset/etahenui.png", sicon_start,
                          sicon_size);
  return startup_icon_ready;
}
