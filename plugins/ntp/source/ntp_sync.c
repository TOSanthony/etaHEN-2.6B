#include "ntp_sync.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef SYS_settimeofday
#define SYS_settimeofday 122
#endif

/* Exported by libSceNet */
int sceNetInit(void);
int sceNetPoolCreate(const char *name, int size, int flags);
int sceNetPoolDestroy(int pool_id);

/* Exported by libkernel */
int gettimeofday(struct timeval *tv, struct timezone *tz);
int settimeofday(const struct timeval *tv, const struct timezone *tz);

#define NTP_PORT 123
#define NTP_PACKET_SIZE 48
#define MAX_NTP_RESPONSE_SIZE 512
#define NTP_ORIGINATE_TIMESTAMP_OFFSET 24
#define NTP_TRANSMIT_TIMESTAMP_OFFSET 40
#define NTP_TIMESTAMP_SIZE 8
#define NTP_VERSION 4
#define NTP_CLIENT_MODE 3
#define NTP_SERVER_MODE 4
#define NTP_CLIENT_FLAGS ((NTP_VERSION << 3) | NTP_CLIENT_MODE)
#define NETWORK_POOL_SIZE (64 * 1024)
#define SOCKET_TIMEOUT_SECONDS 3
#define ATTEMPTS_PER_SERVER 2
#define RETRY_DELAY_SECONDS 1
#define MAX_CLOCK_VERIFICATION_DELTA_US (2 * 1000000LL)

#define NTP_TO_UNIX_EPOCH 2208988800U
#define MINIMUM_UNIX_TIME 946684800LL    /* 2000-01-01 */
#define MAXIMUM_UNIX_TIME 4102444800LL   /* 2100-01-01 */

static const char *const kDefaultNtpServers[] = {
    "fr.pool.ntp.org",
    "europe.pool.ntp.org",
    "de.pool.ntp.org",
    "ptbtime1.ptb.de",
    "ptbtime2.ptb.de",
    "pool.ntp.org",
    "time.cloudflare.com",
    "time.google.com",
};

static bool monotonic_microseconds(int64_t *output) {
    if (!output) return false;
    struct timespec value = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return false;
    *output = (int64_t)value.tv_sec * 1000000LL + (int64_t)(value.tv_nsec / 1000);
    return true;
}

static uint32_t read_big_endian_u32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8)  |
           ((uint32_t)bytes[3]);
}

static void write_big_endian_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)(value);
}

static bool write_client_transmit_timestamp(uint8_t *packet) {
    if (!packet) return false;
    struct timeval current = {0};
    if (gettimeofday(&current, NULL) != 0) return false;
    if (current.tv_usec < 0 || current.tv_usec >= 1000000) return false;

    int64_t unix_seconds = (int64_t)current.tv_sec;
    if (unix_seconds < -(int64_t)NTP_TO_UNIX_EPOCH) return false;

    uint64_t full_ntp_seconds = (uint64_t)(unix_seconds + (int64_t)NTP_TO_UNIX_EPOCH);
    uint32_t wire_seconds = (uint32_t)full_ntp_seconds;
    uint32_t wire_fraction = (uint32_t)(((uint64_t)current.tv_usec << 32) / 1000000ULL);

    if (wire_seconds == 0 && wire_fraction == 0) {
        wire_fraction = 1;
    }
    write_big_endian_u32(packet + NTP_TRANSMIT_TIMESTAMP_OFFSET, wire_seconds);
    write_big_endian_u32(packet + NTP_TRANSMIT_TIMESTAMP_OFFSET + 4, wire_fraction);
    return true;
}

static bool configure_socket_timeouts(int socket_fd) {
    struct timeval timeout = {0};
    timeout.tv_sec = SOCKET_TIMEOUT_SECONDS;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        return false;
    }
    if (setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
        return false;
    }
    return true;
}

