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
#include "platform/platform.h"

/* ============================================================================
 * TLS Support (Optional - requires OpenSSL)
 * ============================================================================
 *
 * This header conditionally includes OpenSSL headers when HAVE_OPENSSL is
 * defined via compiler flags (-DHAVE_OPENSSL). All OpenSSL-dependent types
 * (SSL_CTX, SSL) are only available when this macro is defined.
 *
 * When HAVE_OPENSSL is defined:
 *   - Full TLS encryption support
 *   - SSL_CTX and SSL types available in structs
 *   - TLS functions are fully functional
 *
 * When HAVE_OPENSSL is NOT defined:
 *   - No TLS support (stubs return errors)
 *   - SSL_CTX and SSL fields are excluded from structs
 *   - Calling TLS functions returns error codes
 *
 * Build commands:
 *   make ENABLE_TLS=1    # With TLS support (requires libssl-dev)
 *   make                 # Without TLS (default)
 * ============================================================================ */
#ifdef HAVE_OPENSSL
/* OpenSSL headers - only included when HAVE_OPENSSL is defined */
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/crypto.h>
#endif /* HAVE_OPENSSL */

#ifdef __cplusplus
extern "C" {
#endif

/* Default port for coordinator */
#define DIST_DEFAULT_PORT 7777

/* Maximum workers */
#define DIST_MAX_WORKERS 64

/* Maximum federated coordinators */
#define DIST_MAX_FEDERATION 8

/* Maximum message size */
#define DIST_MAX_MSG_SIZE 8192

/* Authentication token max length */
#define DIST_AUTH_TOKEN_MAX 64

/* Rate limiting defaults */
#define DIST_RATE_LIMIT_WINDOW_SEC 60      /* Rate limit window in seconds */
#define DIST_RATE_LIMIT_MAX_CONNECTIONS 10 /* Max connections per IP per window */
#define DIST_RATE_LIMIT_MAX_MESSAGES 100   /* Max messages per connection per window */

/* Work unit status */
typedef enum {
    WORK_STATUS_PENDING = 0,
    WORK_STATUS_ASSIGNED,
    WORK_STATUS_COMPLETED,
    WORK_STATUS_FAILED
} work_status_t;

/* Worker status (lifecycle states) */
typedef enum {
    WORKER_STATUS_JOINING = 0,       /* Worker is connecting/registering */
    WORKER_STATUS_ACTIVE,            /* Worker is active and can accept work */
    WORKER_STATUS_LEAVING,           /* Worker requested graceful departure */
    WORKER_STATUS_DISCONNECTED       /* Worker disconnected or timed out */
} worker_status_t;

/* Worker information */
typedef struct {
    int id;
    char hostname[64];
    double perf_score;          /* Performance score from sysinfo */

    /* Detailed hardware info */
    int cpu_cores;              /* Physical CPU cores */
    int cpu_threads;            /* Logical threads */
    char cpu_name[64];          /* CPU model name */
    char gpu_name[64];          /* GPU model name (empty if none) */
    int gpu_memory_mb;          /* GPU VRAM in MB */
    double cpu_speed_mkeys;     /* CPU-only speed Mkeys/s */
    double gpu_speed_mkeys;     /* GPU-only speed Mkeys/s */

    uint64_t keys_processed;
    double throughput;          /* Mkeys/s */
    int socket_fd;
    bool connected;
    uint64_t last_heartbeat;
    int current_work_id;
#ifdef HAVE_OPENSSL
    SSL *ssl;                   /* SSL connection for this worker (NULL if TLS disabled) */
#else
    void *ssl;                  /* Placeholder when OpenSSL not available */
#endif

    /* Per-worker threading for non-blocking client handling */
    platform_thread_t handler_thread;   /* Dedicated thread for this worker */
    bool handler_running;       /* Is the handler thread running? */
    void *coordinator;          /* Back-reference to coordinator (cast to dist_coordinator_t*) */

    /* Timeout configuration */
    int heartbeat_timeout_sec;  /* Timeout before marking worker as dead (0 = use coordinator default) */

    /* Worker status and lifecycle */
    worker_status_t status;     /* Current lifecycle status */
    bool leave_requested;       /* Worker requested graceful departure */
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

/* ============================================================================
 * Federation Types (defined here for use in coordinator struct)
 * ============================================================================ */

/* Federation role */
typedef enum {
    FEDERATION_STANDALONE = 0,   /* Not federated (default) */
    FEDERATION_PRIMARY,          /* Primary coordinator (distributes work to secondaries) */
    FEDERATION_SECONDARY         /* Secondary coordinator (receives work ranges from primary) */
} federation_role_t;

/* Federation peer info */
typedef struct {
    char host[256];
    int port;
    int socket_fd;
    bool connected;
    bool is_primary;
    uint64_t last_heartbeat;
    int work_units_assigned;      /* Work units assigned to this peer */
    int work_units_completed;     /* Work units completed by this peer */
    uint64_t keys_processed;
} dist_federation_peer_t;

/* Federation state */
typedef struct {
    federation_role_t role;
    char primary_host[256];
    int primary_port;

    dist_federation_peer_t peers[DIST_MAX_FEDERATION];
    int peer_count;

    /* Range partitioning for this coordinator */
    char assigned_range_start[68];
    char assigned_range_end[68];

    /* Sync state */
    uint64_t last_sync_time;
    int sync_interval_sec;

    /* Result sharing */
    bool results_shared;
} dist_federation_state_t;

/* Rate limiting entry for tracking connection attempts */
typedef struct {
    uint32_t ip_addr;               /* IPv4 address */
    uint64_t window_start;          /* Start of current rate limit window */
    int connection_count;           /* Connections in current window */
    int message_count;              /* Messages in current window */
} rate_limit_entry_t;

/* Rate limiter state */
typedef struct {
    rate_limit_entry_t *entries;    /* Array of rate limit entries */
    int entry_count;                /* Number of entries */
    int entry_capacity;             /* Capacity of entries array */
    platform_mutex_t mutex;          /* Protects entries */
    int max_connections_per_window; /* Max connections per IP per window */
    int max_messages_per_window;    /* Max messages per connection per window */
    int window_sec;                 /* Window duration in seconds */
    bool enabled;                   /* Whether rate limiting is enabled */
} rate_limiter_t;

/* Coordinator state */
typedef struct {
    int listen_socket;
    int port;
    char bind_address[64];          /* Specific interface to bind to (empty = all) */

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

    /* Thread synchronization - fine-grained locking for scalability */
    platform_mutex_t work_mutex;     /* Protects work unit assignment - hold briefly! */
    platform_mutex_t stats_mutex;    /* Protects statistics counters - separate from work */
    platform_mutex_t worker_mutex;   /* Protects worker array modifications */
    platform_mutex_t result_mutex;   /* Protects results array and result_count */

    /* Work distribution optimization */
    int next_pending_hint;          /* Hint for next pending work unit (optimization) */

    /* Authentication */
    bool auth_enabled;                          /* Whether authentication is required */
    char auth_token[DIST_AUTH_TOKEN_MAX];       /* Expected token from workers */

    /* Rate limiting */
    rate_limiter_t rate_limiter;                /* Rate limiter state */

    /* TLS support (optional) */
    bool tls_enabled;                           /* Whether TLS is enabled */
    char tls_cert_file[256];                    /* Path to TLS certificate */
    char tls_key_file[256];                     /* Path to TLS private key */
#ifdef HAVE_OPENSSL
    SSL_CTX *ssl_ctx;                           /* OpenSSL context for server */
#else
    void *ssl_ctx;                              /* Placeholder when OpenSSL not available */
#endif

    char output_file[256];

    /* Federation state */
    dist_federation_state_t federation;

    /* Job configuration (sent to workers) */
    char job_target_address[64];    /* Target address/hash */
    char job_mode[32];              /* "address", "bsgs", "xpoint" */
    char job_key_type[16];          /* "compress", "uncompress", "both" */
    int job_puzzle_number;          /* Puzzle number (informational) */
    int job_bits;                   /* Bit range (informational) */
    int heartbeat_interval_sec;     /* How often workers should heartbeat */

    /* Health check timing */
    uint64_t last_health_check_ms;  /* Last health check timestamp (moved from static) */

    /* Timeout configuration */
    int worker_timeout_sec;         /* Timeout before marking worker as dead (default: 3x heartbeat) */
    int work_timeout_sec;           /* Timeout before reassigning work from unresponsive worker (default: 5x heartbeat) */
    int connection_timeout_sec;     /* TCP connection accept timeout (default: 30) */
} dist_coordinator_t;

/* Worker client state */
typedef struct {
    int socket_fd;
    char coordinator_host[256];
    int coordinator_port;
    char worker_id[64];
    double perf_score;

    /* Detailed hardware info to report */
    int cpu_cores;
    int cpu_threads;
    char cpu_name[64];
    char gpu_name[64];
    int gpu_memory_mb;
    double cpu_speed_mkeys;
    double gpu_speed_mkeys;

    bool connected;
    bool has_work;
    char current_range_start[65];
    char current_range_end[65];
    int current_work_id;

    uint64_t keys_processed;

    /* Local progress (reported at registration for resume) */
    int local_completed_count;      /* Number of locally completed ranges */

    /* Server-configured settings */
    int heartbeat_interval_sec;

    /* Authentication */
    char auth_token[DIST_AUTH_TOKEN_MAX];       /* Token to send to coordinator */

    /* Received job config from server */
    char received_target_address[64];
    char received_mode[32];
    char received_key_type[16];
    int received_puzzle_number;
    int received_bits;

    /* TLS support (optional) */
    bool tls_enabled;                           /* Whether TLS is enabled */
#ifdef HAVE_OPENSSL
    SSL_CTX *ssl_ctx;                           /* OpenSSL context for client */
    SSL *ssl;                                   /* SSL connection */
#else
    void *ssl_ctx;                              /* Placeholder when OpenSSL not available */
    void *ssl;                                  /* Placeholder when OpenSSL not available */
#endif

    /* Timeout configuration */
    int connect_timeout_sec;                    /* Timeout for connecting to coordinator (default: 30) */
    int response_timeout_sec;                   /* Timeout waiting for coordinator response (default: 60) */
    int reconnect_delay_sec;                    /* Delay before reconnecting on connection loss (default: 5) */
} dist_worker_client_t;

/* ============================================================================
 * Coordinator Functions
 * ============================================================================ */

/**
 * Check if a port is available for binding (early check before setup)
 * Use this to fail fast before loading state, creating work units, etc.
 * @param port Port to check (0 = default 7777)
 * @param bind_address Specific interface to bind (NULL = all interfaces)
 * @return 0 if available, 1 if port in use, -1 on error, -2 if invalid address
 */
int dist_coordinator_check_port(int port, const char *bind_address);

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
 * Set job configuration for distribution to workers
 * @param coordinator Coordinator state
 * @param target_address Target address/hash to search for
 * @param mode Search mode ("address", "bsgs", "xpoint")
 * @param key_type Key type ("compress", "uncompress", "both")
 * @param puzzle_number Puzzle number (informational)
 * @param bits Bit range (informational)
 */
void dist_coordinator_set_job_config(dist_coordinator_t *coordinator,
                                     const char *target_address,
                                     const char *mode,
                                     const char *key_type,
                                     int puzzle_number,
                                     int bits);

/**
 * Set heartbeat interval for workers
 * @param coordinator Coordinator state
 * @param interval_sec Seconds between heartbeats (0 = use default 30)
 */
void dist_coordinator_set_heartbeat_interval(dist_coordinator_t *coordinator,
                                             int interval_sec);

/**
 * Enable authentication and set the expected token
 * Workers must provide this token to connect
 * @param coordinator Coordinator state
 * @param token Authentication token (NULL or empty to disable)
 */
void dist_coordinator_set_auth_token(dist_coordinator_t *coordinator,
                                     const char *token);

/**
 * Set bind address for coordinator
 * By default, coordinator binds to all interfaces (0.0.0.0).
 * Use this to bind to a specific interface for security.
 * @param coordinator Coordinator state
 * @param address IP address to bind to (e.g., "127.0.0.1", "192.168.1.100")
 *                NULL or empty string = bind to all interfaces
 */
void dist_coordinator_set_bind_address(dist_coordinator_t *coordinator,
                                       const char *address);

/**
 * Set worker timeout (time before marking worker as dead)
 * @param coordinator Coordinator state
 * @param timeout_sec Timeout in seconds (0 = use default: 3x heartbeat interval)
 */
void dist_coordinator_set_worker_timeout(dist_coordinator_t *coordinator,
                                         int timeout_sec);

/**
 * Set work redistribution timeout (time before reassigning stalled work)
 * @param coordinator Coordinator state
 * @param timeout_sec Timeout in seconds (0 = use default: 5x heartbeat interval)
 */
void dist_coordinator_set_work_timeout(dist_coordinator_t *coordinator,
                                       int timeout_sec);

/**
 * Set connection timeout for accepting new clients
 * @param coordinator Coordinator state
 * @param timeout_sec Timeout in seconds (0 = use default: 30)
 */
void dist_coordinator_set_connection_timeout(dist_coordinator_t *coordinator,
                                             int timeout_sec);

/**
 * Enable rate limiting to prevent DoS attacks
 * @param coordinator Coordinator state
 * @param max_connections Max connections per IP per window (0 = default 10)
 * @param max_messages Max messages per connection per window (0 = default 100)
 * @param window_sec Window duration in seconds (0 = default 60)
 */
void dist_coordinator_enable_rate_limiting(dist_coordinator_t *coordinator,
                                           int max_connections,
                                           int max_messages,
                                           int window_sec);

/**
 * Configure TLS for encrypted communication (optional)
 * Requires OpenSSL to be available at compile time.
 * @param coordinator Coordinator state
 * @param cert_file Path to PEM certificate file
 * @param key_file Path to PEM private key file
 * @return 0 on success, -1 on error (TLS not available or files not found)
 */
int dist_coordinator_enable_tls(dist_coordinator_t *coordinator,
                                const char *cert_file,
                                const char *key_file);

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
 * Print detailed worker statistics
 * Shows CPU speed, GPU speed, and total for each worker plus grand total
 * @param coordinator Coordinator state
 */
void dist_coordinator_print_worker_stats(const dist_coordinator_t *coordinator);

/**
 * Get detailed speed statistics
 * @param coordinator Coordinator state
 * @param total_cpu_speed Output: total CPU speed across all workers (Mkeys/s)
 * @param total_gpu_speed Output: total GPU speed across all workers (Mkeys/s)
 * @param total_combined Output: total combined speed (Mkeys/s)
 */
void dist_coordinator_get_speed_stats(const dist_coordinator_t *coordinator,
                                      double *total_cpu_speed,
                                      double *total_gpu_speed,
                                      double *total_combined);

/**
 * Shutdown coordinator
 * @param coordinator Coordinator state
 */
void dist_coordinator_shutdown(dist_coordinator_t *coordinator);

/* ============================================================================
 * Persistent State Functions
 * ============================================================================ */

/**
 * Save coordinator state to file for recovery after restart
 * @param coordinator Coordinator state
 * @param filepath Path to save state (e.g., "coordinator_state.json")
 * @return 0 on success, -1 on error
 */
int dist_coordinator_save_state(const dist_coordinator_t *coordinator,
                                const char *filepath);

/**
 * Load coordinator state from file
 * Call this before dist_coordinator_start() to resume previous session
 * @param coordinator Coordinator state (must be initialized)
 * @param filepath Path to state file
 * @return 0 on success, 1 if file not found, -1 on error
 */
int dist_coordinator_load_state(dist_coordinator_t *coordinator,
                                const char *filepath);

/**
 * Get default state file path based on job configuration
 * @param coordinator Coordinator state
 * @param filepath Output buffer for path
 * @param filepath_size Size of output buffer
 */
void dist_coordinator_get_state_path(const dist_coordinator_t *coordinator,
                                     char *filepath, size_t filepath_size);

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
 * Gracefully leave coordinator (notify before disconnect)
 * @param client Client state
 * @param reason Optional reason for leaving (can be NULL)
 * @return 0 on success, -1 on error
 */
int dist_worker_leave(dist_worker_client_t *client, const char *reason);

/**
 * Disconnect from coordinator
 * @param client Client state
 */
void dist_worker_disconnect(dist_worker_client_t *client);

/**
 * Set detailed hardware info before connecting
 * @param client Client state
 * @param cpu_cores Physical CPU cores
 * @param cpu_threads Logical CPU threads
 * @param cpu_name CPU model name
 * @param gpu_name GPU model name (NULL if no GPU)
 * @param gpu_memory_mb GPU VRAM in MB (0 if no GPU)
 */
void dist_worker_set_hardware_info(dist_worker_client_t *client,
                                   int cpu_cores, int cpu_threads,
                                   const char *cpu_name,
                                   const char *gpu_name, int gpu_memory_mb);

/**
 * Get job configuration received from coordinator
 * @param client Client state
 * @param target_address Output: target address (64 bytes min)
 * @param mode Output: search mode (32 bytes min)
 * @param key_type Output: key type (16 bytes min)
 * @return 0 on success, -1 if not connected or no config received
 */
int dist_worker_get_job_config(const dist_worker_client_t *client,
                               char *target_address,
                               char *mode,
                               char *key_type);

/**
 * Get heartbeat interval configured by server
 * @param client Client state
 * @return Interval in seconds, or 30 as default
 */
int dist_worker_get_heartbeat_interval(const dist_worker_client_t *client);

/**
 * Set authentication token for connecting to coordinator
 * @param client Client state
 * @param token Authentication token to use
 */
void dist_worker_set_auth_token(dist_worker_client_t *client, const char *token);

/**
 * Set local completed count for cross-execution tracking
 * @param client Client state
 * @param count Number of locally completed ranges
 */
void dist_worker_set_local_progress(dist_worker_client_t *client, int count);

/**
 * Enable TLS for worker client
 * Call this before dist_worker_connect() to use encrypted communication.
 * Requires the server to also have TLS enabled.
 * @param client Client state
 * @param verify_server If true, verify server certificate (requires CA or skip verify)
 * @return 0 on success, -1 on error (TLS not available)
 */
int dist_worker_enable_tls(dist_worker_client_t *client, bool verify_server);

/* ============================================================================
 * Multi-Coordinator Federation Functions
 * ============================================================================ */

/**
 * Initialize federation as primary coordinator
 * Primary coordinates work distribution to secondary coordinators
 * @param coordinator Coordinator state
 * @param federation_port Port for federation connections (can be same as worker port)
 * @return 0 on success, -1 on error
 */
int dist_federation_init_primary(dist_coordinator_t *coordinator,
                                 int federation_port);

/**
 * Initialize federation as secondary coordinator
 * Secondary receives work range assignment from primary
 * @param coordinator Coordinator state
 * @param primary_host Primary coordinator hostname/IP
 * @param primary_port Primary coordinator federation port
 * @return 0 on success, -1 on error
 */
int dist_federation_init_secondary(dist_coordinator_t *coordinator,
                                   const char *primary_host, int primary_port);

/**
 * Add a secondary coordinator to federation (primary only)
 * @param coordinator Primary coordinator state
 * @param host Secondary coordinator hostname
 * @param port Secondary coordinator port
 * @return Peer index on success, -1 on error
 */
int dist_federation_add_peer(dist_coordinator_t *coordinator,
                             const char *host, int port);

/**
 * Connect to primary coordinator (secondary only)
 * @param coordinator Secondary coordinator state
 * @return 0 on success, -1 on error
 */
int dist_federation_connect(dist_coordinator_t *coordinator);

/**
 * Process federation events
 * Call this periodically to handle peer communication
 * @param coordinator Coordinator state
 * @param timeout_ms Timeout for select
 * @return 0 on normal, 1 if results found by peer, -1 on error
 */
int dist_federation_process(dist_coordinator_t *coordinator, int timeout_ms);

/**
 * Share found result with federation peers
 * @param coordinator Coordinator state
 * @param private_key Found private key
 * @param address Found address
 * @return 0 on success, -1 on error
 */
int dist_federation_share_result(dist_coordinator_t *coordinator,
                                 const char *private_key, const char *address);

/**
 * Get federation statistics
 * @param coordinator Coordinator state
 * @param total_peers Output: number of connected peers
 * @param total_units Output: total work units across federation
 * @param total_completed Output: completed units across federation
 */
void dist_federation_stats(const dist_coordinator_t *coordinator,
                           int *total_peers, int *total_units, int *total_completed);

/**
 * Shutdown federation connections
 * @param coordinator Coordinator state
 */
void dist_federation_shutdown(dist_coordinator_t *coordinator);

#ifdef __cplusplus
}
#endif

#endif /* DISTRIBUTED_H */
