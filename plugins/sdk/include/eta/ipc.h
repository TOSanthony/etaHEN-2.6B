#pragma once

#include <stdint.h>

#define ETA_PLUGIN_IPC_MAGIC 0x4F504943u /* 'OPIC' */
#define ETA_PLUGIN_IPC_VERSION 1u
#define ETA_PLUGIN_IPC_MAX_PAYLOAD 4096u
#define ETA_PLUGIN_IPC_SOCKET_PATH "/system_tmp/etahen/ipc/plugin_service"

enum eta_plugin_ipc_command {
    ETA_PLUGIN_IPC_PING = 1,
    ETA_PLUGIN_IPC_RESPONSE = 2,
    ETA_PLUGIN_IPC_LOG = 3,
    ETA_PLUGIN_IPC_NOTIFY = 4,
    ETA_PLUGIN_IPC_CONFIG_GET = 5,
    ETA_PLUGIN_IPC_CONFIG_SET = 6,
    ETA_PLUGIN_IPC_GET_STATUS = 7,
    ETA_PLUGIN_IPC_STOP = 8,
    ETA_PLUGIN_IPC_EVENT = 9,
    ETA_PLUGIN_IPC_UI_REGISTER_BEGIN = 10,
    ETA_PLUGIN_IPC_UI_REGISTER_CHUNK = 11,
    ETA_PLUGIN_IPC_UI_REGISTER_COMMIT = 12,
    ETA_PLUGIN_IPC_UI_REGISTER_ABORT = 13,
    ETA_PLUGIN_IPC_UI_UNREGISTER = 14,
    ETA_PLUGIN_IPC_UI_SET_VALUE = 15,
    ETA_PLUGIN_IPC_HELLO = 16
};

/* HELLO payload: plugin ABI, capabilities, ID length, reserved, then ID bytes. */
#define ETA_PLUGIN_IPC_HELLO_HEADER_SIZE 12u
#define ETA_PLUGIN_IPC_UI_EVENT_HEADER_SIZE 36u

typedef struct eta_plugin_ipc_response {
    int32_t status;
    uint32_t data_size;
    uint8_t data[];
} eta_plugin_ipc_response;

/* Wire format is intentionally fixed-width. Do not put pointers or C++ types here. */
typedef struct eta_plugin_ipc_frame {
    uint32_t magic;
    uint16_t version;
    uint16_t command;
    uint32_t request_id;
    uint32_t payload_size;
    uint8_t payload[ETA_PLUGIN_IPC_MAX_PAYLOAD];
} eta_plugin_ipc_frame;

#define ETA_PLUGIN_IPC_RESPONSE_HEADER_SIZE 8u
