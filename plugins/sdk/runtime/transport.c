#include "eta/transport.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static eta_status transfer(int fd, void *data, size_t size, bool sending) {
    size_t offset = 0;
    while (offset < size) {
        ssize_t result;
        if (sending) {
#ifdef MSG_NOSIGNAL
            result = send(fd, (const char *)data + offset,
                          size - offset, MSG_NOSIGNAL);
#else
            result = send(fd, (const char *)data + offset, size - offset, 0);
#endif
        } else {
            result = recv(fd, (char *)data + offset, size - offset, 0);
        }
        if (result == 0) {
            return ETA_E_IO;
        }
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return ETA_E_IO;
        }
        offset += (size_t)result;
    }
    return ETA_OK;
}

eta_status eta_transport_send_frame(const eta_transport *transport,
                                        const eta_plugin_ipc_frame *frame) {
    if (!transport || !transport->send || !frame ||
        frame->magic != ETA_PLUGIN_IPC_MAGIC ||
        frame->version != ETA_PLUGIN_IPC_VERSION ||
        frame->payload_size > ETA_PLUGIN_IPC_MAX_PAYLOAD) {
        return ETA_E_INVALID_ARGUMENT;
    }
    return transport->send(transport->context, frame, sizeof(*frame));
}

eta_status eta_transport_recv_frame(const eta_transport *transport,
                                        eta_plugin_ipc_frame *frame) {
    if (!transport || !transport->recv || !frame) {
        return ETA_E_INVALID_ARGUMENT;
    }
    eta_status result = transport->recv(transport->context, frame, sizeof(*frame));
    if (result != ETA_OK) {
        return result;
    }
    if (frame->magic != ETA_PLUGIN_IPC_MAGIC ||
        frame->version != ETA_PLUGIN_IPC_VERSION ||
        frame->payload_size > ETA_PLUGIN_IPC_MAX_PAYLOAD) {
        return ETA_E_PROTOCOL;
    }
    return ETA_OK;
}

static eta_status socket_send(void *context, const void *data, size_t size) {
    eta_socket_transport *state = (eta_socket_transport *)context;
    return transfer(state->fd, (void *)data, size, true);
}

static eta_status socket_recv(void *context, void *data, size_t size) {
    eta_socket_transport *state = (eta_socket_transport *)context;
    return transfer(state->fd, data, size, false);
}

static void socket_close(void *context) {
    eta_socket_transport *state = (eta_socket_transport *)context;
    if (state && state->owns_fd && state->fd >= 0) {
        close(state->fd);
        state->fd = -1;
    }
}

eta_status eta_socket_transport_init(eta_transport *transport,
                                         eta_socket_transport *state,
                                         int fd, bool owns_fd) {
    if (!transport || !state || fd < 0) {
        return ETA_E_INVALID_ARGUMENT;
    }
    state->fd = fd;
    state->owns_fd = owns_fd;
#ifdef SO_NOSIGPIPE
    int no_sigpipe = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE,
                     &no_sigpipe, sizeof(no_sigpipe));
#endif
    transport->context = state;
    transport->send = socket_send;
    transport->recv = socket_recv;
    transport->close = socket_close;
    return ETA_OK;
}

eta_status eta_socket_transport_connect(eta_transport *transport,
                                            eta_socket_transport *state,
                                            const char *path) {
    if (!transport || !state || !path || !path[0]) {
        return ETA_E_INVALID_ARGUMENT;
    }
    if (strlen(path) >= sizeof(((struct sockaddr_un *)0)->sun_path)) {
        return ETA_E_INVALID_ARGUMENT;
    }
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return ETA_E_IO;
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, path, strlen(path) + 1);
    const socklen_t address_size = (socklen_t)(
        offsetof(struct sockaddr_un, sun_path) + strlen(path) + 1);
    if (connect(fd, (const struct sockaddr *)&address, address_size) != 0) {
        close(fd);
        return ETA_E_IO;
    }
    return eta_socket_transport_init(transport, state, fd, true);
}

void eta_socket_transport_deinit(eta_transport *transport) {
    if (!transport) {
        return;
    }
    if (transport->close) {
        transport->close(transport->context);
    }
    memset(transport, 0, sizeof(*transport));
}
