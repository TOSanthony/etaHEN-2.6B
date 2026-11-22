#include "eta/client.h"
#include "client_internal.h"
#include "ui_client_internal.h"

#include <ctype.h>
#include <errno.h>
#include <string.h>

static void put_u16(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *out, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) out[i] = (uint8_t)(value >> (i * 8));
}

static int plugin_id_valid(const char *id) {
    const char *end = id ? (const char *)memchr(id, '\0', ETA_PLUGIN_ID_MAX) : NULL;
    if (!end || end == id) return 0;
    for (const unsigned char *cursor = (const unsigned char *)id;
         cursor != (const unsigned char *)end; ++cursor) {
        if (!(isalnum(*cursor) || *cursor == '_' || *cursor == '-' ||
              *cursor == '.')) return 0;
    }
    return 1;
}

static eta_status client_request_unlocked(
    eta_client *client, uint16_t command, const void *payload,
    size_t payload_size, eta_plugin_ipc_frame *response) {
    eta_plugin_ipc_frame request;
    memset(&request, 0, sizeof(request));
    request.magic = ETA_PLUGIN_IPC_MAGIC;
    request.version = ETA_PLUGIN_IPC_VERSION;
    request.command = command;
    request.payload_size = (uint32_t)payload_size;
    if (payload_size) memcpy(request.payload, payload, payload_size);
    request.request_id = client->next_request_id++;
    eta_status result = eta_transport_send_frame(client->transport, &request);
    if (result == ETA_OK) result = eta_transport_recv_frame(client->transport, response);
    if (result != ETA_OK) return result;
    if (response->command != ETA_PLUGIN_IPC_RESPONSE ||
        response->request_id != request.request_id || response->payload_size < 8) {
        return ETA_E_PROTOCOL;
    }
    return ETA_OK;
}

static eta_status client_request(eta_client *client, uint16_t command,
                                   const void *payload, size_t payload_size,
                                   eta_plugin_ipc_frame *response) {
    if (!client || !client->initialized || !client->transport || !response ||
        payload_size > ETA_PLUGIN_IPC_MAX_PAYLOAD) return ETA_E_NOT_INITIALIZED;
    pthread_mutex_lock(&client->mutex);
    eta_status result = client_request_unlocked(
        client, command, payload, payload_size, response);
    pthread_mutex_unlock(&client->mutex);
    return result;
}

static eta_status response_status(const eta_plugin_ipc_frame *response,
                                    const uint8_t **data, size_t *size) {
    if (!response || !data || !size || response->payload_size < 8) return ETA_E_PROTOCOL;
    int32_t status;
    uint32_t data_size;
    memcpy(&status, response->payload, sizeof(status));
    memcpy(&data_size, response->payload + 4, sizeof(data_size));
    if ((size_t)data_size > response->payload_size - 8) return ETA_E_PROTOCOL;
    *data = response->payload + 8;
    *size = data_size;
    return (eta_status)status;
}

eta_status eta_client_request(eta_client *client, uint16_t command,
                                  const void *payload, size_t payload_size,
                                  void *out_data, size_t out_capacity,
                                  size_t *out_size) {
    eta_plugin_ipc_frame response;
    eta_status result = client_request(client, command, payload, payload_size,
                                         &response);
    if (result != ETA_OK) return result;
    const uint8_t *data = NULL;
    size_t size = 0;
    result = response_status(&response, &data, &size);
    if (result != ETA_OK) return result;
    if (size > out_capacity || (size != 0 && !out_data)) return ETA_E_INVALID_ARGUMENT;
    if (size) memcpy(out_data, data, size);
    if (out_size) *out_size = size;
    return ETA_OK;
}

