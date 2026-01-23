/*
 * distributed.c - Distributed Mode Implementation
 */

#include "distributed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>

/* ============================================================================
 * Clean Logging System
 * ============================================================================ */

/* ANSI color codes */
#define CLR_RESET   "\033[0m"
#define CLR_BOLD    "\033[1m"
#define CLR_DIM     "\033[2m"
#define CLR_GREEN   "\033[32m"
#define CLR_YELLOW  "\033[33m"
#define CLR_BLUE    "\033[34m"
#define CLR_CYAN    "\033[36m"
#define CLR_RED     "\033[31m"
#define CLR_MAGENTA "\033[35m"

/* Log prefixes */
#define LOG_SERVER  CLR_CYAN  "[SERVER]" CLR_RESET
#define LOG_CLIENT  CLR_BLUE  "[CLIENT]" CLR_RESET
#define LOG_WORKER  CLR_GREEN "[WORKER]" CLR_RESET
#define LOG_OK      CLR_GREEN "  ✓ " CLR_RESET
#define LOG_INFO    CLR_CYAN  "  ℹ " CLR_RESET
#define LOG_WARN    CLR_YELLOW"  ⚠ " CLR_RESET
#define LOG_ERR     CLR_RED   "  ✗ " CLR_RESET
#define LOG_FOUND   CLR_MAGENTA CLR_BOLD "  ★ " CLR_RESET

/* Simple JSON helpers (minimal, no external deps) with safe buffer handling */
static void json_add_string(char *buf, size_t sz, const char *key, const char *val) {
    size_t current_len = strlen(buf);
    if (current_len >= sz - 1) return;  /* Buffer already full */
    size_t remaining = sz - current_len;
    int written = snprintf(buf + current_len, remaining, "\"%s\":\"%s\",", key, val);
    if (written < 0 || (size_t)written >= remaining) {
        buf[sz - 1] = '\0';  /* Ensure null termination on overflow */
    }
}

static void json_add_int(char *buf, size_t sz, const char *key, int64_t val) {
    size_t current_len = strlen(buf);
    if (current_len >= sz - 1) return;  /* Buffer already full */
    size_t remaining = sz - current_len;
    int written = snprintf(buf + current_len, remaining, "\"%s\":%lld,", key, (long long)val);
    if (written < 0 || (size_t)written >= remaining) {
        buf[sz - 1] = '\0';  /* Ensure null termination on overflow */
    }
}

static void json_add_double(char *buf, size_t sz, const char *key, double val) {
    size_t current_len = strlen(buf);
    if (current_len >= sz - 1) return;  /* Buffer already full */
    size_t remaining = sz - current_len;
    int written = snprintf(buf + current_len, remaining, "\"%s\":%.3f,", key, val);
    if (written < 0 || (size_t)written >= remaining) {
        buf[sz - 1] = '\0';  /* Ensure null termination on overflow */
    }
}

static int json_get_string(const char *json, const char *key, char *out, size_t outsz) {
    if (!json || !key || !out || outsz == 0) return -1;
    out[0] = '\0';  /* Initialize output */

    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char *start = strstr(json, pattern);
    if (!start) return -1;
    start += strlen(pattern);
    const char *end = strchr(start, '"');
    if (!end || end < start) return -1;  /* Also check for inverted pointers */
    size_t len = (size_t)(end - start);
    if (len >= outsz) len = outsz - 1;
    memcpy(out, start, len);  /* memcpy is safer than strncpy here */
    out[len] = '\0';
    return 0;
}

static int64_t json_get_int(const char *json, const char *key) {
    if (!json || !key) return 0;

    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *start = strstr(json, pattern);
    if (!start) return 0;
    start += strlen(pattern);
    /* Skip whitespace */
    while (*start == ' ' || *start == '\t') start++;
    return strtoll(start, NULL, 10);
}

static double json_get_double(const char *json, const char *key) {
    if (!json || !key) return 0.0;

    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *start = strstr(json, pattern);
    if (!start) return 0.0;
    start += strlen(pattern);
    /* Skip whitespace */
    while (*start == ' ' || *start == '\t') start++;
    return strtod(start, NULL);
}

static uint64_t time_ms(void) {
    struct timespec ts;
    /* Use CLOCK_REALTIME (Unix epoch) for compatibility with time(NULL) comparisons */
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

/* Set socket to non-blocking */
static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* Set socket receive/send timeout
 * @param fd Socket file descriptor
 * @param timeout_sec Timeout in seconds (0 = no timeout)
 * @return 0 on success, -1 on error
 */
static int set_socket_timeout(int fd, int timeout_sec) {
    if (fd < 0) return -1;

    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;

    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        return -1;
    }
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        return -1;
    }
    return 0;
}

/* Send all data with retry logic for partial sends */
static int send_all(int fd, const void *data, size_t len) {
    const char *ptr = (const char *)data;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t sent = send(fd, ptr, remaining, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) continue;  /* Interrupted, retry */
            return -1;  /* Real error */
        }
        if (sent == 0) return -1;  /* Connection closed */
        ptr += sent;
        remaining -= (size_t)sent;
    }
    return 0;
}

/* Send message with length prefix */
static int send_msg(int fd, const char *msg) {
    if (!msg) return -1;

    uint32_t len = (uint32_t)strlen(msg);
    if (len > DIST_MAX_MSG_SIZE) return -1;  /* Message too large */

    uint32_t net_len = htonl(len);

    if (send_all(fd, &net_len, 4) != 0) return -1;
    if (send_all(fd, msg, len) != 0) return -1;
    return 0;
}

/* Receive message with length prefix - with proper bounds checking */
static int recv_msg(int fd, char *buf, size_t bufsz) {
    if (!buf || bufsz < 2) return -1;  /* Need at least space for one char + null */

    uint32_t net_len;
    ssize_t n = recv(fd, &net_len, 4, MSG_WAITALL);
    if (n <= 0) return -1;  /* Connection closed (0) or error (-1) */
    if (n != 4) return -1;  /* Partial read */

    uint32_t len = ntohl(net_len);

    /* Validate message size: must fit with null terminator, and have max limit */
    if (len == 0) return -1;  /* Empty message invalid */
    if (len > DIST_MAX_MSG_SIZE) return -1;  /* Prevent DoS with huge messages */
    if (len >= bufsz) return -1;  /* Must have room for null terminator */

    n = recv(fd, buf, len, MSG_WAITALL);
    if (n <= 0) return -1;  /* Connection closed or error */
    if (n != (ssize_t)len) return -1;  /* Partial read */

    buf[len] = '\0';
    return (int)len;
}

/* ============================================================================
 * Coordinator Implementation
 * ============================================================================ */

int dist_coordinator_init(dist_coordinator_t *coord, int port) {
    memset(coord, 0, sizeof(*coord));
    coord->port = (port > 0) ? port : DIST_DEFAULT_PORT;
    coord->listen_socket = -1;

    /* Initialize work mutex for thread-safe work unit assignment */
    if (pthread_mutex_init(&coord->work_mutex, NULL) != 0) {
        return -1;
    }

    /* Allocate results array */
    coord->result_capacity = 1024;
    coord->results = calloc(coord->result_capacity, sizeof(dist_result_t));
    if (!coord->results) {
        pthread_mutex_destroy(&coord->work_mutex);
        return -1;
    }

    return 0;
}

/* Helper: Parse hex string to 128-bit integer (supports up to 128-bit ranges) */
static __uint128_t parse_hex128(const char *hex) {
    __uint128_t result = 0;
    while (*hex) {
        char c = *hex++;
        int digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else continue;  /* Skip invalid chars */
        result = (result << 4) | digit;
    }
    return result;
}

/* Helper: Convert 128-bit integer to hex string */
static void uint128_to_hex(char *buf, size_t sz, __uint128_t val) {
    if (val == 0) {
        snprintf(buf, sz, "0");
        return;
    }
    /* 128-bit = 32 hex digits max + null terminator */
    char tmp[34];
    int i = 32;
    tmp[33] = '\0';
    tmp[i] = '\0';

    while (val > 0 && i > 0) {
        i--;
        int digit = val & 0xF;
        tmp[i] = digit < 10 ? '0' + digit : 'a' + digit - 10;
        val >>= 4;
    }

    /* Copy result to output buffer */
    size_t len = 32 - i;
    if (len >= sz) len = sz - 1;
    memcpy(buf, &tmp[i], len);
    buf[len] = '\0';
}

