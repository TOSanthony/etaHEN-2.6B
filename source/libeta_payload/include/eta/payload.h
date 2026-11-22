/* Copyright (C) 2025 etaHEN / LightningMods
 *
 * Payload ELF load helpers (PID files + elfldr socket).
 * User payloads are bare .elf files. Plugin ELFs are managed separately under
 * /data/etaHEN/plugins by libeta_plugin_manager.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <eta/payload_identity.h>

#ifdef __cplusplus
extern "C" {
#endif

bool eta_payload_is_elf(const void *buf, size_t size);

/**
 * Derive launch/PID key from basename: "foo.elf" → "foo".
 * Rejects empty stem (".elf") and path separators.
 */
bool eta_payload_elf_key_from_name(const char *name, char *out, size_t out_sz);

void eta_payload_pid_path(char *out, size_t out_sz, const char *title_id);
pid_t eta_payload_read_pid_file(const char *pid_path);
/** Persist only a real payload PID (>1); otherwise remove the marker. */
void eta_payload_write_pid_file(const char *pid_path, pid_t pid);

/**
 * True when the PID file for @title_id names a still-live user Payload.
 * Invalid and dead PID records are cleared without terminating any process.
 */
bool eta_payload_running(const char *title_id);

/** Check path-hash and default basename PID identities without removing records. */
bool eta_payload_path_running(const char *path);

/** Remove a stopped ELF and its sidecars; caller must restrict the installation root. */
bool eta_payload_remove(const char *path);

/**
 * Launch an existing absolute ELF path exclusively through etaHEN's private
 * 9020 elfldr. User payloads never fall back to the external 9021
 * bootstrap/recovery loader and never infer a PID from process snapshots.
 *
 * Returns the loader-reported PID (>1) on success or -1 on every failure,
 * including an unavailable loader, protocol timeout, or missing/invalid PID.
 */
pid_t eta_payload_launch_elfldr(const char *title_id, const char *path);

/** malloc'd file contents; caller free(). NULL on error. */
uint8_t *eta_payload_read_file(const char *path, size_t *out_size);

/**
 * Load a payload .elf from @path.
 * @filename Optional basename. If NULL, derived from @path.
 * A live recorded instance is preserved and also returns true. Otherwise true
 * requires the private loader to report a real PID (>1) and persist it.
 */
bool eta_payload_load(const char *path, const char *filename);

/**
 * Load a Payload using an explicit runtime key. When @key is NULL or invalid,
 * this preserves eta_payload_load's legacy basename-derived identity.
 */
bool eta_payload_load_with_key(const char *path, const char *filename,
                                 const char *key);

#ifdef __cplusplus
}
#endif