static bool resolve_server(const char *hostname, struct sockaddr_in *address) {
    if (!hostname || !address) return false;
    memset(address, 0, sizeof(*address));

    int numeric_result = inet_pton(AF_INET, hostname, &address->sin_addr);
    if (numeric_result == 1) {
        address->sin_family = AF_INET;
        address->sin_port = htons(NTP_PORT);
        return true;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    struct addrinfo *results = NULL;
    int result = getaddrinfo(hostname, NULL, &hints, &results);
    if (result != 0) return false;

    bool resolved = false;
    for (struct addrinfo *curr = results; curr; curr = curr->ai_next) {
        if (!curr->ai_addr || curr->ai_addrlen < sizeof(struct sockaddr_in)) continue;
        memcpy(address, curr->ai_addr, sizeof(struct sockaddr_in));
        address->sin_port = htons(NTP_PORT);
        resolved = true;
        break;
    }
    freeaddrinfo(results);
    return resolved;
}

static bool request_time(const struct sockaddr_in *server, struct timeval *output) {
    if (!server || !output) return false;

    int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd < 0) return false;

    if (!configure_socket_timeouts(socket_fd)) {
        close(socket_fd);
        return false;
    }

    uint8_t request[NTP_PACKET_SIZE] = {0};
    request[0] = NTP_CLIENT_FLAGS;
    if (!write_client_transmit_timestamp(request)) {
        close(socket_fd);
        return false;
    }

    int64_t start_microseconds = 0;
    int64_t end_microseconds = 0;
    bool have_start = monotonic_microseconds(&start_microseconds);

    ssize_t sent = sendto(socket_fd, request, sizeof(request), 0,
                          (const struct sockaddr *)server, sizeof(*server));
    if (sent != (ssize_t)sizeof(request)) {
        close(socket_fd);
        return false;
    }

    uint8_t response[MAX_NTP_RESPONSE_SIZE] = {0};
    struct sockaddr_in source = {0};
    socklen_t source_length = sizeof(source);
    ssize_t received = recvfrom(socket_fd, response, sizeof(response), 0,
                                (struct sockaddr *)&source, &source_length);
    bool have_end = monotonic_microseconds(&end_microseconds);
    close(socket_fd);

    if (received < (ssize_t)NTP_PACKET_SIZE) return false;
    if (source_length < sizeof(source) || source.sin_family != AF_INET ||
        source.sin_port != server->sin_port ||
        source.sin_addr.s_addr != server->sin_addr.s_addr) {
        return false;
    }

    if (memcmp(response + NTP_ORIGINATE_TIMESTAMP_OFFSET,
               request + NTP_TRANSMIT_TIMESTAMP_OFFSET,
               NTP_TIMESTAMP_SIZE) != 0) {
        return false;
    }

    uint8_t flags = response[0];
    uint8_t leap_indicator = flags >> 6;
    uint8_t version = (flags >> 3) & 0x07;
    uint8_t mode = flags & 0x07;
    uint8_t stratum = response[1];

    if (leap_indicator == 3 || mode != NTP_SERVER_MODE ||
        version != NTP_VERSION || stratum == 0 || stratum >= 16) {
        return false;
    }

    uint32_t ntp_seconds = read_big_endian_u32(response + 40);
    uint32_t ntp_fraction = read_big_endian_u32(response + 44);
    if (ntp_seconds == 0 && ntp_fraction == 0) return false;

    uint64_t full_ntp_seconds = ntp_seconds;
    if (full_ntp_seconds < NTP_TO_UNIX_EPOCH) {
        full_ntp_seconds += (1ULL << 32);
    }
    int64_t unix_seconds = (int64_t)(full_ntp_seconds - (uint64_t)NTP_TO_UNIX_EPOCH);
    if (unix_seconds < MINIMUM_UNIX_TIME || unix_seconds > MAXIMUM_UNIX_TIME) {
        return false;
    }

    int64_t microseconds = (int64_t)(((uint64_t)ntp_fraction * 1000000ULL) >> 32);
    if (have_start && have_end && end_microseconds > start_microseconds) {
        microseconds += (end_microseconds - start_microseconds) / 2;
    }

    int64_t total_microseconds = unix_seconds * 1000000LL + microseconds;
    output->tv_sec = (time_t)(total_microseconds / 1000000LL);
    output->tv_usec = (suseconds_t)(total_microseconds % 1000000LL);
    return true;
}

