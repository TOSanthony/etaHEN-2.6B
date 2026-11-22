#pragma once

#include <stddef.h>

#define DL_MAX_PLUGINS 24
#define DL_ID_LEN 16

typedef enum dl_result {
    DL_OK = 0,
    DL_ERR_NETWORK,
    DL_ERR_HTTP,
    DL_ERR_INVALID,
    DL_ERR_IO
} dl_result;

typedef void (*dl_progress_fn)(void *user, unsigned percent, size_t received, size_t total);

/* Lists the plugin ELFs (<ID>.elf, 4 letters + 5 digits) attached to the GitHub
 * release of `channel` (0 = "nightly" tag, 1 = latest stable). The updater
 * itself is skipped. Returns the number of plugins (0 on error, detail set). */
unsigned downloader_list_plugins(int channel, char ids[][DL_ID_LEN], unsigned max,
                                 char *detail, size_t detail_size);

/* Downloads one plugin ELF and installs it atomically into PLUGINS_DIR. */
dl_result downloader_install_plugin(const char *plugin_id, int channel,
                                    dl_progress_fn progress, void *progress_user,
                                    char *detail, size_t detail_size);

/* Removes the "keep" marker so the built-in version is restored at the next
 * etaHEN start. */
void downloader_forget_plugin(const char *plugin_id);
