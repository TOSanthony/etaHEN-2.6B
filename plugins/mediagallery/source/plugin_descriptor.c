#include <eta/plugin.h>

#include "plugin_config.h"

/* CAP_KERNEL is required to raise credentials before reading /user/av_contents.
 * CAP_PROCESS matches the FTP plugin's profile. CAP_UI is the settings page only;
 * the media library itself is served over HTTP, not through ShellUI.
 *
 * NOTE: ETA_PLUGIN_FLAG_AUTO_START does not exist in the pinned SDK revision
 * (0cf43465) — plugin.h only defines NONE / LONG_RUNNING / STOP_SUPPORTED.
 * Referencing it breaks the build, so it is intentionally absent. */
ETA_PLUGIN_DEFINE(
    PLUGIN_ID, PLUGIN_VERSION, PLUGIN_NAME,
    ETA_PLUGIN_CAP_IPC | ETA_PLUGIN_CAP_PROCESS | ETA_PLUGIN_CAP_KERNEL |
        ETA_PLUGIN_CAP_UI,
    ETA_PLUGIN_FLAG_LONG_RUNNING | ETA_PLUGIN_FLAG_STOP_SUPPORTED);
