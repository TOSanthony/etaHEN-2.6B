#pragma once

#include <cstdint>
#include <string_view>
#include <sys/types.h>
#include <vector>

#include <eta/sprx_catalog.hpp>

namespace eta::daemon::sprx_plugins {

inline constexpr const char *kCatalogPath =
    "/data/etaHEN/sprx/catalog.ini";

void start();
void reconcile();
std::vector<eta::sprx::SprxInventoryEntry> inventory();
eta::sprx::SprxOperationResult set_enabled(std::string_view id,
                                              bool enabled);
/** Removes only the manifest entry; it never deletes the module file. */
eta::sprx::SprxOperationResult remove(std::string_view id);
void on_big_app_started(pid_t pid, uint32_t app_id,
                        std::string_view title_id);
void on_big_app_exited(pid_t pid, uint32_t app_id,
                       std::string_view title_id);
void stop();

} // namespace eta::daemon::sprx_plugins
