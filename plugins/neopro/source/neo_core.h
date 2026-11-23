#pragma once

#include <stddef.h>

/* Engine of the PS4 Pro mode plugin: ps-patch-system ShellCore hook + patch server + NeoEnabler launch shim. */

typedef struct neo_game_info {
    char tid[10];
    char name[96];
    int pro_on; /* a Pro patch of the game is enabled */
} neo_game_info;

#define NEO_MAX_GAMES 48

/* Starts the worker (installs patches, hooks ShellCore, answers launch questions). */
int neo_core_start(int enabled, int all_games);
void neo_core_set(int enabled, int all_games);
void neo_core_stop(void);
/* Short status text for the settings page. */
void neo_core_status(char *out, size_t cap);
/* Writes the bundled game patches to /data/user/patches/xml_orbis; text for the page in msg. */
int neo_core_sync(char *msg, size_t cap);
int neo_games_list(neo_game_info *out, int cap);
int neo_game_set(const char *tid, int on, char *msg, size_t cap);
