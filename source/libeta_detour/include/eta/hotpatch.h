/* Copyright (C) 2026 etaHEN */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ETA_X64_ABSOLUTE_JUMP_SIZE 14u
#define ETA_X64_ATOMIC_PATCH_SIZE 16u

struct eta_x64_atomic_patch {
  uint8_t expected[ETA_X64_ATOMIC_PATCH_SIZE];
  uint8_t desired[ETA_X64_ATOMIC_PATCH_SIZE];
};

/*
 * Build an aligned 16-byte compare/exchange image for:
 *   jmp qword ptr [rip+0]; dq destination
 * Bytes 14..15 are preserved from the original function.
 */
bool eta_x64_build_atomic_patch(
    uintptr_t target_address,
    const uint8_t current[ETA_X64_ATOMIC_PATCH_SIZE],
    uintptr_t destination,
    struct eta_x64_atomic_patch *patch);

#ifdef __cplusplus
}
#endif
