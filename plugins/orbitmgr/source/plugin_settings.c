#include "plugin_settings.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "plugin_config.h"

void plugin_settings_defaults(plugin_settings *settings) {
    if (!settings) return;
    settings->autostart = 0;
    snprintf(settings->elf_path, sizeof(settings->elf_path), "%s",
             ORBIT_DEFAULT_ELF_PATH);
    snprintf(settings->search_path, sizeof(settings->search_path), "%s",
             ORBIT_DEFAULT_SEARCH_PATH);
}

static void trim_line_end(char *value) {
    size_t end = strlen(value);
    while (end > 0 && (value[end - 1] == '\n' || value[end - 1] == '\r' ||
                       value[end - 1] == ' ' || value[end - 1] == '\t')) {
        value[--end] = '\0';
    }
}

int plugin_settings_load(const char *path, plugin_settings *settings) {
    if (!path || !settings) return -1;
    plugin_settings_defaults(settings);

    FILE *file = fopen(path, "r");
    if (!file) return errno == ENOENT ? 0 : -1;

    char line[384];
    while (fgets(line, sizeof(line), file)) {
        trim_line_end(line);
        if (strncmp(line, "autostart=", 10) == 0) {
            const char *value = line + 10;
            if (strcmp(value, "true") == 0) settings->autostart = 1;
            if (strcmp(value, "false") == 0) settings->autostart = 0;
            continue;
        }
        if (strncmp(line, "elf_path=", 9) == 0) {
            snprintf(settings->elf_path, sizeof(settings->elf_path), "%s",
                     line + 9);
            continue;
        }
        if (strncmp(line, "search_path=", 12) == 0) {
            snprintf(settings->search_path, sizeof(settings->search_path),
                     "%s", line + 12);
            continue;
        }
    }
    return fclose(file) == 0 ? 0 : -1;
}

int plugin_settings_save(const char *path, const plugin_settings *settings) {
    if (!path || !settings) return -1;

    char temporary_path[512];
    const int path_length = snprintf(
        temporary_path, sizeof(temporary_path), "%s.tmp", path);
    if (path_length < 0 || (size_t)path_length >= sizeof(temporary_path)) {
        return -1;
    }

    FILE *file = fopen(temporary_path, "w");
    if (!file) return -1;

    int result = fprintf(file, "autostart=%s\nelf_path=%s\nsearch_path=%s\n",
                         settings->autostart ? "true" : "false",
                         settings->elf_path, settings->search_path) < 0
                     ? -1
                     : 0;
    if (result == 0 && fflush(file) != 0) result = -1;
    if (result == 0 && fsync(fileno(file)) != 0) result = -1;
    if (fclose(file) != 0) result = -1;
    if (result == 0 && rename(temporary_path, path) != 0) result = -1;
    if (result != 0) unlink(temporary_path);
    return result;
}
