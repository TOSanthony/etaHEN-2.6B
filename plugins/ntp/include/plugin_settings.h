#pragma once

#include <stdint.h>

#define PLUGIN_SETTINGS_SERVER_MAX 128

typedef struct plugin_settings {
    int enabled;
    char server[PLUGIN_SETTINGS_SERVER_MAX];
    uint32_t interval_minutes;
    int notify;
} plugin_settings;

void plugin_settings_defaults(plugin_settings *settings);
int plugin_settings_load(const char *path, plugin_settings *settings);
int plugin_settings_save(const char *path, const plugin_settings *settings);
