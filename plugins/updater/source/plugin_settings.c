#include "plugin_settings.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void plugin_settings_defaults(plugin_settings *s) {
    if (s) s->channel = 0;
}

int plugin_settings_load(const char *path, plugin_settings *s) {
    if (!path || !s) return -1;
    plugin_settings_defaults(s);
    FILE *f = fopen(path, "r");
    if (!f) return errno == ENOENT ? 0 : -1;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "channel=latest", 14) == 0) s->channel = 1;
        else if (strncmp(line, "channel=nightly", 15) == 0) s->channel = 0;
    }
    return fclose(f) == 0 ? 0 : -1;
}

int plugin_settings_save(const char *path, const plugin_settings *s) {
    if (!path || !s) return -1;
    char tmp[512];
    int len = snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (len < 0 || (size_t)len >= sizeof tmp) return -1;
    FILE *f = fopen(tmp, "w");
    if (!f) return -1;
    int r = fprintf(f, "channel=%s\n", s->channel ? "latest" : "nightly") < 0 ? -1 : 0;
    if (r == 0 && fflush(f) != 0) r = -1;
    if (r == 0 && fsync(fileno(f)) != 0) r = -1;
    if (fclose(f) != 0) r = -1;
    if (r == 0 && rename(tmp, path) != 0) r = -1;
    if (r != 0) unlink(tmp);
    return r;
}