int dist_coordinator_set_range(dist_coordinator_t *coord,
                               const char *range_start, const char *range_end,
                               uint64_t work_unit_size) {
    if (!coord || !range_start || !range_end) return -1;

    /* Parse range as hex - using 128-bit integers for puzzles up to 128 bits */
    __uint128_t start = parse_hex128(range_start);
    __uint128_t end = parse_hex128(range_end);

    if (end <= start) {
        printf(LOG_SERVER LOG_ERR "Invalid range: end <= start\n");
        return -1;
    }

    __uint128_t total = end - start;

    /* Store as 64-bit for stats (may overflow for very large ranges) */
    if (total > ((__uint128_t)1 << 64)) {
        coord->total_keys = UINT64_MAX;  /* Capped for display */
    } else {
        coord->total_keys = (uint64_t)total;
    }

    /* Calculate number of work units */
    __uint128_t work_size = (__uint128_t)work_unit_size;
    __uint128_t num_units_128 = (total + work_size - 1) / work_size;

    int num_units;
    if (num_units_128 > 100000) {
        num_units = 100000;  /* Limit to prevent excessive memory */
        printf(LOG_SERVER LOG_WARN "Range too large, limiting to %d work units\n", num_units);
    } else {
        num_units = (int)num_units_128;
    }

    coord->work_units = calloc(num_units, sizeof(dist_work_unit_t));
    if (!coord->work_units) return -1;

    /* Create work units */
    __uint128_t pos = start;
    for (int i = 0; i < num_units && pos < end; i++) {
        __uint128_t unit_end = pos + work_size;
        if (unit_end > end) unit_end = end;

        dist_work_unit_t *unit = &coord->work_units[i];
        unit->id = i;
        uint128_to_hex(unit->range_start, sizeof(unit->range_start), pos);
        uint128_to_hex(unit->range_end, sizeof(unit->range_end), unit_end);
        unit->status = WORK_STATUS_PENDING;
        unit->assigned_worker = -1;

        /* Keys in unit (capped to 64-bit for stats) */
        __uint128_t keys = unit_end - pos;
        unit->keys_in_unit = keys > UINT64_MAX ? UINT64_MAX : (uint64_t)keys;

        coord->work_unit_count++;
        coord->work_units_pending++;
        pos = unit_end;
    }

    printf(LOG_SERVER LOG_OK "Created %d work units (range 0x%.16s...)\n",
           coord->work_unit_count, range_start);
    return coord->work_unit_count;
}

