/*
 * distributed.h - Distributed Mode for Keyhunt
 *
 * Enables cluster computing with coordinator/worker architecture.
 * Coordinator distributes work ranges to workers and collects results.
 *
 * Architecture:
 *   ┌─────────────────┐
 *   │   Coordinator   │  (port 7777)
 *   └────────┬────────┘
 *       ┌────┴────┬─────────┐
 *   ┌───▼───┐ ┌───▼───┐ ┌───▼───┐
 *   │Worker1│ │Worker2│ │Worker3│
 *   └───────┘ └───────┘ └───────┘
 *
 * Protocol: TCP + JSON (simple, readable, debuggable)
 */

#ifndef DISTRIBUTED_H
#define DISTRIBUTED_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Default port for coordinator */
#define DIST_DEFAULT_PORT 7777

/* Maximum workers */
#define DIST_MAX_WORKERS 64

/* Maximum message size */
#define DIST_MAX_MSG_SIZE 8192

/* Work unit status */
typedef enum {
    WORK_STATUS_PENDING = 0,
    WORK_STATUS_ASSIGNED,
    WORK_STATUS_COMPLETED,
    WORK_STATUS_FAILED
} work_status_t;

/* Worker information */
typedef struct {
    int id;
    char hostname[64];
    double perf_score;          /* Performance score from sysinfo */
    uint64_t keys_processed;
    double throughput;          /* Mkeys/s */
    int socket_fd;
    bool connected;
    uint64_t last_heartbeat;
    int current_work_id;
} dist_worker_t;

/* Work unit */
typedef struct {
    int id;
    char range_start[65];       /* Hex string */
    char range_end[65];         /* Hex string */
    int assigned_worker;
    work_status_t status;
    uint64_t keys_in_unit;
    uint64_t assigned_time;
    uint64_t completed_time;
} dist_work_unit_t;

/* Found key result */
typedef struct {
    char private_key[65];       /* Hex string */
    char address[36];           /* Base58 address */
    int worker_id;
    uint64_t found_time;
} dist_result_t;

/* Coordinator state */
typedef struct {
    int listen_socket;
    int port;

    dist_worker_t workers[DIST_MAX_WORKERS];
    int worker_count;

    dist_work_unit_t *work_units;
    int work_unit_count;
    int work_units_completed;
    int work_units_pending;

    dist_result_t *results;
    int result_count;
    int result_capacity;

    uint64_t total_keys;
    uint64_t keys_processed;
    double total_throughput;

    bool running;
    bool all_work_done;

    char output_file[256];
} dist_coordinator_t;

/* Worker client state */
typedef struct {
    int socket_fd;
    char coordinator_host[256];
    int coordinator_port;
    char worker_id[64];
    double perf_score;

    bool connected;
    bool has_work;
    char current_range_start[65];
    char current_range_end[65];
    int current_work_id;

    uint64_t keys_processed;
} dist_worker_client_t;

/* ============================================================================
 * Coordinator Functions
 * ============================================================================ */

/**
 * Initialize coordinator
 * @param coordinator Output coordinator state
 * @param port Port to listen on (0 = default)
 * @return 0 on success, -1 on error
 */
int dist_coordinator_init(dist_coordinator_t *coordinator, int port);

/**
 * Set work range for distribution
 * @param coordinator Coordinator state
 * @param range_start Hex string of start
 * @param range_end Hex string of end
 * @param work_unit_size Keys per work unit
 * @return Number of work units created, or -1 on error
 */
int dist_coordinator_set_range(dist_coordinator_t *coordinator,
                               const char *range_start, const char *range_end,
                               uint64_t work_unit_size);

/**
 * Start coordinator (begins accepting workers)
 * @param coordinator Coordinator state
 * @return 0 on success, -1 on error
 */
int dist_coordinator_start(dist_coordinator_t *coordinator);

/**
 * Process coordinator events (non-blocking)
 * Call this in a loop to handle worker connections and messages
 * @param coordinator Coordinator state
 * @param timeout_ms Timeout for select (0 = non-blocking)
 * @return 0 on normal, 1 if all work done, -1 on error
 */
int dist_coordinator_process(dist_coordinator_t *coordinator, int timeout_ms);

/**
 * Get coordinator statistics
 * @param coordinator Coordinator state
 * @param workers_active Output: number of active workers
 * @param work_pending Output: work units pending
 * @param work_completed Output: work units completed
 * @param throughput Output: total throughput Mkeys/s
 */
void dist_coordinator_stats(const dist_coordinator_t *coordinator,
                            int *workers_active, int *work_pending,
                            int *work_completed, double *throughput);

/**
 * Shutdown coordinator
 * @param coordinator Coordinator state
 */
void dist_coordinator_shutdown(dist_coordinator_t *coordinator);

/* ============================================================================
 * Worker Client Functions
 * ============================================================================ */

/**
 * Initialize worker client
 * @param client Output client state
 * @param coordinator_host Coordinator hostname/IP
 * @param coordinator_port Coordinator port (0 = default)
 * @param perf_score Performance score (from sysinfo)
 * @return 0 on success, -1 on error
 */
int dist_worker_init(dist_worker_client_t *client,
                     const char *coordinator_host, int coordinator_port,
                     double perf_score);

/**
 * Connect to coordinator
 * @param client Client state
 * @return 0 on success, -1 on error
 */
int dist_worker_connect(dist_worker_client_t *client);

/**
 * Request work from coordinator
 * @param client Client state
 * @param range_start Output: start of assigned range (hex)
 * @param range_end Output: end of assigned range (hex)
 * @return 0 if work assigned, 1 if no more work, -1 on error
 */
int dist_worker_request_work(dist_worker_client_t *client,
                             char *range_start, char *range_end);

/**
 * Report work completion to coordinator
 * @param client Client state
 * @param keys_processed Keys processed in this unit
 * @param elapsed_ms Time taken in milliseconds
 * @return 0 on success, -1 on error
 */
int dist_worker_report_done(dist_worker_client_t *client,
                            uint64_t keys_processed, uint64_t elapsed_ms);

/**
 * Report found key to coordinator
 * @param client Client state
 * @param private_key Hex string of private key
 * @param address Base58 address found
 * @return 0 on success, -1 on error
 */
int dist_worker_report_found(dist_worker_client_t *client,
                             const char *private_key, const char *address);

/**
 * Send heartbeat to coordinator
 * @param client Client state
 * @param keys_since_last Keys processed since last heartbeat
 * @return 0 on success, -1 on error
 */
int dist_worker_heartbeat(dist_worker_client_t *client, uint64_t keys_since_last);

/**
 * Disconnect from coordinator
 * @param client Client state
 */
void dist_worker_disconnect(dist_worker_client_t *client);

#ifdef __cplusplus
}
#endif

#endif /* DISTRIBUTED_H */
