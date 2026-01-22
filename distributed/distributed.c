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

/* Simple JSON helpers (minimal, no external deps) */
static void json_add_string(char *buf, size_t sz, const char *key, const char *val) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "\"%s\":\"%s\",", key, val);
    strncat(buf, tmp, sz - strlen(buf) - 1);
}

static void json_add_int(char *buf, size_t sz, const char *key, int64_t val) {
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "\"%s\":%lld,", key, (long long)val);
    strncat(buf, tmp, sz - strlen(buf) - 1);
}

static void json_add_double(char *buf, size_t sz, const char *key, double val) {
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "\"%s\":%.3f,", key, val);
    strncat(buf, tmp, sz - strlen(buf) - 1);
}

static int json_get_string(const char *json, const char *key, char *out, size_t outsz) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char *start = strstr(json, pattern);
    if (!start) return -1;
    start += strlen(pattern);
    const char *end = strchr(start, '"');
    if (!end) return -1;
    size_t len = (size_t)(end - start);
    if (len >= outsz) len = outsz - 1;
    strncpy(out, start, len);
    out[len] = '\0';
    return 0;
}

static int64_t json_get_int(const char *json, const char *key) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *start = strstr(json, pattern);
    if (!start) return 0;
    start += strlen(pattern);
    return strtoll(start, NULL, 10);
}

static double json_get_double(const char *json, const char *key) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *start = strstr(json, pattern);
    if (!start) return 0.0;
    start += strlen(pattern);
    return strtod(start, NULL);
}

static uint64_t time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

/* Set socket to non-blocking */
static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* Send message with length prefix */
static int send_msg(int fd, const char *msg) {
    uint32_t len = (uint32_t)strlen(msg);
    uint32_t net_len = htonl(len);

    if (send(fd, &net_len, 4, 0) != 4) return -1;
    if (send(fd, msg, len, 0) != (ssize_t)len) return -1;
    return 0;
}

/* Receive message with length prefix */
static int recv_msg(int fd, char *buf, size_t bufsz) {
    uint32_t net_len;
    ssize_t n = recv(fd, &net_len, 4, MSG_WAITALL);
    if (n != 4) return -1;

    uint32_t len = ntohl(net_len);
    if (len >= bufsz) return -1;

    n = recv(fd, buf, len, MSG_WAITALL);
    if (n != (ssize_t)len) return -1;

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

    /* Allocate results array */
    coord->result_capacity = 1024;
    coord->results = calloc(coord->result_capacity, sizeof(dist_result_t));
    if (!coord->results) return -1;

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
        printf("[Coordinator] Error: end <= start (start=0x%s, end=0x%s)\n",
               range_start, range_end);
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
        printf("[Coordinator] Warning: Range too large, limiting to %d work units\n", num_units);
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

    printf("[Coordinator] Created %d work units for range 0x%s - 0x%s\n",
           coord->work_unit_count, range_start, range_end);
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

    printf("[Coordinator] Listening on port %d\n", coord->port);
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
        dist_work_unit_t *unit = find_pending_work(coord);
        char response[DIST_MAX_MSG_SIZE];

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

        send_msg(worker->socket_fd, response);

    } else if (strcmp(type, "work_done") == 0) {
        int work_id = (int)json_get_int(msg, "work_id");
        uint64_t keys = (uint64_t)json_get_int(msg, "keys_processed");
        uint64_t elapsed = (uint64_t)json_get_int(msg, "elapsed_ms");

        if (work_id >= 0 && work_id < coord->work_unit_count) {
            dist_work_unit_t *unit = &coord->work_units[work_id];
            unit->status = WORK_STATUS_COMPLETED;
            unit->completed_time = time_ms();
            coord->work_units_completed++;
        }

        worker->keys_processed += keys;
        coord->keys_processed += keys;

        if (elapsed > 0) {
            worker->throughput = (double)keys / (double)elapsed * 1000.0 / 1000000.0;
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

            printf("[Coordinator] FOUND by worker %d: %s -> %s\n",
                   worker->id, privkey, address);
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

            /* Read registration message */
            char msg[DIST_MAX_MSG_SIZE];
            if (recv_msg(client_fd, msg, sizeof(msg)) > 0) {
                char type[32] = {0};
                json_get_string(msg, "type", type, sizeof(type));

                if (strcmp(type, "register") == 0) {
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

                    printf("[Coordinator] Worker %d connected from %s (score=%.1f)\n",
                           worker->id, worker->hostname, worker->perf_score);
                    if (worker->cpu_name[0] || worker->gpu_name[0]) {
                        printf("[Coordinator]   Hardware: %s (%d threads)",
                               worker->cpu_name[0] ? worker->cpu_name : "unknown CPU",
                               worker->cpu_threads);
                        if (worker->gpu_name[0]) {
                            printf(", GPU: %s (%d MB)", worker->gpu_name, worker->gpu_memory_mb);
                        }
                        printf("\n");
                    }
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
                printf("[Coordinator] Worker %d disconnected\n", worker->id);
                close(worker->socket_fd);
                worker->socket_fd = -1;
                worker->connected = false;

                /* Reassign work if any */
                if (worker->current_work_id >= 0 &&
                    worker->current_work_id < coord->work_unit_count) {
                    dist_work_unit_t *unit = &coord->work_units[worker->current_work_id];
                    if (unit->status == WORK_STATUS_ASSIGNED) {
                        unit->status = WORK_STATUS_PENDING;
                        coord->work_units_pending++;
                    }
                }
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

    printf("[Coordinator] Shutdown complete. %d results found.\n", coord->result_count);
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
    printf("[Worker] Connected to coordinator at %s:%d\n",
           client->coordinator_host, client->coordinator_port);

    if (client->received_target_address[0]) {
        printf("[Worker] Received job config: puzzle #%d (%d bits), mode=%s\n",
               client->received_puzzle_number, client->received_bits, client->received_mode);
        printf("[Worker] Target: %.40s%s\n", client->received_target_address,
               strlen(client->received_target_address) > 40 ? "..." : "");
        printf("[Worker] Heartbeat interval: %d seconds\n", client->heartbeat_interval_sec);
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
    printf("[Worker] Disconnected from coordinator\n");
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

    printf("[Coordinator] Job config: puzzle #%d (%d bits), mode=%s, key_type=%s\n",
           puzzle_number, bits, mode ? mode : "?", key_type ? key_type : "?");
    printf("[Coordinator] Target: %.40s%s\n", target_address ? target_address : "?",
           target_address && strlen(target_address) > 40 ? "..." : "");
}

void dist_coordinator_set_heartbeat_interval(dist_coordinator_t *coordinator,
                                             int interval_sec) {
    if (!coordinator) return;
    coordinator->heartbeat_interval_sec = interval_sec > 0 ? interval_sec : 30;
    printf("[Coordinator] Heartbeat interval: %d seconds\n",
           coordinator->heartbeat_interval_sec);
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