static eta_status service_log(void *context, eta_log_level level, const char *message) {
    uint32_t level_value = (uint32_t)level;
    size_t length = message ? strlen(message) : 0;
    if (length + sizeof(level_value) > ETA_PLUGIN_IPC_MAX_PAYLOAD) return ETA_E_INVALID_ARGUMENT;
    uint8_t payload[ETA_PLUGIN_IPC_MAX_PAYLOAD];
    memcpy(payload, &level_value, sizeof(level_value));
    if (length) memcpy(payload + sizeof(level_value), message, length);
    eta_plugin_ipc_frame response;
    eta_status result = client_request((eta_client *)context, ETA_PLUGIN_IPC_LOG,
                                         payload, sizeof(level_value) + length, &response);
    if (result != ETA_OK) return result;
    const uint8_t *data; size_t size;
    return response_status(&response, &data, &size);
}

static eta_status service_notify(void *context, const char *message) {
    eta_plugin_ipc_frame response;
    size_t length = message ? strlen(message) : 0;
    if (!message || length >= ETA_PLUGIN_IPC_MAX_PAYLOAD) return ETA_E_INVALID_ARGUMENT;
    eta_status result = client_request((eta_client *)context, ETA_PLUGIN_IPC_NOTIFY,
                                         message, length, &response);
    if (result != ETA_OK) return result;
    const uint8_t *data; size_t size;
    return response_status(&response, &data, &size);
}

static eta_status service_config_get(void *context, const char *key,
                                       char *value, size_t value_size) {
    eta_plugin_ipc_frame response;
    if (!key || !value || value_size == 0) return ETA_E_INVALID_ARGUMENT;
    size_t key_size = strlen(key) + 1;
    if (key_size > ETA_PLUGIN_IPC_MAX_PAYLOAD) return ETA_E_INVALID_ARGUMENT;
    eta_status result = client_request((eta_client *)context, ETA_PLUGIN_IPC_CONFIG_GET,
                                         key, key_size, &response);
    if (result != ETA_OK) return result;
    const uint8_t *data; size_t size;
    result = response_status(&response, &data, &size);
    if (result != ETA_OK) return result;
    if (size >= value_size) return ETA_E_INVALID_ARGUMENT;
    memcpy(value, data, size);
    value[size] = '\0';
    return ETA_OK;
}

static eta_status service_config_set(void *context, const char *key, const char *value) {
    eta_plugin_ipc_frame response;
    if (!key || !value) return ETA_E_INVALID_ARGUMENT;
    size_t key_size = strlen(key) + 1;
    size_t value_size = strlen(value) + 1;
    if (key_size + value_size > ETA_PLUGIN_IPC_MAX_PAYLOAD) return ETA_E_INVALID_ARGUMENT;
    uint8_t payload[ETA_PLUGIN_IPC_MAX_PAYLOAD];
    memcpy(payload, key, key_size);
    memcpy(payload + key_size, value, value_size);
    eta_status result = client_request((eta_client *)context, ETA_PLUGIN_IPC_CONFIG_SET,
                                         payload, key_size + value_size, &response);
    if (result != ETA_OK) return result;
    const uint8_t *data; size_t size;
    return response_status(&response, &data, &size);
}

static eta_status service_query_interface(void *context, const char *name,
                                            uint32_t min_version,
                                            void *out_interface,
                                            size_t interface_size) {
    if (!context || !name || !out_interface) return ETA_E_INVALID_ARGUMENT;
    eta_client *client = (eta_client *)context;
    if (!client->initialized) return ETA_E_NOT_INITIALIZED;
    pthread_mutex_lock(&client->mutex);
    const int ui_available = client->session_open &&
        (client->session_capabilities & ETA_PLUGIN_CAP_UI) != 0;
    pthread_mutex_unlock(&client->mutex);
    if (!ui_available) {
        return ETA_E_NOT_SUPPORTED;
    }
    if (strcmp(name, ETA_UI_SERVICE_NAME) != 0 ||
        min_version > ETA_UI_ABI_VERSION ||
        interface_size < sizeof(eta_ui_services_v1)) {
        return ETA_E_NOT_SUPPORTED;
    }
    return eta_ui_client_make_services(client,
                                         (eta_ui_services_v1 *)out_interface);
}

