/* Copies a TOSanthony/orbit-store PPSA99177.ffpkg from a user-chosen folder
 * into /data/homebrew/, where ShadowMountPlus (a separate etaHEN plugin)
 * picks it up and registers it as a real title icon within ~10s. See
 * orbit-store's launcher/README.md ("With the TV app installed") and
 * app/README.md ("Install on the PS5", step 3). */
#include "orbit_ffpkg.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "plugin_config.h"

#define MAX_PATH_LEN 400

static int has_ffpkg_suffix(const char *name) {
    size_t len = strlen(name);
    if (len < 7) return 0; /* "x.ffpkg" */
    const char *suffix = name + (len - 6);
    char lower[7];
    for (int i = 0; i < 6; i++) lower[i] = (char)tolower((unsigned char)suffix[i]);
    lower[6] = '\0';
    return strcmp(lower, ".ffpkg") == 0;
}

static int path_is_valid(const char *path) {
    if (!path || path[0] != '/') return 0;
    size_t len = strlen(path);
    if (len < 2 || len >= MAX_PATH_LEN) return 0;
    if (strstr(path, "..") != NULL) return 0;
    return 1;
}

static orbit_ffpkg_result copy_file(const char *source, const char *dest) {
    FILE *in = fopen(source, "rb");
    if (!in) return ORBIT_FFPKG_IO_ERROR;

    char tmp_dest[MAX_PATH_LEN + 8];
    snprintf(tmp_dest, sizeof(tmp_dest), "%s.part", dest);

    FILE *out = fopen(tmp_dest, "wb");
    if (!out) {
        fclose(in);
        return ORBIT_FFPKG_IO_ERROR;
    }

    char buffer[65536];
    size_t n;
    int io_error = 0;
    while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (fwrite(buffer, 1, n, out) != n) {
            io_error = 1;
            break;
        }
    }
    if (ferror(in)) io_error = 1;
    if (!io_error && fflush(out) != 0) io_error = 1;
    if (!io_error && fsync(fileno(out)) != 0) io_error = 1;
    fclose(in);
    fclose(out);

    if (io_error) {
        unlink(tmp_dest);
        return ORBIT_FFPKG_IO_ERROR;
    }
    if (rename(tmp_dest, dest) != 0) {
        unlink(tmp_dest);
        return ORBIT_FFPKG_IO_ERROR;
    }
    return ORBIT_FFPKG_OK;
}

orbit_ffpkg_result orbit_ffpkg_install_from(const char *search_path,
                                            char *out_name,
                                            unsigned out_name_size) {
    if (!path_is_valid(search_path)) return ORBIT_FFPKG_BAD_PATH;

    DIR *dir = opendir(search_path);
    if (!dir) return ORBIT_FFPKG_IO_ERROR;

    char found_name[256] = {0};
    char found_path[MAX_PATH_LEN] = {0};
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!has_ffpkg_suffix(entry->d_name)) continue;

        char candidate[MAX_PATH_LEN];
        int n = snprintf(candidate, sizeof(candidate), "%s/%s", search_path,
                         entry->d_name);
        if (n <= 0 || (size_t)n >= sizeof(candidate)) continue;

        struct stat st;
        if (stat(candidate, &st) != 0 || !S_ISREG(st.st_mode)) continue;

        snprintf(found_name, sizeof(found_name), "%s", entry->d_name);
        snprintf(found_path, sizeof(found_path), "%s", candidate);
        break;
    }
    closedir(dir);

    if (found_name[0] == '\0') return ORBIT_FFPKG_NOT_FOUND;

    mkdir(ORBIT_HOMEBREW_DIR, 0777); /* ignore EEXIST */

    char dest_path[MAX_PATH_LEN];
    int n = snprintf(dest_path, sizeof(dest_path), "%s/%s",
                     ORBIT_HOMEBREW_DIR, found_name);
    if (n <= 0 || (size_t)n >= sizeof(dest_path)) return ORBIT_FFPKG_IO_ERROR;

    orbit_ffpkg_result result = copy_file(found_path, dest_path);
    if (result == ORBIT_FFPKG_OK && out_name && out_name_size > 0) {
        snprintf(out_name, out_name_size, "%s", found_name);
    }
    return result;
}
