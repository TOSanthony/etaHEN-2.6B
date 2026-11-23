#include "plugin_settings.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void plugin_settings_defaults(plugin_settings *settings) {
    if (!settings) return;
    settings->enabled = 1;
    settings->all_games = 0;
    settings->force_8gib = 0;
    settings->gpu_on = 0;
    settings->gpu_mhz = 2200;
    settings->cpu_on = 0;
    settings->cpu_mhz = 3200;
}

static void apply_bool(const char *value, int *target) {
    if (!strncmp(value, "true", 4) || !strncmp(value, "1", 1)) *target = 1;
    else if (!strncmp(value, "false", 5) || !strncmp(value, "0", 1)) *target = 0;
}

int plugin_settings_load(const char *path, plugin_settings *settings) {
    if (!path || !settings) return -1;
    plugin_settings_defaults(settings);
    FILE *file = fopen(path, "r");
    if (!file) return errno == ENOENT ? 0 : -1;
    char line[128];
    while (fgets(line, sizeof(line), file)) {
        if (!strncmp(line, "enabled=", 8)) apply_bool(line + 8, &settings->enabled);
        else if (!strncmp(line, "all_games=", 10)) apply_bool(line + 10, &settings->all_games);
        else if (!strncmp(line, "force_8gib=", 11)) apply_bool(line + 11, &settings->force_8gib);
        else if (!strncmp(line, "gpu_on=", 7)) apply_bool(line + 7, &settings->gpu_on);
        else if (!strncmp(line, "cpu_on=", 7)) apply_bool(line + 7, &settings->cpu_on);
        else if (!strncmp(line, "gpu_mhz=", 8)) settings->gpu_mhz = (unsigned)strtoul(line + 8, NULL, 10);
        else if (!strncmp(line, "cpu_mhz=", 8)) settings->cpu_mhz = (unsigned)strtoul(line + 8, NULL, 10);
    }
    return fclose(file) == 0 ? 0 : -1;
}

int plugin_settings_save(const char *path, const plugin_settings *settings) {
    if (!path || !settings) return -1;
    char tmp[512];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) return -1;
    FILE *file = fopen(tmp, "w");
    if (!file) return -1;
    int rc = fprintf(file, "enabled=%s\nall_games=%s\nforce_8gib=%s\ngpu_on=%s\ngpu_mhz=%u\ncpu_on=%s\ncpu_mhz=%u\n",
                     settings->enabled ? "true" : "false", settings->all_games ? "true" : "false",
                     settings->force_8gib ? "true" : "false", settings->gpu_on ? "true" : "false", settings->gpu_mhz,
                     settings->cpu_on ? "true" : "false", settings->cpu_mhz) < 0 ? -1 : 0;
    if (rc == 0 && fflush(file) != 0) rc = -1;
    if (fclose(file) != 0) rc = -1;
    if (rc == 0 && rename(tmp, path) != 0) rc = -1;
    if (rc != 0) unlink(tmp);
    return rc;
}
