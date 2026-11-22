#pragma once

#include <eta/payload_identity.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ETA_PAYLOAD_PRIORITY_MAX 999
#define ETA_PAYLOAD_DELAY_MAX 300

typedef struct EtaPayloadConfig {
  int priority;
  int delay_seconds;
} EtaPayloadConfig;

/* Read <accessible ELF path>.json. Missing files use defaults and succeed;
 * invalid/unreadable files return false with defaults in out. */
bool eta_payload_config_load(const char *path, EtaPayloadConfig *out);
bool eta_payload_config_save(const char *path, const EtaPayloadConfig *config);

/* Descending priority, then ascending basename and canonical absolute path. */
int eta_payload_compare(int priority_a, const char *path_a,
                          int priority_b, const char *path_b);

#ifdef __cplusplus
}
#endif
