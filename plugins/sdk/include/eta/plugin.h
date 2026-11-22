#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ETA_PLUGIN_ABI_VERSION 1u
#define ETA_PLUGIN_ID_MAX 32u
#define ETA_PLUGIN_VERSION_MAX 16u
#define ETA_PLUGIN_NAME_MAX 64u

enum eta_plugin_capability {
    ETA_PLUGIN_CAP_NONE = 0,
    ETA_PLUGIN_CAP_NOTIFY = 1u << 0,
    ETA_PLUGIN_CAP_IPC = 1u << 1,
    ETA_PLUGIN_CAP_PROCESS = 1u << 2,
    ETA_PLUGIN_CAP_INJECT = 1u << 3,
    ETA_PLUGIN_CAP_KERNEL = 1u << 4,
    ETA_PLUGIN_CAP_UI = 1u << 5
};

enum eta_plugin_flags {
    ETA_PLUGIN_FLAG_NONE = 0,
    ETA_PLUGIN_FLAG_AUTO_START = 1u << 0,
    ETA_PLUGIN_FLAG_LONG_RUNNING = 1u << 1,
    ETA_PLUGIN_FLAG_STOP_SUPPORTED = 1u << 2
};

/* This descriptor is an ELF section marker, not a host function table. */
typedef struct eta_plugin_descriptor_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t capabilities;
    uint32_t flags;
    char plugin_id[ETA_PLUGIN_ID_MAX];
    char version[ETA_PLUGIN_VERSION_MAX];
    char name[ETA_PLUGIN_NAME_MAX];
} eta_plugin_descriptor_v1;

#if defined(__ELF__)
#define ETA_PLUGIN_SECTION __attribute__((used, section(".eta_plugin"), aligned(8)))
#else
#define ETA_PLUGIN_SECTION
#endif

#define ETA_PLUGIN_DEFINE(ID, VERSION, NAME, CAPS, FLAGS) \
    const eta_plugin_descriptor_v1 eta_plugin_descriptor \
        ETA_PLUGIN_SECTION = { \
            sizeof(eta_plugin_descriptor_v1), ETA_PLUGIN_ABI_VERSION, \
            (CAPS), (FLAGS), (ID), (VERSION), (NAME) }

#ifdef __cplusplus
}
#endif
