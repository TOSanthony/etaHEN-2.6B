#pragma once

typedef struct plugin_settings {
    int store;   /* PlayStation Store tile visible (NPXS40047) */
    int plus;    /* PlayStation Plus tile visible (NPXS40037) */
    int hub;     /* Welcome Hub tile visible (NPXS40063) */
    int keep;    /* re-apply periodically so the shell cannot revert */
    int notify;
    int bridge;  /* Welcome Hub bridge: forced signed-in patch + widgets (replaces wh_bridge.elf) */
} plugin_settings;

void plugin_settings_defaults(plugin_settings *settings);
int plugin_settings_load(const char *path, plugin_settings *settings);
int plugin_settings_save(const char *path, const plugin_settings *settings);