static bool set_system_time(const struct timeval *value, int64_t *out_delta_us) {
    struct timeval candidate = *value;
    int set_rc = settimeofday(&candidate, NULL);
    if (set_rc != 0) {
        set_rc = syscall(SYS_settimeofday, &candidate, NULL);
    }
    if (set_rc != 0) return false;

    struct timeval verified = {0};
    if (gettimeofday(&verified, NULL) != 0) return false;

    int64_t difference =
        ((int64_t)verified.tv_sec - (int64_t)candidate.tv_sec) * 1000000LL +
        ((int64_t)verified.tv_usec - (int64_t)candidate.tv_usec);
    int64_t abs_diff = difference < 0 ? -difference : difference;

    if (out_delta_us) *out_delta_us = difference;
    if (abs_diff > MAX_CLOCK_VERIFICATION_DELTA_US) return false;
    return true;
}

static bool format_utc(const struct timeval *value, char *output, size_t output_size) {
    if (!output || output_size == 0) return false;
    time_t seconds = value->tv_sec;
    struct tm broken_down = {0};
    if (!gmtime_r(&seconds, &broken_down)) return false;
    return strftime(output, output_size, "%Y-%m-%d %H:%M:%S UTC", &broken_down) > 0;
}

int ntp_synchronize_clock(const char *custom_server, ntp_sync_result *result) {
    if (result) {
        memset(result, 0, sizeof(*result));
        snprintf(result->error_message, sizeof(result->error_message), "Initialization failed");
    }

    (void)sceNetInit();
    int pool_id = sceNetPoolCreate("eta_ntp", NETWORK_POOL_SIZE, 0);

    const char *servers[32];
    size_t server_count = 0;

    /* Prioritize the user-configured server if provided */
    if (custom_server && custom_server[0] != '\0') {
        servers[server_count++] = custom_server;
    }

    /* Append default mainland China and global fallback servers */
    for (size_t i = 0; i < sizeof(kDefaultNtpServers) / sizeof(kDefaultNtpServers[0]); ++i) {
        const char *def = kDefaultNtpServers[i];
        bool duplicate = false;
        for (size_t j = 0; j < server_count; ++j) {
            if (strcmp(servers[j], def) == 0) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate && server_count < 30) {
            servers[server_count++] = def;
        }
    }

    struct timeval network_time = {0};
    const char *success_server = NULL;
    bool received_time = false;

    for (size_t i = 0; i < server_count; ++i) {
        const char *server_name = servers[i];
        struct sockaddr_in server_addr = {0};
        if (!resolve_server(server_name, &server_addr)) {
            continue;
        }

        for (int attempt = 0; attempt < ATTEMPTS_PER_SERVER; ++attempt) {
            if (request_time(&server_addr, &network_time)) {
                received_time = true;
                success_server = server_name;
                break;
            }
            if (attempt + 1 < ATTEMPTS_PER_SERVER) {
                sleep(RETRY_DELAY_SECONDS);
            }
        }
        if (received_time) break;
    }

    if (pool_id >= 0) {
        sceNetPoolDestroy(pool_id);
    }

    if (!received_time) {
        if (result) {
            snprintf(result->error_message, sizeof(result->error_message),
                     "Failed to receive valid NTP response");
        }
        return -1;
    }

    int64_t delta_us = 0;
    if (!set_system_time(&network_time, &delta_us)) {
        if (result) {
            snprintf(result->error_message, sizeof(result->error_message),
                     "Failed to set system clock (check permissions)");
        }
        return -2;
    }

    if (result) {
        result->success = 1;
        result->clock_delta_us = delta_us;
        snprintf(result->server_used, sizeof(result->server_used), "%s",
                 success_server ? success_server : "unknown");
        format_utc(&network_time, result->formatted_time, sizeof(result->formatted_time));
        snprintf(result->error_message, sizeof(result->error_message), "Success");
    }

    return 0;
}