int dist_coordinator_start(dist_coordinator_t *coord) {
    /* Create socket */
    coord->listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (coord->listen_socket < 0) {
        perror("[Coordinator] socket");
        return -1;
    }

    int opt = 1;
    setsockopt(coord->listen_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(coord->port);

    if (bind(coord->listen_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[Coordinator] bind");
        close(coord->listen_socket);
        return -1;
    }

    if (listen(coord->listen_socket, 16) < 0) {
        perror("[Coordinator] listen");
        close(coord->listen_socket);
        return -1;
    }

    set_nonblocking(coord->listen_socket);
    coord->running = true;

    printf(LOG_SERVER LOG_OK "Listening on port " CLR_BOLD "%d" CLR_RESET "\n", coord->port);
    return 0;
}

/* Find next pending work unit */
static dist_work_unit_t* find_pending_work(dist_coordinator_t *coord) {
    for (int i = 0; i < coord->work_unit_count; i++) {
        if (coord->work_units[i].status == WORK_STATUS_PENDING) {
            return &coord->work_units[i];
        }
    }
    return NULL;
}

/* Handle worker message */
static int handle_worker_msg(dist_coordinator_t *coord, int worker_idx, const char *msg) {
    dist_worker_t *worker = &coord->workers[worker_idx];
    char type[32] = {0};
    json_get_string(msg, "type", type, sizeof(type));

    if (strcmp(type, "request_work") == 0) {
        /* Update heartbeat - worker is alive */
        worker->last_heartbeat = time_ms();

        char response[DIST_MAX_MSG_SIZE];

        /* Lock work mutex for thread-safe assignment */
        pthread_mutex_lock(&coord->work_mutex);

        dist_work_unit_t *unit = find_pending_work(coord);

        if (unit) {
            unit->status = WORK_STATUS_ASSIGNED;
            unit->assigned_worker = worker->id;
            unit->assigned_time = time_ms();
            worker->current_work_id = unit->id;
            coord->work_units_pending--;

            snprintf(response, sizeof(response), "{");
            json_add_string(response, sizeof(response), "type", "work_assignment");
            json_add_int(response, sizeof(response), "work_id", unit->id);
            json_add_string(response, sizeof(response), "range_start", unit->range_start);
            json_add_string(response, sizeof(response), "range_end", unit->range_end);
            /* Remove trailing comma */
            size_t len = strlen(response);
            if (len > 0 && response[len-1] == ',') response[len-1] = '\0';
            strcat(response, "}");
        } else {
            snprintf(response, sizeof(response), "{\"type\":\"no_work\"}");
        }

        pthread_mutex_unlock(&coord->work_mutex);

        send_msg(worker->socket_fd, response);

    } else if (strcmp(type, "work_done") == 0) {
        int work_id = (int)json_get_int(msg, "work_id");
        uint64_t keys = (uint64_t)json_get_int(msg, "keys_processed");
        uint64_t elapsed = (uint64_t)json_get_int(msg, "elapsed_ms");

        /* Update heartbeat - worker is alive */
        worker->last_heartbeat = time_ms();

        /* Lock work mutex for thread-safe completion */
        pthread_mutex_lock(&coord->work_mutex);

        if (work_id >= 0 && work_id < coord->work_unit_count) {
            dist_work_unit_t *unit = &coord->work_units[work_id];
            unit->status = WORK_STATUS_COMPLETED;
            unit->completed_time = time_ms();
            coord->work_units_completed++;
        }

        worker->keys_processed += keys;
        coord->keys_processed += keys;

        pthread_mutex_unlock(&coord->work_mutex);

        if (elapsed > 0) {
            worker->throughput = (double)keys / (double)elapsed * 1000.0 / 1000000.0;
            /* Update speed stats for dashboard - use CPU field if no GPU indicated */
            if (worker->gpu_memory_mb > 0) {
                worker->gpu_speed_mkeys = worker->throughput;
            } else {
                worker->cpu_speed_mkeys = worker->throughput;
            }
        }

        /* Send ack */
        send_msg(worker->socket_fd, "{\"type\":\"ack\"}");

    } else if (strcmp(type, "found") == 0) {
        char privkey[65] = {0}, address[36] = {0};
        json_get_string(msg, "private_key", privkey, sizeof(privkey));
        json_get_string(msg, "address", address, sizeof(address));

        if (coord->result_count < coord->result_capacity) {
            dist_result_t *result = &coord->results[coord->result_count++];
            strncpy(result->private_key, privkey, sizeof(result->private_key)-1);
            strncpy(result->address, address, sizeof(result->address)-1);
            result->worker_id = worker->id;
            result->found_time = time_ms();

            printf("\n" LOG_SERVER LOG_FOUND CLR_BOLD CLR_MAGENTA "KEY FOUND!" CLR_RESET " Worker #%d\n", worker->id);
            printf(LOG_INFO "Private Key: " CLR_BOLD "%s" CLR_RESET "\n", privkey);
            printf(LOG_INFO "Address:     " CLR_BOLD "%s" CLR_RESET "\n\n", address);
        }

        send_msg(worker->socket_fd, "{\"type\":\"ack\"}");

    } else if (strcmp(type, "heartbeat") == 0) {
        worker->last_heartbeat = time_ms();
        send_msg(worker->socket_fd, "{\"type\":\"ack\"}");
    }

    return 0;
}

int dist_coordinator_process(dist_coordinator_t *coord, int timeout_ms) {
    if (!coord->running) return -1;

    fd_set readfds;
    FD_ZERO(&readfds);

    int maxfd = coord->listen_socket;
    FD_SET(coord->listen_socket, &readfds);

    /* Add worker sockets */
    for (int i = 0; i < coord->worker_count; i++) {
        if (coord->workers[i].connected && coord->workers[i].socket_fd >= 0) {
            FD_SET(coord->workers[i].socket_fd, &readfds);
            if (coord->workers[i].socket_fd > maxfd) {
                maxfd = coord->workers[i].socket_fd;
            }
        }
    }

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ready = select(maxfd + 1, &readfds, NULL, NULL, &tv);
    if (ready < 0) {
        if (errno == EINTR) return 0;
        return -1;
    }

    /* Check for new connections */
    if (FD_ISSET(coord->listen_socket, &readfds)) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(coord->listen_socket, (struct sockaddr*)&client_addr, &client_len);

        if (client_fd >= 0 && coord->worker_count < DIST_MAX_WORKERS) {
            int opt = 1;
            setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

            /* Set socket timeout to prevent hanging on malicious/stuck clients */
            set_socket_timeout(client_fd, 60);  /* 60 second timeout */

            /* Read registration message */
            char msg[DIST_MAX_MSG_SIZE];
            if (recv_msg(client_fd, msg, sizeof(msg)) > 0) {
                char type[32] = {0};
                json_get_string(msg, "type", type, sizeof(type));

                if (strcmp(type, "register") == 0) {
                    /* Validate authentication token if enabled */
                    bool auth_passed = true;
                    if (coord->auth_enabled) {
                        char provided_token[DIST_AUTH_TOKEN_MAX] = {0};
                        json_get_string(msg, "auth_token", provided_token, sizeof(provided_token));

                        /* Constant-time comparison to prevent timing attacks */
                        size_t expected_len = strlen(coord->auth_token);
                        size_t provided_len = strlen(provided_token);

                        if (expected_len != provided_len) {
                            auth_passed = false;
                        } else {
                            volatile int diff = 0;
                            for (size_t i = 0; i < expected_len; i++) {
                                diff |= coord->auth_token[i] ^ provided_token[i];
                            }
                            auth_passed = (diff == 0);
                        }

                        if (!auth_passed) {
                            char hostname[64] = {0};
                            json_get_string(msg, "hostname", hostname, sizeof(hostname));
                            printf(LOG_SERVER LOG_ERR "Authentication failed from %s - invalid token\n",
                                   hostname[0] ? hostname : "unknown");
                            send_msg(client_fd, "{\"type\":\"auth_failed\",\"message\":\"Invalid authentication token\"}");
                            close(client_fd);
                        }
                    }

                    if (auth_passed) {
                    dist_worker_t *worker = &coord->workers[coord->worker_count];
                    worker->id = coord->worker_count;
                    worker->socket_fd = client_fd;
                    worker->connected = true;
                    worker->last_heartbeat = time_ms();
                    worker->perf_score = json_get_double(msg, "perf_score");
                    json_get_string(msg, "hostname", worker->hostname, sizeof(worker->hostname));

                    /* Parse detailed hardware info */
                    worker->cpu_cores = (int)json_get_int(msg, "cpu_cores");
                    worker->cpu_threads = (int)json_get_int(msg, "cpu_threads");
                    json_get_string(msg, "cpu_name", worker->cpu_name, sizeof(worker->cpu_name));
                    json_get_string(msg, "gpu_name", worker->gpu_name, sizeof(worker->gpu_name));
                    worker->gpu_memory_mb = (int)json_get_int(msg, "gpu_memory_mb");
                    worker->cpu_speed_mkeys = json_get_double(msg, "cpu_speed_mkeys");
                    worker->gpu_speed_mkeys = json_get_double(msg, "gpu_speed_mkeys");

                    coord->worker_count++;

                    /* Send welcome with job config */
                    char welcome[DIST_MAX_MSG_SIZE];
                    snprintf(welcome, sizeof(welcome),
                             "{\"type\":\"welcome\",\"worker_id\":%d,\"work_units\":%d,"
                             "\"target_address\":\"%s\",\"mode\":\"%s\",\"key_type\":\"%s\","
                             "\"puzzle_number\":%d,\"bits\":%d,\"heartbeat_interval\":%d}",
                             worker->id, coord->work_unit_count,
                             coord->job_target_address,
                             coord->job_mode,
                             coord->job_key_type,
                             coord->job_puzzle_number,
                             coord->job_bits,
                             coord->heartbeat_interval_sec > 0 ? coord->heartbeat_interval_sec : 30);
                    send_msg(client_fd, welcome);

                    /* Print connection info with hardware details and speeds */
                    printf(LOG_SERVER LOG_OK "Worker " CLR_GREEN "#%d" CLR_RESET " connected from " CLR_BOLD "%s" CLR_RESET "\n",
                           worker->id, worker->hostname[0] ? worker->hostname : "localhost");
                    if (worker->cpu_name[0]) {
                        printf(LOG_SERVER LOG_INFO "  Hardware: %s", worker->cpu_name);
                        if (worker->gpu_name[0]) {
                            printf(" + %s", worker->gpu_name);
                        }
                        printf("\n");
                    }
                    /* Show speeds */
                    double worker_total = worker->cpu_speed_mkeys + worker->gpu_speed_mkeys;
                    printf(LOG_SERVER LOG_INFO "  Speed: CPU: " CLR_CYAN "%.2f" CLR_RESET " Mkeys/s",
                           worker->cpu_speed_mkeys);
                    if (worker->gpu_speed_mkeys > 0) {
                        printf(" | GPU: " CLR_GREEN "%.2f" CLR_RESET " Mkeys/s", worker->gpu_speed_mkeys);
                    }
                    printf(" | Total: " CLR_BOLD CLR_GREEN "%.2f" CLR_RESET " Mkeys/s\n", worker_total);
                    }  /* end if (auth_passed) */
                }
            } else {
                close(client_fd);
            }
        } else if (client_fd >= 0) {
            close(client_fd);  /* Too many workers */
        }
    }

    /* Check worker messages */
    for (int i = 0; i < coord->worker_count; i++) {
        dist_worker_t *worker = &coord->workers[i];
        if (!worker->connected || worker->socket_fd < 0) continue;

        if (FD_ISSET(worker->socket_fd, &readfds)) {
            char msg[DIST_MAX_MSG_SIZE];
            int n = recv_msg(worker->socket_fd, msg, sizeof(msg));
            if (n > 0) {
                handle_worker_msg(coord, i, msg);
            } else {
                /* Worker disconnected */
                printf(LOG_SERVER LOG_WARN "Worker " CLR_YELLOW "#%d" CLR_RESET " disconnected\n", worker->id);
                close(worker->socket_fd);
                worker->socket_fd = -1;
                worker->connected = false;

                /* Reassign work if any */
                pthread_mutex_lock(&coord->work_mutex);
                if (worker->current_work_id >= 0 &&
                    worker->current_work_id < coord->work_unit_count) {
                    dist_work_unit_t *unit = &coord->work_units[worker->current_work_id];
                    if (unit->status == WORK_STATUS_ASSIGNED) {
                        unit->status = WORK_STATUS_PENDING;
                        coord->work_units_pending++;
                    }
                }
                pthread_mutex_unlock(&coord->work_mutex);
            }
        }
    }

    /* Check for stale work units (assigned but worker unresponsive) */
    uint64_t now_ms = time_ms();
    uint64_t stale_timeout_ms = 5 * 60 * 1000;  /* 5 minutes */

    for (int i = 0; i < coord->work_unit_count; i++) {
        dist_work_unit_t *unit = &coord->work_units[i];
        if (unit->status == WORK_STATUS_ASSIGNED) {
            /* Check if assigned worker is still connected and responsive */
            bool worker_connected = false;
            bool worker_responsive = false;

            for (int w = 0; w < coord->worker_count; w++) {
                if (coord->workers[w].id == unit->assigned_worker) {
                    worker_connected = coord->workers[w].connected;
                    if (worker_connected) {
                        /* Check if heartbeat is recent */
                        worker_responsive = (now_ms - coord->workers[w].last_heartbeat < stale_timeout_ms);
                    }
                    break;
                }
            }

            /* Reassign if:
             * 1. Worker is disconnected, OR
             * 2. Worker connected but unresponsive AND assignment is old
             * This prevents premature reassignment when worker is still connected
             * but just has a delayed heartbeat */
            bool should_reassign = false;
            if (!worker_connected) {
                /* Worker disconnected - reassign immediately */
                should_reassign = true;
            } else if (!worker_responsive && (now_ms - unit->assigned_time > stale_timeout_ms)) {
                /* Worker connected but unresponsive and assignment is old */
                should_reassign = true;
            }

            if (should_reassign) {
                printf(LOG_SERVER LOG_WARN "Work unit #%d timed out, reassigning\n", unit->id);
                pthread_mutex_lock(&coord->work_mutex);
                unit->status = WORK_STATUS_PENDING;
                unit->assigned_worker = -1;
                coord->work_units_pending++;
                pthread_mutex_unlock(&coord->work_mutex);
            }
        }
    }

    /* Check for stuck workers (connected but no progress for too long) */
    uint64_t stuck_timeout_ms = 3 * 60 * 1000;  /* 3 minutes no progress */

    for (int i = 0; i < coord->worker_count; i++) {
        dist_worker_t *worker = &coord->workers[i];
        if (worker->connected && worker->socket_fd >= 0) {
            /* Worker is stuck if no heartbeat for 3 minutes */
            if (now_ms - worker->last_heartbeat > stuck_timeout_ms) {
                printf(LOG_SERVER LOG_WARN "Worker #%d (%s) stuck for 3+ min, forcing disconnect\n",
                       worker->id, worker->hostname[0] ? worker->hostname : "localhost");

                /* Force disconnect - this will trigger work reassignment */
                close(worker->socket_fd);
                worker->socket_fd = -1;
                worker->connected = false;
                worker->throughput = 0.0;

                /* Reassign any work this worker had */
                pthread_mutex_lock(&coord->work_mutex);
                if (worker->current_work_id >= 0 &&
                    worker->current_work_id < coord->work_unit_count) {
                    dist_work_unit_t *unit = &coord->work_units[worker->current_work_id];
                    if (unit->status == WORK_STATUS_ASSIGNED) {
                        unit->status = WORK_STATUS_PENDING;
                        unit->assigned_worker = -1;
                        coord->work_units_pending++;
                        printf(LOG_SERVER LOG_INFO "Work unit #%d reassigned to pool\n", unit->id);
                    }
                }
                pthread_mutex_unlock(&coord->work_mutex);
            }
        }
    }

    /* Update total throughput */
    coord->total_throughput = 0.0;
    for (int i = 0; i < coord->worker_count; i++) {
        if (coord->workers[i].connected) {
            coord->total_throughput += coord->workers[i].throughput;
        }
    }

    /* Check if all done */
    if (coord->work_units_completed >= coord->work_unit_count) {
        coord->all_work_done = true;
        return 1;
    }

    return 0;
}

void dist_coordinator_stats(const dist_coordinator_t *coord,
                            int *workers_active, int *work_pending,
                            int *work_completed, double *throughput) {
    int active = 0;
    for (int i = 0; i < coord->worker_count; i++) {
        if (coord->workers[i].connected) active++;
    }

    if (workers_active) *workers_active = active;
    if (work_pending) *work_pending = coord->work_units_pending;
    if (work_completed) *work_completed = coord->work_units_completed;
    if (throughput) *throughput = coord->total_throughput;
}

void dist_coordinator_get_speed_stats(const dist_coordinator_t *coord,
                                      double *total_cpu_speed,
                                      double *total_gpu_speed,
                                      double *total_combined) {
    double cpu_sum = 0.0;
    double gpu_sum = 0.0;

    for (int i = 0; i < coord->worker_count; i++) {
        if (coord->workers[i].connected) {
            cpu_sum += coord->workers[i].cpu_speed_mkeys;
            gpu_sum += coord->workers[i].gpu_speed_mkeys;
        }
    }

    if (total_cpu_speed) *total_cpu_speed = cpu_sum;
    if (total_gpu_speed) *total_gpu_speed = gpu_sum;
    if (total_combined) *total_combined = cpu_sum + gpu_sum;
}

void dist_coordinator_print_worker_stats(const dist_coordinator_t *coord) {
    double total_cpu = 0.0;
    double total_gpu = 0.0;
    int active_count = 0;

    printf("\n" CLR_CYAN "┌──────────────────────────────────────────────────────────────────┐" CLR_RESET "\n");
    printf(CLR_CYAN "│" CLR_RESET CLR_BOLD "  Worker Statistics                                               " CLR_RESET CLR_CYAN "│" CLR_RESET "\n");
    printf(CLR_CYAN "├──────┬────────────────┬────────────────┬────────────────┬─────────┤" CLR_RESET "\n");
    printf(CLR_CYAN "│" CLR_RESET " ID   " CLR_CYAN "│" CLR_RESET " CPU (Mkeys/s) " CLR_CYAN "│" CLR_RESET " GPU (Mkeys/s) " CLR_CYAN "│" CLR_RESET " Total (Mkeys/s)" CLR_CYAN "│" CLR_RESET " Status " CLR_CYAN "│" CLR_RESET "\n");
    printf(CLR_CYAN "├──────┼────────────────┼────────────────┼────────────────┼─────────┤" CLR_RESET "\n");

    for (int i = 0; i < coord->worker_count; i++) {
        const dist_worker_t *w = &coord->workers[i];
        double worker_total = w->cpu_speed_mkeys + w->gpu_speed_mkeys;
        const char *status = w->connected ? CLR_GREEN "Online" CLR_RESET : CLR_RED "Offline" CLR_RESET;

        printf(CLR_CYAN "│" CLR_RESET " %4d " CLR_CYAN "│" CLR_RESET " %14.2f " CLR_CYAN "│" CLR_RESET " %14.2f " CLR_CYAN "│" CLR_RESET " %14.2f " CLR_CYAN "│" CLR_RESET " %s " CLR_CYAN "│" CLR_RESET "\n",
               w->id, w->cpu_speed_mkeys, w->gpu_speed_mkeys, worker_total, status);

        if (w->connected) {
            total_cpu += w->cpu_speed_mkeys;
            total_gpu += w->gpu_speed_mkeys;
            active_count++;
        }
    }

    printf(CLR_CYAN "├──────┴────────────────┴────────────────┴────────────────┴─────────┤" CLR_RESET "\n");
    printf(CLR_CYAN "│" CLR_RESET CLR_BOLD " TOTAL (%d workers): CPU: %.2f | GPU: %.2f | Combined: " CLR_GREEN "%.2f" CLR_RESET " Mkeys/s " CLR_CYAN "│" CLR_RESET "\n",
           active_count, total_cpu, total_gpu, total_cpu + total_gpu);
    printf(CLR_CYAN "└──────────────────────────────────────────────────────────────────┘" CLR_RESET "\n\n");
}

void dist_coordinator_shutdown(dist_coordinator_t *coord) {
    coord->running = false;

    /* Close worker connections */
    for (int i = 0; i < coord->worker_count; i++) {
        if (coord->workers[i].socket_fd >= 0) {
            send_msg(coord->workers[i].socket_fd, "{\"type\":\"shutdown\"}");
            close(coord->workers[i].socket_fd);
        }
    }

    if (coord->listen_socket >= 0) {
        close(coord->listen_socket);
    }

    free(coord->work_units);
    free(coord->results);

    /* Destroy work mutex */
    pthread_mutex_destroy(&coord->work_mutex);

    printf(LOG_SERVER LOG_INFO "Shutdown complete. " CLR_BOLD "%d" CLR_RESET " results found.\n", coord->result_count);
}

/* ============================================================================
 * Worker Client Implementation
 * ============================================================================ */

int dist_worker_init(dist_worker_client_t *client,
                     const char *coordinator_host, int coordinator_port,
                     double perf_score) {
    memset(client, 0, sizeof(*client));
    strncpy(client->coordinator_host, coordinator_host, sizeof(client->coordinator_host)-1);
    client->coordinator_port = (coordinator_port > 0) ? coordinator_port : DIST_DEFAULT_PORT;
    client->perf_score = perf_score;
    client->socket_fd = -1;

    /* Generate worker ID */
    char hostname[64] = {0};
    gethostname(hostname, sizeof(hostname)-1);
    snprintf(client->worker_id, sizeof(client->worker_id), "%s-%d", hostname, getpid());

    return 0;
}

int dist_worker_connect(dist_worker_client_t *client) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", client->coordinator_port);

    if (getaddrinfo(client->coordinator_host, port_str, &hints, &res) != 0) {
        return -1;
    }

    client->socket_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (client->socket_fd < 0) {
        freeaddrinfo(res);
        return -1;
    }

    if (connect(client->socket_fd, res->ai_addr, res->ai_addrlen) < 0) {
        close(client->socket_fd);
        client->socket_fd = -1;
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);

    int opt = 1;
    setsockopt(client->socket_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    /* Send registration with hardware info */
    char hostname[64] = {0};
    gethostname(hostname, sizeof(hostname)-1);

    char msg[DIST_MAX_MSG_SIZE];
    snprintf(msg, sizeof(msg), "{");
    json_add_string(msg, sizeof(msg), "type", "register");
    json_add_string(msg, sizeof(msg), "id", client->worker_id);
    json_add_string(msg, sizeof(msg), "hostname", hostname);
    json_add_double(msg, sizeof(msg), "perf_score", client->perf_score);
    json_add_int(msg, sizeof(msg), "cpu_cores", client->cpu_cores);
    json_add_int(msg, sizeof(msg), "cpu_threads", client->cpu_threads);
    json_add_string(msg, sizeof(msg), "cpu_name", client->cpu_name);
    json_add_string(msg, sizeof(msg), "gpu_name", client->gpu_name);
    json_add_int(msg, sizeof(msg), "gpu_memory_mb", client->gpu_memory_mb);
    json_add_double(msg, sizeof(msg), "cpu_speed_mkeys", client->cpu_speed_mkeys);
    json_add_double(msg, sizeof(msg), "gpu_speed_mkeys", client->gpu_speed_mkeys);
    /* Include auth token if set */
    if (client->auth_token[0] != '\0') {
        json_add_string(msg, sizeof(msg), "auth_token", client->auth_token);
    }
    size_t len = strlen(msg);
    if (len > 0 && msg[len-1] == ',') msg[len-1] = '\0';
    strcat(msg, "}");

    if (send_msg(client->socket_fd, msg) != 0) {
        close(client->socket_fd);
        client->socket_fd = -1;
        return -1;
    }

    /* Wait for welcome with job config */
    char response[DIST_MAX_MSG_SIZE];
    if (recv_msg(client->socket_fd, response, sizeof(response)) <= 0) {
        close(client->socket_fd);
        client->socket_fd = -1;
        return -1;
    }

    /* Check for authentication failure */
    char resp_type[32] = {0};
    json_get_string(response, "type", resp_type, sizeof(resp_type));
    if (strcmp(resp_type, "auth_failed") == 0) {
        char error_msg[256] = {0};
        json_get_string(response, "message", error_msg, sizeof(error_msg));
        printf(LOG_CLIENT LOG_ERR "Authentication failed: %s\n",
               error_msg[0] ? error_msg : "Invalid token");
        close(client->socket_fd);
        client->socket_fd = -1;
        return -1;
    }

    /* Parse job config from welcome message */
    json_get_string(response, "target_address", client->received_target_address,
                    sizeof(client->received_target_address));
    json_get_string(response, "mode", client->received_mode, sizeof(client->received_mode));
    json_get_string(response, "key_type", client->received_key_type, sizeof(client->received_key_type));
    client->received_puzzle_number = (int)json_get_int(response, "puzzle_number");
    client->received_bits = (int)json_get_int(response, "bits");
    client->heartbeat_interval_sec = (int)json_get_int(response, "heartbeat_interval");
    if (client->heartbeat_interval_sec <= 0) client->heartbeat_interval_sec = 30;

    client->connected = true;
    printf(LOG_CLIENT LOG_OK "Connected to " CLR_BOLD "%s:%d" CLR_RESET "\n",
           client->coordinator_host, client->coordinator_port);

    if (client->received_target_address[0]) {
        printf(LOG_CLIENT LOG_INFO "Job: Puzzle " CLR_BOLD "#%d" CLR_RESET " (%d bits) | Mode: %s\n",
               client->received_puzzle_number, client->received_bits, client->received_mode);
        printf(LOG_CLIENT LOG_INFO "Target: %.40s%s\n", client->received_target_address,
               strlen(client->received_target_address) > 40 ? "..." : "");
    }

    return 0;
}

int dist_worker_request_work(dist_worker_client_t *client,
                             char *range_start, char *range_end) {
    if (!client->connected) return -1;

    if (send_msg(client->socket_fd, "{\"type\":\"request_work\"}") != 0) {
        return -1;
    }

    char response[DIST_MAX_MSG_SIZE];
    if (recv_msg(client->socket_fd, response, sizeof(response)) <= 0) {
        return -1;
    }

    char type[32] = {0};
    json_get_string(response, "type", type, sizeof(type));

    if (strcmp(type, "work_assignment") == 0) {
        client->current_work_id = (int)json_get_int(response, "work_id");
        json_get_string(response, "range_start", range_start, 65);
        json_get_string(response, "range_end", range_end, 65);
        strncpy(client->current_range_start, range_start, sizeof(client->current_range_start)-1);
        strncpy(client->current_range_end, range_end, sizeof(client->current_range_end)-1);
        client->has_work = true;
        return 0;
    } else if (strcmp(type, "no_work") == 0) {
        client->has_work = false;
        return 1;  /* No more work */
    }

    return -1;
}

int dist_worker_report_done(dist_worker_client_t *client,
                            uint64_t keys_processed, uint64_t elapsed_ms) {
    if (!client->connected || !client->has_work) return -1;

    char msg[512];
    snprintf(msg, sizeof(msg), "{");
    json_add_string(msg, sizeof(msg), "type", "work_done");
    json_add_int(msg, sizeof(msg), "work_id", client->current_work_id);
    json_add_int(msg, sizeof(msg), "keys_processed", keys_processed);
    json_add_int(msg, sizeof(msg), "elapsed_ms", elapsed_ms);
    size_t len = strlen(msg);
    if (len > 0 && msg[len-1] == ',') msg[len-1] = '\0';
    strcat(msg, "}");

    if (send_msg(client->socket_fd, msg) != 0) return -1;

    char response[DIST_MAX_MSG_SIZE];
    if (recv_msg(client->socket_fd, response, sizeof(response)) <= 0) return -1;

    client->keys_processed += keys_processed;
    client->has_work = false;
    return 0;
}

int dist_worker_report_found(dist_worker_client_t *client,
                             const char *private_key, const char *address) {
    if (!client->connected) return -1;

    char msg[512];
    snprintf(msg, sizeof(msg), "{");
    json_add_string(msg, sizeof(msg), "type", "found");
    json_add_string(msg, sizeof(msg), "private_key", private_key);
    json_add_string(msg, sizeof(msg), "address", address);
    size_t len = strlen(msg);
    if (len > 0 && msg[len-1] == ',') msg[len-1] = '\0';
    strcat(msg, "}");

    if (send_msg(client->socket_fd, msg) != 0) return -1;

    char response[DIST_MAX_MSG_SIZE];
    return (recv_msg(client->socket_fd, response, sizeof(response)) > 0) ? 0 : -1;
}

int dist_worker_heartbeat(dist_worker_client_t *client, uint64_t keys_since_last) {
    if (!client->connected) return -1;

    char msg[256];
    snprintf(msg, sizeof(msg), "{\"type\":\"heartbeat\",\"keys\":%llu}",
             (unsigned long long)keys_since_last);

    if (send_msg(client->socket_fd, msg) != 0) return -1;

    char response[DIST_MAX_MSG_SIZE];
    return (recv_msg(client->socket_fd, response, sizeof(response)) > 0) ? 0 : -1;
}

void dist_worker_disconnect(dist_worker_client_t *client) {
    if (client->socket_fd >= 0) {
        close(client->socket_fd);
        client->socket_fd = -1;
    }
    client->connected = false;
    printf(LOG_CLIENT LOG_INFO "Disconnected\n");
}

/* ============================================================================
 * New API Functions for Protocol Enhancements
 * ============================================================================ */

void dist_coordinator_set_job_config(dist_coordinator_t *coordinator,
                                     const char *target_address,
                                     const char *mode,
                                     const char *key_type,
                                     int puzzle_number,
                                     int bits) {
    if (!coordinator) return;

    if (target_address) {
        strncpy(coordinator->job_target_address, target_address,
                sizeof(coordinator->job_target_address) - 1);
    }
    if (mode) {
        strncpy(coordinator->job_mode, mode, sizeof(coordinator->job_mode) - 1);
    }
    if (key_type) {
        strncpy(coordinator->job_key_type, key_type, sizeof(coordinator->job_key_type) - 1);
    }
    coordinator->job_puzzle_number = puzzle_number;
    coordinator->job_bits = bits;

    printf(LOG_SERVER LOG_INFO "Job: Puzzle " CLR_BOLD "#%d" CLR_RESET " (%d bits) | Mode: %s | Keys: %s\n",
           puzzle_number, bits, mode ? mode : "?", key_type ? key_type : "?");
}

void dist_coordinator_set_heartbeat_interval(dist_coordinator_t *coordinator,
                                             int interval_sec) {
    if (!coordinator) return;
    coordinator->heartbeat_interval_sec = interval_sec > 0 ? interval_sec : 30;
}

void dist_coordinator_set_auth_token(dist_coordinator_t *coordinator,
                                     const char *token) {
    if (!coordinator) return;

    if (token && token[0] != '\0') {
        strncpy(coordinator->auth_token, token, DIST_AUTH_TOKEN_MAX - 1);
        coordinator->auth_token[DIST_AUTH_TOKEN_MAX - 1] = '\0';
        coordinator->auth_enabled = true;
        printf(LOG_SERVER LOG_INFO "Authentication enabled (token required)\n");
    } else {
        coordinator->auth_token[0] = '\0';
        coordinator->auth_enabled = false;
    }
}

void dist_worker_set_hardware_info(dist_worker_client_t *client,
                                   int cpu_cores, int cpu_threads,
                                   const char *cpu_name,
                                   const char *gpu_name, int gpu_memory_mb) {
    if (!client) return;

    client->cpu_cores = cpu_cores;
    client->cpu_threads = cpu_threads;
    if (cpu_name) {
        strncpy(client->cpu_name, cpu_name, sizeof(client->cpu_name) - 1);
    }
    if (gpu_name) {
        strncpy(client->gpu_name, gpu_name, sizeof(client->gpu_name) - 1);
    }
    client->gpu_memory_mb = gpu_memory_mb;
}

int dist_worker_get_job_config(const dist_worker_client_t *client,
                               char *target_address,
                               char *mode,
                               char *key_type) {
    if (!client || !client->connected) return -1;
    if (!client->received_target_address[0]) return -1;  /* No config received */

    if (target_address) {
        strncpy(target_address, client->received_target_address, 63);
        target_address[63] = '\0';
    }
    if (mode) {
        strncpy(mode, client->received_mode, 31);
        mode[31] = '\0';
    }
    if (key_type) {
        strncpy(key_type, client->received_key_type, 15);
        key_type[15] = '\0';
    }
    return 0;
}

int dist_worker_get_heartbeat_interval(const dist_worker_client_t *client) {
    if (!client) return 30;
    return client->heartbeat_interval_sec > 0 ? client->heartbeat_interval_sec : 30;
}

void dist_worker_set_auth_token(dist_worker_client_t *client, const char *token) {
    if (!client) return;

    if (token && token[0] != '\0') {
        strncpy(client->auth_token, token, DIST_AUTH_TOKEN_MAX - 1);
        client->auth_token[DIST_AUTH_TOKEN_MAX - 1] = '\0';
    } else {
        client->auth_token[0] = '\0';
    }
}

/* ============================================================================
 * Persistent State Functions
 * ============================================================================ */

/* State file format version - increment when format changes */
#define STATE_VERSION 1

void dist_coordinator_get_state_path(const dist_coordinator_t *coordinator,
                                     char *filepath, size_t filepath_size) {
    if (!coordinator || !filepath || filepath_size == 0) return;

    /* Generate path based on puzzle number and target */
    if (coordinator->job_puzzle_number > 0) {
        snprintf(filepath, filepath_size, "coordinator_state_puzzle%d.json",
                 coordinator->job_puzzle_number);
    } else {
        snprintf(filepath, filepath_size, "coordinator_state.json");
    }
}

int dist_coordinator_save_state(const dist_coordinator_t *coordinator,
                                const char *filepath) {
    if (!coordinator || !filepath) return -1;

    FILE *f = fopen(filepath, "w");
    if (!f) {
        printf(LOG_SERVER LOG_ERR "Failed to save state to %s: %s\n",
               filepath, strerror(errno));
        return -1;
    }

    /* Write header */
    fprintf(f, "{\n");
    fprintf(f, "  \"version\": %d,\n", STATE_VERSION);
    fprintf(f, "  \"save_time\": %llu,\n", (unsigned long long)time(NULL));

    /* Job configuration - for validation on load */
    fprintf(f, "  \"job_target_address\": \"%s\",\n", coordinator->job_target_address);
    fprintf(f, "  \"job_mode\": \"%s\",\n", coordinator->job_mode);
    fprintf(f, "  \"job_key_type\": \"%s\",\n", coordinator->job_key_type);
    fprintf(f, "  \"job_puzzle_number\": %d,\n", coordinator->job_puzzle_number);
    fprintf(f, "  \"job_bits\": %d,\n", coordinator->job_bits);

    /* Progress statistics */
    fprintf(f, "  \"total_keys\": %llu,\n", (unsigned long long)coordinator->total_keys);
    fprintf(f, "  \"keys_processed\": %llu,\n", (unsigned long long)coordinator->keys_processed);
    fprintf(f, "  \"work_unit_count\": %d,\n", coordinator->work_unit_count);
    fprintf(f, "  \"work_units_completed\": %d,\n", coordinator->work_units_completed);

    /* Authentication (if enabled) */
    if (coordinator->auth_enabled && coordinator->auth_token[0]) {
        fprintf(f, "  \"auth_token\": \"%s\",\n", coordinator->auth_token);
    }

    /* Work units array - only save PENDING and COMPLETED status */
    fprintf(f, "  \"work_units\": [\n");
    int first_unit = 1;
    for (int i = 0; i < coordinator->work_unit_count; i++) {
        const dist_work_unit_t *unit = &coordinator->work_units[i];

        /* Only save completed units (PENDING units will be regenerated) */
        if (unit->status == WORK_STATUS_COMPLETED) {
            if (!first_unit) fprintf(f, ",\n");
            first_unit = 0;

            fprintf(f, "    {\"id\": %d, \"range_start\": \"%s\", \"range_end\": \"%s\", "
                       "\"status\": %d, \"keys_in_unit\": %llu}",
                    unit->id, unit->range_start, unit->range_end,
                    (int)unit->status, (unsigned long long)unit->keys_in_unit);
        }
    }
    fprintf(f, "\n  ],\n");

    /* Results array */
    fprintf(f, "  \"results\": [\n");
    for (int i = 0; i < coordinator->result_count; i++) {
        const dist_result_t *result = &coordinator->results[i];
        if (i > 0) fprintf(f, ",\n");
        fprintf(f, "    {\"private_key\": \"%s\", \"address\": \"%s\", "
                   "\"worker_id\": %d, \"found_time\": %llu}",
                result->private_key, result->address,
                result->worker_id, (unsigned long long)result->found_time);
    }
    fprintf(f, "\n  ]\n");

    fprintf(f, "}\n");
    fclose(f);

    printf(LOG_SERVER LOG_OK "State saved to %s (%d/%d units completed)\n",
           filepath, coordinator->work_units_completed, coordinator->work_unit_count);
    return 0;
}

int dist_coordinator_load_state(dist_coordinator_t *coordinator,
                                const char *filepath) {
    if (!coordinator || !filepath) return -1;

    FILE *f = fopen(filepath, "r");
    if (!f) {
        if (errno == ENOENT) {
            return 1;  /* File not found - not an error, just no state to restore */
        }
        printf(LOG_SERVER LOG_ERR "Failed to open state file %s: %s\n",
               filepath, strerror(errno));
        return -1;
    }

    /* Read entire file */
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 100 * 1024 * 1024) {  /* Max 100MB */
        printf(LOG_SERVER LOG_ERR "Invalid state file size: %ld\n", fsize);
        fclose(f);
        return -1;
    }

    char *json = (char *)malloc((size_t)fsize + 1);
    if (!json) {
        fclose(f);
        return -1;
    }

    size_t read_bytes = fread(json, 1, (size_t)fsize, f);
    fclose(f);
    json[read_bytes] = '\0';

    /* Validate version */
    int version = (int)json_get_int(json, "version");
    if (version != STATE_VERSION) {
        printf(LOG_SERVER LOG_WARN "State file version mismatch (got %d, expected %d)\n",
               version, STATE_VERSION);
        free(json);
        return -1;
    }

    /* Load job configuration */
    char saved_target[64] = {0};
    char saved_mode[32] = {0};
    json_get_string(json, "job_target_address", saved_target, sizeof(saved_target));
    json_get_string(json, "job_mode", saved_mode, sizeof(saved_mode));
    (void)json_get_int(json, "job_puzzle_number");  /* Read for validation, value unused */

    /* Validate job matches current configuration */
    if (coordinator->job_target_address[0] && saved_target[0]) {
        if (strcmp(coordinator->job_target_address, saved_target) != 0) {
            printf(LOG_SERVER LOG_WARN "State file target mismatch - not loading\n");
            printf(LOG_SERVER LOG_INFO "  Current: %s\n", coordinator->job_target_address);
            printf(LOG_SERVER LOG_INFO "  Saved:   %s\n", saved_target);
            free(json);
            return -1;
        }
    }

    /* Load authentication token if present */
    char saved_auth[DIST_AUTH_TOKEN_MAX] = {0};
    if (json_get_string(json, "auth_token", saved_auth, sizeof(saved_auth)) == 0) {
        if (saved_auth[0]) {
            strncpy(coordinator->auth_token, saved_auth, DIST_AUTH_TOKEN_MAX - 1);
            coordinator->auth_enabled = true;
        }
    }

    /* Load progress statistics */
    coordinator->keys_processed = (uint64_t)json_get_int(json, "keys_processed");
    /* Note: work_units_completed is recalculated from work_units array */

    /* Parse completed work units to mark them as done */
    const char *units_start = strstr(json, "\"work_units\":");
    if (units_start) {
        units_start = strchr(units_start, '[');
        if (units_start) {
            units_start++;

            int units_restored = 0;

            /* Parse each unit object */
            const char *unit_ptr = units_start;
            while ((unit_ptr = strchr(unit_ptr, '{')) != NULL) {
                const char *unit_end = strchr(unit_ptr, '}');
                if (!unit_end) break;

                /* Extract unit ID */
                char unit_json[512];
                size_t unit_len = (size_t)(unit_end - unit_ptr + 1);
                if (unit_len >= sizeof(unit_json)) {
                    unit_ptr = unit_end + 1;
                    continue;
                }
                memcpy(unit_json, unit_ptr, unit_len);
                unit_json[unit_len] = '\0';

                int unit_id = (int)json_get_int(unit_json, "id");
                int unit_status = (int)json_get_int(unit_json, "status");

                /* Mark this unit as completed in current work units */
                if (unit_id >= 0 && unit_id < coordinator->work_unit_count) {
                    if (unit_status == WORK_STATUS_COMPLETED) {
                        coordinator->work_units[unit_id].status = WORK_STATUS_COMPLETED;
                        units_restored++;
                    }
                }

                unit_ptr = unit_end + 1;
            }

            /* Recalculate pending/completed counts */
            coordinator->work_units_completed = 0;
            coordinator->work_units_pending = 0;
            for (int i = 0; i < coordinator->work_unit_count; i++) {
                if (coordinator->work_units[i].status == WORK_STATUS_COMPLETED) {
                    coordinator->work_units_completed++;
                } else {
                    coordinator->work_units_pending++;
                }
            }

            printf(LOG_SERVER LOG_OK "Restored %d completed work units from state file\n",
                   units_restored);
        }
    }

    /* Parse results */
    const char *results_start = strstr(json, "\"results\":");
    if (results_start) {
        results_start = strchr(results_start, '[');
        if (results_start) {
            results_start++;

            const char *result_ptr = results_start;
            while ((result_ptr = strchr(result_ptr, '{')) != NULL) {
                const char *result_end = strchr(result_ptr, '}');
                if (!result_end) break;

                char result_json[512];
                size_t result_len = (size_t)(result_end - result_ptr + 1);
                if (result_len >= sizeof(result_json)) {
                    result_ptr = result_end + 1;
                    continue;
                }
                memcpy(result_json, result_ptr, result_len);
                result_json[result_len] = '\0';

                /* Add to results */
                if (coordinator->result_count < coordinator->result_capacity) {
                    dist_result_t *r = &coordinator->results[coordinator->result_count];
                    json_get_string(result_json, "private_key", r->private_key, sizeof(r->private_key));
                    json_get_string(result_json, "address", r->address, sizeof(r->address));
                    r->worker_id = (int)json_get_int(result_json, "worker_id");
                    r->found_time = (uint64_t)json_get_int(result_json, "found_time");

                    if (r->private_key[0]) {
                        coordinator->result_count++;
                        printf(LOG_SERVER LOG_FOUND "Restored found key: %s\n", r->private_key);
                    }
                }

                result_ptr = result_end + 1;
            }
        }
    }

    free(json);

    printf(LOG_SERVER LOG_OK "State loaded: %d/%d work units completed, %d results\n",
           coordinator->work_units_completed, coordinator->work_unit_count,
           coordinator->result_count);

    return 0;
}

