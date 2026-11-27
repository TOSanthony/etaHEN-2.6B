#pragma once

typedef struct plugin_settings {
    int autostart;                /* launch orbit_store.elf when this plugin loads */
    char elf_path[256];            /* path to the Orbit Store service ELF */
    char search_path[256];         /* folder scanned for *.ffpkg to install */
} plugin_settings;

void plugin_settings_defaults(plugin_settings *settings);
int plugin_settings_load(const char *path, plugin_settings *settings);
int plugin_settings_save(const char *path, const plugin_settings *settings);