eta_status eta_client_init(eta_client *client, eta_transport *transport) {
    if (!client || !transport || !transport->send || !transport->recv) return ETA_E_INVALID_ARGUMENT;
    memset(client, 0, sizeof(*client));
    if (pthread_mutex_init(&client->mutex, NULL) != 0) return ETA_E_IO;
    client->transport = transport;
    client->next_request_id = 1;
    client->initialized = 1;
    return ETA_OK;
}

eta_status eta_client_open_session(
    eta_client *client, const eta_plugin_descriptor_v1 *descriptor) {
    const uint32_t known_capabilities =
        ETA_PLUGIN_CAP_NOTIFY | ETA_PLUGIN_CAP_IPC |
        ETA_PLUGIN_CAP_PROCESS | ETA_PLUGIN_CAP_INJECT |
        ETA_PLUGIN_CAP_KERNEL | ETA_PLUGIN_CAP_UI;
    if (!client || !client->initialized || !descriptor ||
        descriptor->struct_size < sizeof(*descriptor) ||
        descriptor->abi_version != ETA_PLUGIN_ABI_VERSION ||
        !plugin_id_valid(descriptor->plugin_id) ||
        (descriptor->capabilities & ~known_capabilities) != 0) {
        return ETA_E_INVALID_ARGUMENT;
    }
    const size_t id_size = strlen(descriptor->plugin_id);
    uint8_t payload[ETA_PLUGIN_IPC_HELLO_HEADER_SIZE + ETA_PLUGIN_ID_MAX];
    put_u32(payload, descriptor->abi_version);
    put_u32(payload + 4, descriptor->capabilities);
    put_u16(payload + 8, (uint16_t)id_size);
    put_u16(payload + 10, 0);
    memcpy(payload + ETA_PLUGIN_IPC_HELLO_HEADER_SIZE,
           descriptor->plugin_id, id_size);
    eta_plugin_ipc_frame response;
    pthread_mutex_lock(&client->mutex);
    if (client->session_open) {
        pthread_mutex_unlock(&client->mutex);
        return ETA_E_ALREADY_INITIALIZED;
    }
    eta_status result = client_request_unlocked(
        client, ETA_PLUGIN_IPC_HELLO, payload,
        ETA_PLUGIN_IPC_HELLO_HEADER_SIZE + id_size, &response);
    if (result == ETA_OK) {
        const uint8_t *data = NULL;
        size_t size = 0;
        result = response_status(&response, &data, &size);
    }
    if (result == ETA_OK) {
        client->session_capabilities = descriptor->capabilities;
        client->session_open = 1;
    }
    pthread_mutex_unlock(&client->mutex);
    return result;
}

void eta_client_deinit(eta_client *client) {
    if (!client || !client->initialized) return;
    pthread_mutex_destroy(&client->mutex);
    client->transport = NULL;
    client->session_capabilities = 0;
    client->initialized = 0;
    client->session_open = 0;
}

eta_status eta_client_make_services(eta_client *client,
                                        eta_host_services_v1 *out_services) {
    if (!client || !client->initialized || !out_services) return ETA_E_INVALID_ARGUMENT;
    memset(out_services, 0, sizeof(*out_services));
    out_services->struct_size = sizeof(*out_services);
    out_services->abi_version = ETA_HOST_SERVICES_ABI_VERSION;
    out_services->context = client;
    out_services->log = service_log;
    out_services->notify = service_notify;
    out_services->config_get = service_config_get;
    out_services->config_set = service_config_set;
    out_services->query_interface = service_query_interface;
    return ETA_OK;
}

eta_status eta_client_ping(eta_client *client) {
    eta_plugin_ipc_frame response;
    eta_status result = client_request(client, ETA_PLUGIN_IPC_PING, NULL, 0, &response);
    if (result != ETA_OK) return result;
    const uint8_t *data; size_t size;
    return response_status(&response, &data, &size);
}
