#pragma once

#include <stdint.h>

typedef struct plugin_settings {
    int      enabled;    /* start the HTTP listener */
    uint16_t port;       /* listener TCP port */
    int      local_only; /* bind 127.0.0.1 instead of all interfaces */
} plugin_settings;

void plugin_settings_defaults(plugin_settings *settings);
int plugin_settings_load(const char *path, plugin_settings *settings);
int plugin_settings_save(const char *path, const plugin_settings *settings);
