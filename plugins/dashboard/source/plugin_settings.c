#include "plugin_settings.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void plugin_settings_defaults(plugin_settings *s) {
    if (!s) return;
    s->store = 0;
    s->plus = 0;
    s->hub = 1;
    s->keep = 1;
    s->notify = 0;
    s->bridge = 1;
    s->restart_shell = 0;
    s->restart_delay = 30;
}

static int parse_bool(const char *v, int *out) {
    if (strcmp(v, "true") == 0 || strcmp(v, "1") == 0) { *out = 1; return 1; }
    if (strcmp(v, "false") == 0 || strcmp(v, "0") == 0) { *out = 0; return 1; }
    return 0;
}

int plugin_settings_load(const char *path, plugin_settings *s) {
    if (!path || !s) return -1;
    plugin_settings_defaults(s);
    FILE *f = fopen(path, "r");
    if (!f) return errno == ENOENT ? 0 : -1;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = 0;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char *k = line, *v = eq + 1;
        if (!strcmp(k, "store")) parse_bool(v, &s->store);
        else if (!strcmp(k, "plus")) parse_bool(v, &s->plus);
        else if (!strcmp(k, "hub")) parse_bool(v, &s->hub);
        else if (!strcmp(k, "keep")) parse_bool(v, &s->keep);
        else if (!strcmp(k, "notify")) parse_bool(v, &s->notify);
        else if (!strcmp(k, "restart_shell")) parse_bool(v, &s->restart_shell);
        else if (!strcmp(k, "restart_delay")) {
            int d = atoi(v);
            if (d >= 5 && d <= 300) s->restart_delay = d;
        }
        else if (!strcmp(k, "bridge")) parse_bool(v, &s->bridge);
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
    int r = fprintf(f, "store=%d\nplus=%d\nhub=%d\nkeep=%d\nnotify=%d\nbridge=%d\nrestart_shell=%d\nrestart_delay=%d\n",
                    s->store, s->plus, s->hub, s->keep, s->notify, s->bridge, s->restart_shell, s->restart_delay) < 0 ? -1 : 0;
    if (r == 0 && fflush(f) != 0) r = -1;
    if (r == 0 && fsync(fileno(f)) != 0) r = -1;
    if (fclose(f) != 0) r = -1;
    if (r == 0 && rename(tmp, path) != 0) r = -1;
    if (r != 0) unlink(tmp);
    return r;
}
