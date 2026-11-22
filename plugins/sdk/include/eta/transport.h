#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "eta/ipc.h"
#include "eta/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef eta_status (*eta_transport_send_fn)(void *context,
                                                const void *data, size_t size);
typedef eta_status (*eta_transport_recv_fn)(void *context,
                                                void *data, size_t size);
typedef void (*eta_transport_close_fn)(void *context);

/* Dependency inversion point: IPC, shared memory, or a test double can implement this. */
typedef struct eta_transport {
    void *context;
    eta_transport_send_fn send;
    eta_transport_recv_fn recv;
    eta_transport_close_fn close;
} eta_transport;

eta_status eta_transport_send_frame(const eta_transport *transport,
                                        const eta_plugin_ipc_frame *frame);
eta_status eta_transport_recv_frame(const eta_transport *transport,
                                        eta_plugin_ipc_frame *frame);

/* Adapt an already-connected stream socket. The fd is borrowed unless owns_fd is true. */
typedef struct eta_socket_transport {
    int fd;
    bool owns_fd;
} eta_socket_transport;

eta_status eta_socket_transport_init(eta_transport *transport,
                                         eta_socket_transport *state,
                                         int fd, bool owns_fd);
/* Connect to a Unix stream socket and transfer ownership to the transport. */
eta_status eta_socket_transport_connect(eta_transport *transport,
                                            eta_socket_transport *state,
                                            const char *path);
void eta_socket_transport_deinit(eta_transport *transport);

#ifdef __cplusplus
}
#endif
