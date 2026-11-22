#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>

typedef struct ntp_sync_result {
    int success;
    int64_t clock_delta_us;
    char formatted_time[64];
    char server_used[128];
    char error_message[128];
} ntp_sync_result;

int ntp_synchronize_clock(const char *custom_server, ntp_sync_result *result);
