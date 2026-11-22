/* Copyright (C) 2025 etaHEN / LightningMods
 *
 * Process-local localization for user-facing notification text.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum eta_notify_language {
  ETA_NOTIFY_LANG_ZH_HANS = 0,
  ETA_NOTIFY_LANG_EN = 1,
  ETA_NOTIFY_LANG_AR = 2,
  ETA_NOTIFY_LANG_ZH_HANT = 3,
  ETA_NOTIFY_LANG_JA = 4,
  ETA_NOTIFY_LANG_FR = 5,
  ETA_NOTIFY_LANG_DE = 6,
  ETA_NOTIFY_LANG_KO = 7,
  ETA_NOTIFY_LANG_ES = 8,
  ETA_NOTIFY_LANG_PT_BR = 9,
  ETA_NOTIFY_LANG_IT = 10,
  ETA_NOTIFY_LANG_RU = 11,
  ETA_NOTIFY_LANG_PL = 12,
  ETA_NOTIFY_LANG_TH = 13,
} eta_notify_language_t;

/** Select the resolved language used by plain and rich notifications. */
void eta_notify_set_language(eta_notify_language_t language);

/** Return the currently selected notification language. */
eta_notify_language_t eta_notify_get_language(void);

/**
 * Resolve the shared toolbox language setting.
 * ui_language: 0=system, 1=zh-Hans, 2=en, 3=ar, 4=zh-Hant, 5=ja, 6=fr,
 * 7=de, 8=ko, 9=es, 10=pt-BR, 11=it, 12=ru, 13=pl, 14=th.
 * system_language is the value returned for SCE_SYSTEM_SERVICE_PARAM_ID_LANG
 * (0=Japanese, 2/22=French, 3/20=Spanish, 4=German, 5=Italian,
 * 7/17=Portuguese, 8=Russian, 9=Korean, 10=zh-Hant, 11=zh-Hans,
 * 16=Polish, 21=Arabic, 27=Thai; anything else follows English).
 */
eta_notify_language_t eta_notify_resolve_language(int ui_language,
                                                       int system_language);

/** Select a language from the shared setting and PS5 system language. */
void eta_notify_apply_ui_language(int ui_language, int system_language);

/** Translate a stable notification key; unknown keys are returned unchanged. */
const char *eta_notify_tr(const char *key);

#ifdef __cplusplus
}
#endif
