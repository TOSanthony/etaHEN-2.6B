#pragma once

typedef struct plugin_settings {
    int enabled;    /* PS4 Pro mode active */
    int all_games;  /* every PS4 game starts in Pro mode (otherwise only games with a Pro patch enabled) */
} plugin_settings;

void plugin_settings_defaults(plugin_settings *settings);
int plugin_settings_load(const char *path, plugin_settings *settings);
int plugin_settings_save(const char *path, const plugin_settings *settings);
