#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum orbit_ffpkg_result {
    ORBIT_FFPKG_OK = 0,
    ORBIT_FFPKG_NOT_FOUND = 1,  /* no *.ffpkg in the search folder */
    ORBIT_FFPKG_IO_ERROR = 2,   /* copy or folder-creation failure */
    ORBIT_FFPKG_BAD_PATH = 3    /* search_path is not a valid absolute path */
} orbit_ffpkg_result;

/**
 * Scan @search_path (non-recursive) for the first "*.ffpkg" entry and copy it
 * into /data/homebrew/<name> (ShadowMountPlus registers it there ~10s later,
 * per TOSanthony/orbit-store's launcher/README.md and app/README.md). On
 * success, @out_name (if non-NULL) receives the installed file's name.
 */
orbit_ffpkg_result orbit_ffpkg_install_from(const char *search_path,
                                            char *out_name,
                                            unsigned out_name_size);

#ifdef __cplusplus
}
#endif
