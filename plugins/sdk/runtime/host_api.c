#include "eta/host_api.h"

#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static eta_status transfer(int fd, void *buffer, size_t size, int send_mode) {
    size_t offset = 0;
    while (offset < size) {
        ssize_t result;
        if (send_mode) {
            result = send(fd, (const char *)buffer + offset, size - offset, 0);
        } else {
            result = recv(fd, (char *)buffer + offset, size - offset, 0);
        }
        if (result <= 0) {
            return result == 0 ? ETA_E_IO : ETA_E_IO;
        }
        offset += (size_t)result;
    }
    return 0;
}

eta_status eta_plugin_ipc_send(int fd, eta_plugin_ipc_frame *frame) {
    if (fd < 0 || !frame || frame->magic != ETA_PLUGIN_IPC_MAGIC ||
        frame->version != ETA_PLUGIN_IPC_VERSION ||
        frame->payload_size > ETA_PLUGIN_IPC_MAX_PAYLOAD) {
        return ETA_E_INVALID_ARGUMENT;
    }
    return transfer(fd, frame, sizeof(*frame), 1);
}

eta_status eta_plugin_ipc_recv(int fd, eta_plugin_ipc_frame *frame) {
    if (fd < 0 || !frame) {
        return ETA_E_INVALID_ARGUMENT;
    }
    int result = transfer(fd, frame, sizeof(*frame), 0);
    if (result != 0) {
        return result;
    }
    if (frame->magic != ETA_PLUGIN_IPC_MAGIC ||
        frame->version != ETA_PLUGIN_IPC_VERSION ||
        frame->payload_size > ETA_PLUGIN_IPC_MAX_PAYLOAD) {
        return ETA_E_PROTOCOL;
    }
    return 0;
}

static eta_status send_text(int fd, uint16_t command, const char *message) {
    if (!message) {
        return ETA_E_INVALID_ARGUMENT;
    }
    size_t length = strlen(message);
    if (length >= ETA_PLUGIN_IPC_MAX_PAYLOAD) {
        return ETA_E_INVALID_ARGUMENT;
    }
    eta_plugin_ipc_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.magic = ETA_PLUGIN_IPC_MAGIC;
    frame.version = ETA_PLUGIN_IPC_VERSION;
    frame.command = command;
    frame.payload_size = (uint32_t)length;
    memcpy(frame.payload, message, length);
    return eta_plugin_ipc_send(fd, &frame);
}

eta_status eta_plugin_log(int fd, eta_log_level level, const char *message) {
    (void)level;
    return send_text(fd, ETA_PLUGIN_IPC_LOG, message);
}

eta_status eta_plugin_notify(int fd, const char *message) {
    return send_text(fd, ETA_PLUGIN_IPC_NOTIFY, message);
}
