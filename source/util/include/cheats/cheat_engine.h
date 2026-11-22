#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "util_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Patch byte cap (fixed arrays in eta_patch_t). Cheat/patch counts are dynamic. */
#ifndef ETA_MAX_PATCH_BYTES
#define ETA_MAX_PATCH_BYTES 1024
#endif

/* File-level credit authors (GoldHEN credits / SHN Moder / ShnExt author). */
#ifndef ETA_MAX_AUTHORS
#define ETA_MAX_AUTHORS 16
#endif
#ifndef ETA_AUTHOR_NAME_LEN
#define ETA_AUTHOR_NAME_LEN 64
#endif

#ifndef ETA_MAX_PATTERN_BYTES
#define ETA_MAX_PATTERN_BYTES 255
#endif

/* Section bounds (matches util_module_info_t / NineS util_module_info_t) */
#ifndef MODULE_INFO_MAX_SECTIONS
#define MODULE_INFO_MAX_SECTIONS UTIL_MODULE_INFO_MAX_SECTIONS
#endif

typedef enum {
  ETA_PATCH_DIRECT = 0,
  ETA_PATCH_MASK = 1,
  ETA_PATCH_MASK_JUMP32 = 2
} eta_patch_kind_t;

typedef struct {
  /* Zero preserves the fixed-address behavior of existing parser formats. */
  eta_patch_kind_t kind;
  bool code_cave_reloc;
  bool absolute;
  bool is_asm; /* ShnExt may leave unassembled ASM text */
  bool restore_original; /* Disable by restoring bytes captured before enable. */
  int section;
  uint64_t offset;
  size_t on_len;
  size_t off_len;
  uint8_t on[ETA_MAX_PATCH_BYTES];
  uint8_t off[ETA_MAX_PATCH_BYTES];
  uint16_t pattern_len;
  int64_t pattern_offset;
  uint8_t pattern[ETA_MAX_PATTERN_BYTES];
  uint8_t pattern_mask[ETA_MAX_PATTERN_BYTES];
  uint16_t target_len;
  uint8_t target[ETA_MAX_PATTERN_BYTES];
  uint16_t jump_size;
} eta_patch_t;

typedef struct {
  char name[128];
  char description[256];
  char module_name[128];
  bool enabled;
  size_t patch_count;
  size_t patch_capacity;
  eta_patch_t *patches;
} eta_cheat_entry_t;

typedef struct {
  char name[128];
  char process[128];
  size_t author_count;
  char authors[ETA_MAX_AUTHORS][ETA_AUTHOR_NAME_LEN];
  size_t cheat_count;
  size_t cheat_capacity;
  int master_code_id;
  pid_t last_applied_pid;
  eta_cheat_entry_t *cheats;
} eta_cheat_file_t;

void eta_cheat_file_clear(eta_cheat_file_t *f);
/** Append unique non-empty author; returns 0 on success/duplicate, -1 if full/invalid. */
int eta_cheat_file_add_author(eta_cheat_file_t *f, const char *author);
int eta_cheat_file_ensure_cheat(eta_cheat_file_t *f);
int eta_cheat_entry_ensure_patch(eta_cheat_entry_t *e);

/* Load path: eta::cheats::CheatParserFactory / CheatRepository (C++ only). */

#ifdef __cplusplus
}
#endif
