#pragma once

#include <stddef.h>
#include <stdint.h>

#include "eta/client.h"

eta_status eta_client_request(eta_client *client, uint16_t command,
                                  const void *payload, size_t payload_size,
                                  void *out_data, size_t out_capacity,
                                  size_t *out_size);

