#pragma once

#include <stddef.h>

typedef enum dl_result {
    DL_OK = 0,
    DL_ERR_NETWORK,
    DL_ERR_HTTP,
    DL_ERR_INVALID,
    DL_ERR_IO
} dl_result;

/* Downloads one plugin ELF (<id>.elf) from the GitHub release `channel`
 * (0 = nightly tag, 1 = latest stable) and installs it atomically into
 * PLUGINS_DIR. On failure *detail receives a short message. */
dl_result downloader_install_plugin(const char *plugin_id, int channel,
                                    char *detail, size_t detail_size);

/* Removes the "keep" marker so the built-in version is restored at the next
 * etaHEN start. */
void downloader_forget_plugin(const char *plugin_id);