/* ============================================================================
 * Multi-Coordinator Federation Implementation
 * ============================================================================ */

#define LOG_FEDERATION CLR_MAGENTA "[FEDERATION]" CLR_RESET

int dist_federation_init_primary(dist_coordinator_t *coordinator,
                                 int federation_port) {
    if (!coordinator) return -1;

    coordinator->federation.role = FEDERATION_PRIMARY;
    coordinator->federation.peer_count = 0;
    coordinator->federation.sync_interval_sec = 30;
    coordinator->federation.results_shared = false;
    memset(coordinator->federation.peers, 0, sizeof(coordinator->federation.peers));

    printf(LOG_FEDERATION LOG_OK "Initialized as PRIMARY coordinator\n");
    printf(LOG_FEDERATION LOG_INFO "Federation port: %d (same as worker port)\n",
           federation_port > 0 ? federation_port : coordinator->port);

    return 0;
}

int dist_federation_init_secondary(dist_coordinator_t *coordinator,
                                   const char *primary_host, int primary_port) {
    if (!coordinator || !primary_host) return -1;

    coordinator->federation.role = FEDERATION_SECONDARY;
    strncpy(coordinator->federation.primary_host, primary_host,
            sizeof(coordinator->federation.primary_host) - 1);
    coordinator->federation.primary_port = primary_port > 0 ? primary_port : DIST_DEFAULT_PORT;
    coordinator->federation.peer_count = 0;
    coordinator->federation.sync_interval_sec = 30;
    coordinator->federation.results_shared = false;

    printf(LOG_FEDERATION LOG_OK "Initialized as SECONDARY coordinator\n");
    printf(LOG_FEDERATION LOG_INFO "Primary: %s:%d\n",
           coordinator->federation.primary_host,
           coordinator->federation.primary_port);

    return 0;
}

