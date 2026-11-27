#include <eta/plugin.h>

#include "plugin_config.h"

/* AUTO_START: the daemon launches this plugin at boot so the "démarrage
 * automatique" setting can bring Orbit Store's own service up without the
 * user opening the TV app first. */
ETA_PLUGIN_DEFINE(
    PLUGIN_ID, PLUGIN_VERSION, PLUGIN_NAME,
    ETA_PLUGIN_CAP_IPC | ETA_PLUGIN_CAP_PROCESS | ETA_PLUGIN_CAP_KERNEL |
        ETA_PLUGIN_CAP_UI,
    ETA_PLUGIN_FLAG_AUTO_START | ETA_PLUGIN_FLAG_LONG_RUNNING |
        ETA_PLUGIN_FLAG_STOP_SUPPORTED);
