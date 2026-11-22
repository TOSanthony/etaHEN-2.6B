/* Copyright (C) 2026 etaHEN / LightningMods
 *
 * Strategy table of foreign payload process names. A live match on
 * ki_comm / ki_tdname means that family is already running; etaHEN must
 * refuse to continue its own load chain. Matching is case-sensitive. A
 * trailing ".elf" on the table name or the live process name is ignored.
 * Names longer than ki_comm (COMMLEN=19) also match their truncated form,
 * and longer than ki_tdname (TDNAMLEN=16) their thread-name truncation.
 */
#pragma once

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef pid_t (*eta_conflict_find_pid_fn)(const char *name);

typedef struct EtaConflictStrategy {
  const char *family;
  const char *const *proc_names; /* NULL-terminated; ".elf" optional */
} EtaConflictStrategy;

const EtaConflictStrategy *eta_conflict_strategies(size_t *out_count);

/**
 * Walk @strategies in order. First family with a live name wins.
 * Each table name is probed as-is, with/without a trailing ".elf", and
 * truncated to both ki_comm and ki_tdname length. NULL find_pid, missing
 * names, and pid <= 0 are not hits (fail-open).
 */
const char *eta_conflict_scan(const EtaConflictStrategy *strategies,
                                size_t count,
                                eta_conflict_find_pid_fn find_pid);

/** Scan the compiled-in strategy table with @find_pid. */
const char *eta_conflict_detect_with(eta_conflict_find_pid_fn find_pid);

/** Scan with eta_find_pid. Host tests compile this as a no-op. */
const char *eta_conflict_detect(void);

#ifdef __cplusplus
}
#endif