int dist_federation_add_peer(dist_coordinator_t *coordinator,
                             const char *host, int port) {
    if (!coordinator || !host) return -1;

    if (coordinator->federation.role != FEDERATION_PRIMARY) {
        printf(LOG_FEDERATION LOG_ERR "Only primary can add peers\n");
        return -1;
    }

    if (coordinator->federation.peer_count >= DIST_MAX_FEDERATION) {
        printf(LOG_FEDERATION LOG_ERR "Maximum federation peers reached (%d)\n",
               DIST_MAX_FEDERATION);
        return -1;
    }

    int idx = coordinator->federation.peer_count;
    dist_federation_peer_t *peer = &coordinator->federation.peers[idx];

    strncpy(peer->host, host, sizeof(peer->host) - 1);
    peer->port = port > 0 ? port : DIST_DEFAULT_PORT;
    peer->socket_fd = -1;
    peer->connected = false;
    peer->is_primary = false;
    peer->last_heartbeat = 0;
    peer->work_units_assigned = 0;
    peer->work_units_completed = 0;
    peer->keys_processed = 0;

    coordinator->federation.peer_count++;

    printf(LOG_FEDERATION LOG_OK "Added peer: %s:%d (index %d)\n",
           peer->host, peer->port, idx);

    return idx;
}

