#pragma once

#include "cheats/sync/types.hpp"

#include <cstddef>

namespace eta::cheats::sync {

/** Extract supported cheat files from selected archive roots. */
SyncStatus extract_cheat_zip(const char *zip_path, const char *dest_root,
                             const char *const *roots, size_t root_count,
                             SyncProgressFn progress, void *progress_user,
                             SyncCancelFn should_cancel = nullptr,
                             void *cancel_user = nullptr);

} // namespace eta::cheats::sync
