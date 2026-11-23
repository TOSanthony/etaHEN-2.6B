#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ntp_lang {
    NTP_LANG_EN = 0,
    NTP_LANG_ZH_HANS = 1,
    NTP_LANG_FR = 2,
} ntp_lang_t;

/**
 * Initialize / refresh the current system language.
 * Returns true if language changed since last check.
 */
bool ntp_i18n_refresh(void);

/**
 * Get current active language.
 */
ntp_lang_t ntp_i18n_get_lang(void);

/**
 * Translate a key to current language string.
 */
const char *ntp_tr(const char *key);

#ifdef __cplusplus
}
#endif