/* Internal: Connect to a federation peer */
static int federation_connect_peer(dist_federation_peer_t *peer) {
    if (!peer || peer->connected) return 0;

    struct hostent *server = gethostbyname(peer->host);
    if (!server) {
        printf(LOG_FEDERATION LOG_ERR "Cannot resolve %s\n", peer->host);
        return -1;
    }

    peer->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (peer->socket_fd < 0) {
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr.s_addr, server->h_addr, (size_t)server->h_length);
    addr.sin_port = htons((uint16_t)peer->port);

    /* Set connection timeout */
    set_socket_timeout(peer->socket_fd, 10);

    if (connect(peer->socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(peer->socket_fd);
        peer->socket_fd = -1;
        return -1;
    }

    peer->connected = true;
    peer->last_heartbeat = time_ms();

    printf(LOG_FEDERATION LOG_OK "Connected to peer %s:%d\n", peer->host, peer->port);
    return 0;
}

int dist_federation_connect(dist_coordinator_t *coordinator) {
    if (!coordinator) return -1;

    if (coordinator->federation.role != FEDERATION_SECONDARY) {
        printf(LOG_FEDERATION LOG_ERR "Only secondary can connect to primary\n");
        return -1;
    }

    /* Create a peer entry for the primary */
    if (coordinator->federation.peer_count == 0) {
        dist_federation_peer_t *primary = &coordinator->federation.peers[0];
        strncpy(primary->host, coordinator->federation.primary_host, sizeof(primary->host) - 1);
        primary->port = coordinator->federation.primary_port;
        primary->is_primary = true;
        primary->socket_fd = -1;
        primary->connected = false;
        coordinator->federation.peer_count = 1;
    }

    dist_federation_peer_t *primary = &coordinator->federation.peers[0];

    if (federation_connect_peer(primary) != 0) {
        printf(LOG_FEDERATION LOG_ERR "Failed to connect to primary %s:%d\n",
               primary->host, primary->port);
        return -1;
    }

    /* Send registration message */
    char msg[DIST_MAX_MSG_SIZE];
    snprintf(msg, sizeof(msg),
             "{\"type\":\"federation_register\","
             "\"role\":\"secondary\","
             "\"port\":%d,"
             "\"auth_token\":\"%s\"}",
             coordinator->port,
             coordinator->auth_token);

    if (send_msg(primary->socket_fd, msg) != 0) {
        printf(LOG_FEDERATION LOG_ERR "Failed to send registration\n");
        close(primary->socket_fd);
        primary->socket_fd = -1;
        primary->connected = false;
        return -1;
    }

    /* Wait for response with work range assignment */
    char response[DIST_MAX_MSG_SIZE];
    if (recv_msg(primary->socket_fd, response, sizeof(response)) <= 0) {
        printf(LOG_FEDERATION LOG_ERR "No response from primary\n");
        close(primary->socket_fd);
        primary->socket_fd = -1;
        primary->connected = false;
        return -1;
    }

    /* Parse assigned range */
    char resp_type[32] = {0};
    json_get_string(response, "type", resp_type, sizeof(resp_type));

    if (strcmp(resp_type, "federation_welcome") == 0) {
        json_get_string(response, "range_start",
                       coordinator->federation.assigned_range_start,
                       sizeof(coordinator->federation.assigned_range_start));
        json_get_string(response, "range_end",
                       coordinator->federation.assigned_range_end,
                       sizeof(coordinator->federation.assigned_range_end));

        printf(LOG_FEDERATION LOG_OK "Received range assignment:\n");
        printf(LOG_FEDERATION LOG_INFO "  Start: %s\n",
               coordinator->federation.assigned_range_start);
        printf(LOG_FEDERATION LOG_INFO "  End:   %s\n",
               coordinator->federation.assigned_range_end);

        return 0;
    } else if (strcmp(resp_type, "federation_rejected") == 0) {
        char reason[256] = {0};
        json_get_string(response, "reason", reason, sizeof(reason));
        printf(LOG_FEDERATION LOG_ERR "Registration rejected: %s\n", reason);
        close(primary->socket_fd);
        primary->socket_fd = -1;
        primary->connected = false;
        return -1;
    }

    return -1;
}

int dist_federation_process(dist_coordinator_t *coordinator, int timeout_ms) {
    if (!coordinator) return -1;

    if (coordinator->federation.role == FEDERATION_STANDALONE) {
        return 0;  /* Nothing to do in standalone mode */
    }

    uint64_t now = time_ms();

    /* Check peer heartbeats and reconnect if needed */
    for (int i = 0; i < coordinator->federation.peer_count; i++) {
        dist_federation_peer_t *peer = &coordinator->federation.peers[i];

        if (!peer->connected) {
            /* Try to reconnect */
            if (now - peer->last_heartbeat > 30000) {  /* 30s between reconnect attempts */
                federation_connect_peer(peer);
            }
            continue;
        }

        /* Check for timeout */
        if (now - peer->last_heartbeat > 60000) {  /* 60s timeout */
            printf(LOG_FEDERATION LOG_WARN "Peer %s:%d timed out\n",
                   peer->host, peer->port);
            close(peer->socket_fd);
            peer->socket_fd = -1;
            peer->connected = false;
        }
    }

    /* Send periodic sync (every sync_interval) */
    if (now - coordinator->federation.last_sync_time >
        (uint64_t)coordinator->federation.sync_interval_sec * 1000) {

        coordinator->federation.last_sync_time = now;

        /* Send progress update to all peers */
        char msg[DIST_MAX_MSG_SIZE];
        snprintf(msg, sizeof(msg),
                 "{\"type\":\"federation_sync\","
                 "\"work_completed\":%d,"
                 "\"keys_processed\":%llu,"
                 "\"result_count\":%d}",
                 coordinator->work_units_completed,
                 (unsigned long long)coordinator->keys_processed,
                 coordinator->result_count);

        for (int i = 0; i < coordinator->federation.peer_count; i++) {
            dist_federation_peer_t *peer = &coordinator->federation.peers[i];
            if (peer->connected && peer->socket_fd >= 0) {
                send_msg(peer->socket_fd, msg);
            }
        }
    }

    return 0;
}

int dist_federation_share_result(dist_coordinator_t *coordinator,
                                 const char *private_key, const char *address) {
    if (!coordinator || !private_key || !address) return -1;

    if (coordinator->federation.role == FEDERATION_STANDALONE) {
        return 0;  /* Nothing to share in standalone mode */
    }

    char msg[DIST_MAX_MSG_SIZE];
    snprintf(msg, sizeof(msg),
             "{\"type\":\"federation_found\","
             "\"private_key\":\"%s\","
             "\"address\":\"%s\"}",
             private_key, address);

    int shared = 0;
    for (int i = 0; i < coordinator->federation.peer_count; i++) {
        dist_federation_peer_t *peer = &coordinator->federation.peers[i];
        if (peer->connected && peer->socket_fd >= 0) {
            if (send_msg(peer->socket_fd, msg) == 0) {
                shared++;
                printf(LOG_FEDERATION LOG_OK "Shared result with %s:%d\n",
                       peer->host, peer->port);
            }
        }
    }

    coordinator->federation.results_shared = (shared > 0);
    return shared > 0 ? 0 : -1;
}

void dist_federation_stats(const dist_coordinator_t *coordinator,
                           int *total_peers, int *total_units, int *total_completed) {
    if (!coordinator) return;

    int peers = 0;
    int units = coordinator->work_unit_count;
    int completed = coordinator->work_units_completed;

    for (int i = 0; i < coordinator->federation.peer_count; i++) {
        if (coordinator->federation.peers[i].connected) {
            peers++;
            completed += coordinator->federation.peers[i].work_units_completed;
        }
    }

    if (total_peers) *total_peers = peers;
    if (total_units) *total_units = units;
    if (total_completed) *total_completed = completed;
}

void dist_federation_shutdown(dist_coordinator_t *coordinator) {
    if (!coordinator) return;

    printf(LOG_FEDERATION LOG_INFO "Shutting down federation connections...\n");

    for (int i = 0; i < coordinator->federation.peer_count; i++) {
        dist_federation_peer_t *peer = &coordinator->federation.peers[i];
        if (peer->socket_fd >= 0) {
            /* Send goodbye message */
            send_msg(peer->socket_fd, "{\"type\":\"federation_goodbye\"}");
            close(peer->socket_fd);
            peer->socket_fd = -1;
            peer->connected = false;
        }
    }

    coordinator->federation.peer_count = 0;
    coordinator->federation.role = FEDERATION_STANDALONE;
}
