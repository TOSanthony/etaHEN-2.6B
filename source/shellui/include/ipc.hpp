/* Copyright (C) 2025 etaHEN / LightningMods
 *
 * ShellUI IPC client entry — no HookedFuncs (true compile seam).
 * Call sites that need UI types/hooks include hooked_funcs.hpp separately.
 */

#pragma once

#include <eta/ipc_client.hpp>

// shellui-only IPC-adjacent globals
extern bool cheats_shortcut_activate;
