#pragma once

typedef struct plugin_settings {
    int channel; /* 0 = "nightly" release tag, 1 = latest stable release */
} plugin_settings;

void plugin_settings_defaults(plugin_settings *s);
int plugin_settings_load(const char *path, plugin_settings *s);
int plugin_settings_save(const char *path, const plugin_settings *s);
