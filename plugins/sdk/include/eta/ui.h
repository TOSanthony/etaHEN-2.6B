#pragma once

#include <stddef.h>
#include <stdint.h>

#include "eta/plugin.h"
#include "eta/services.h"
#include "eta/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ETA_UI_ABI_VERSION 1u
#define ETA_UI_SERVICE_NAME "eta.ui"

#define ETA_UI_CONTRIBUTION_ID_MAX 64u
#define ETA_UI_NODE_ID_MAX 64u
#define ETA_UI_TITLE_MAX 128u
#define ETA_UI_DESCRIPTION_MAX 256u
#define ETA_UI_VALUE_MAX 256u
#define ETA_UI_BINDING_KEY_MAX 64u
#define ETA_UI_MAX_NODES 256u
#define ETA_UI_MAX_DEPTH 8u

typedef uint64_t eta_ui_handle;

typedef enum eta_ui_node_kind {
    ETA_UI_NODE_PAGE = 1,
    ETA_UI_NODE_MENU = 2,
    ETA_UI_NODE_GROUP = 3,
    ETA_UI_NODE_LABEL = 4,
    ETA_UI_NODE_ACTION = 5,
    ETA_UI_NODE_TOGGLE = 6,
    ETA_UI_NODE_LIST = 7,
    ETA_UI_NODE_LIST_ITEM = 8,
    ETA_UI_NODE_INPUT = 9
} eta_ui_node_kind;

typedef enum eta_ui_value_type {
    ETA_UI_VALUE_NONE = 0,
    ETA_UI_VALUE_BOOL = 1,
    ETA_UI_VALUE_INT = 2,
    ETA_UI_VALUE_STRING = 3
} eta_ui_value_type;

typedef enum eta_ui_binding_kind {
    ETA_UI_BINDING_NONE = 0,
    ETA_UI_BINDING_CONFIG = 1,
    ETA_UI_BINDING_EVENT = 2
} eta_ui_binding_kind;

enum eta_ui_node_flags {
    ETA_UI_NODE_FLAG_NONE = 0,
    ETA_UI_NODE_FLAG_DISABLED = 1u << 0,
    ETA_UI_NODE_FLAG_CONFIRM = 1u << 1,
    ETA_UI_NODE_FLAG_SECRET = 1u << 2
};

typedef struct eta_ui_document_desc_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t flags;
    int32_t priority;
    char plugin_id[ETA_PLUGIN_ID_MAX];
    char contribution_id[ETA_UI_CONTRIBUTION_ID_MAX];
    char title[ETA_UI_TITLE_MAX];
    char description[ETA_UI_DESCRIPTION_MAX];
    char root_page_id[ETA_UI_NODE_ID_MAX];
} eta_ui_document_desc_v1;

typedef struct eta_ui_node_desc_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t kind;
    uint32_t flags;
    uint32_t value_type;
    uint32_t binding;
    int64_t min_value;
    int64_t max_value;
    uint32_t min_length;
    uint32_t max_length;
    char id[ETA_UI_NODE_ID_MAX];
    char parent_id[ETA_UI_NODE_ID_MAX];
    char title[ETA_UI_TITLE_MAX];
    char description[ETA_UI_DESCRIPTION_MAX];
    char target_id[ETA_UI_NODE_ID_MAX];
    char binding_key[ETA_UI_BINDING_KEY_MAX];
    char value[ETA_UI_VALUE_MAX];
} eta_ui_node_desc_v1;

typedef struct eta_ui_event_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t sequence;
    eta_ui_handle handle;
    uint32_t value_type;
    char contribution_id[ETA_UI_CONTRIBUTION_ID_MAX];
    char page_id[ETA_UI_NODE_ID_MAX];
    char node_id[ETA_UI_NODE_ID_MAX];
    char value[ETA_UI_VALUE_MAX];
} eta_ui_event_v1;

typedef struct eta_ui_document eta_ui_document;

eta_status eta_ui_document_create(
    const eta_ui_document_desc_v1 *desc, eta_ui_document **out_document);
void eta_ui_document_destroy(eta_ui_document *document);
eta_status eta_ui_document_add_node(
    eta_ui_document *document, const eta_ui_node_desc_v1 *node);
eta_status eta_ui_document_validate(const eta_ui_document *document);
size_t eta_ui_document_node_count(const eta_ui_document *document);
eta_status eta_ui_document_encoded_size(
    const eta_ui_document *document, size_t *out_size);
eta_status eta_ui_document_encode(
    const eta_ui_document *document, void *buffer, size_t buffer_size,
    size_t *out_size);

typedef eta_status (*eta_ui_register_fn)(
    void *context, const void *document, size_t document_size,
    eta_ui_handle *out_handle);
typedef eta_status (*eta_ui_unregister_fn)(
    void *context, eta_ui_handle handle);
typedef eta_status (*eta_ui_set_value_fn)(
    void *context, eta_ui_handle handle, const char *node_id,
    eta_ui_value_type value_type, const char *value);

typedef struct eta_ui_services_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    void *context;
    eta_ui_register_fn register_document;
    eta_ui_unregister_fn unregister_document;
    eta_ui_set_value_fn set_value;
} eta_ui_services_v1;

eta_status eta_ui_get_services(
    const eta_host_services_v1 *host, eta_ui_services_v1 *out_services);
eta_status eta_ui_register(
    const eta_host_services_v1 *host, const eta_ui_document *document,
    eta_ui_handle *out_handle);
eta_status eta_ui_unregister(
    const eta_host_services_v1 *host, eta_ui_handle handle);
eta_status eta_ui_set_value(
    const eta_host_services_v1 *host, eta_ui_handle handle,
    const char *node_id, eta_ui_value_type value_type, const char *value);

#ifdef __cplusplus
}
#endif
