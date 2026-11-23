#pragma once

#include <stddef.h>

/* Engine of the PS4 Pro mode plugin: ps-patch-system ShellCore hook + patch server + NeoEnabler launch shim,
   8 GiB memory mode and GPU/CPU boost. */

typedef struct neo_game_info {
    char tid[10];
    char name[96];
    int pro_on; /* a Pro patch of the game is enabled */
    int has8;   /* an 8 GiB patch exists and 8 GiB is available */
    int on8;    /* an 8 GiB patch is enabled */
} neo_game_info;

typedef struct neo_config {
    int enabled;       /* PS4 Pro mode active */
    int all_games;     /* Pro mode for every PS4 game */
    int force_8gib;    /* keep 8 GiB on */
    int gpu_on;
    unsigned gpu_mhz;
    int cpu_on;
    unsigned cpu_mhz;
} neo_config;

typedef struct neo_limits {
    unsigned gpu_min, gpu_max, cpu_min, cpu_max;
    int memory_supported;
} neo_limits;

#define NEO_MAX_GAMES 48

int neo_core_start(const neo_config *cfg);
void neo_core_set(const neo_config *cfg);
void neo_core_stop(void);
/* Text for the page: which < 0 main status, 0 memory, 1 GPU, 2 CPU. summary changes whenever one of them does. */
void neo_core_text(int which, char *out, size_t cap);
void neo_core_summary(char *out, size_t cap);
void neo_core_limits(neo_limits *l);
/* Writes the bundled game patches to /data/user/patches/xml_orbis; text for the page in msg. */
int neo_core_sync(char *msg, size_t cap);
int neo_games_list(neo_game_info *out, int cap);
/* mode 0 off, 1 PS4 Pro, 2 PS4 Pro + 8 GiB */
int neo_game_set(const char *tid, int mode, char *msg, size_t cap);
