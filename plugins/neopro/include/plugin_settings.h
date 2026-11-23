#pragma once

typedef struct plugin_settings {
    int      enabled;     /* PS4 Pro mode active */
    int      all_games;   /* every PS4 game starts in Pro mode (otherwise only games with a Pro patch enabled) */
    int      force_8gib;  /* keep the 8 GiB memory mode on */
    int      gpu_on;      /* GPU boost while a PS4 game runs */
    unsigned gpu_mhz;
    int      cpu_on;      /* CPU boost while a PS4 game runs */
    unsigned cpu_mhz;
} plugin_settings;

void plugin_settings_defaults(plugin_settings *settings);
int plugin_settings_load(const char *path, plugin_settings *settings);
int plugin_settings_save(const char *path, const plugin_settings *settings);
