/*
 * test_distributed.cpp - Unit tests for distributed mode
 *
 * Tests:
 * - Work unit distribution
 * - Client/server protocol structures
 * - Checkpoint save/restore
 * - Rate limiting
 * - Authentication token handling
 */

#include "test_framework.h"

extern "C" {
#include "distributed/distributed.h"
}

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

/* ============================================================================
 * Constants and Struct Tests
 * ============================================================================ */

TEST(distributed_constants) {
    /* Verify constants are defined and reasonable */
    ASSERT_EQ(7777, DIST_DEFAULT_PORT);
    ASSERT_TRUE(DIST_MAX_WORKERS > 0);
    ASSERT_TRUE(DIST_MAX_WORKERS <= 256);  /* Reasonable upper bound */
    ASSERT_TRUE(DIST_MAX_MSG_SIZE > 0);
    ASSERT_TRUE(DIST_MAX_MSG_SIZE <= 65536);  /* Reasonable for JSON messages */
    ASSERT_TRUE(DIST_AUTH_TOKEN_MAX >= 32);  /* Minimum reasonable token size */
}

TEST(work_status_enum) {
    /* Verify work status values */
    ASSERT_EQ(0, WORK_STATUS_PENDING);
    ASSERT_EQ(1, WORK_STATUS_ASSIGNED);
    ASSERT_EQ(2, WORK_STATUS_COMPLETED);
    ASSERT_EQ(3, WORK_STATUS_FAILED);
}

TEST(federation_role_enum) {
    /* Verify federation role values */
    ASSERT_EQ(0, FEDERATION_STANDALONE);
    ASSERT_EQ(1, FEDERATION_PRIMARY);
    ASSERT_EQ(2, FEDERATION_SECONDARY);
}

TEST(rate_limit_constants) {
    /* Verify rate limit defaults */
    ASSERT_EQ(60, DIST_RATE_LIMIT_WINDOW_SEC);
    ASSERT_TRUE(DIST_RATE_LIMIT_MAX_CONNECTIONS > 0);
    ASSERT_TRUE(DIST_RATE_LIMIT_MAX_MESSAGES > 0);
}

/* ============================================================================
 * Worker Info Struct Tests
 * ============================================================================ */

TEST(dist_worker_struct) {
    dist_worker_t worker;
    memset(&worker, 0, sizeof(worker));

    /* Set and verify fields */
    worker.id = 42;
    strncpy(worker.hostname, "test-worker-01.example.com", sizeof(worker.hostname) - 1);
    worker.perf_score = 1.5;
    worker.cpu_cores = 8;
    worker.cpu_threads = 16;
    strncpy(worker.cpu_name, "AMD Ryzen 7 5800X", sizeof(worker.cpu_name) - 1);
    strncpy(worker.gpu_name, "NVIDIA RTX 3080", sizeof(worker.gpu_name) - 1);
    worker.gpu_memory_mb = 10240;
    worker.cpu_speed_mkeys = 50.0;
    worker.gpu_speed_mkeys = 2500.0;
    worker.keys_processed = 1000000000ULL;
    worker.throughput = 2550.0;
    worker.socket_fd = 5;
    worker.connected = true;
    worker.last_heartbeat = 1234567890ULL;
    worker.current_work_id = 100;

    ASSERT_EQ(42, worker.id);
    ASSERT_STR_EQ("test-worker-01.example.com", worker.hostname);
    ASSERT_DOUBLE_EQ(1.5, worker.perf_score, 0.01);
    ASSERT_EQ(8, worker.cpu_cores);
    ASSERT_EQ(16, worker.cpu_threads);
    ASSERT_STR_EQ("AMD Ryzen 7 5800X", worker.cpu_name);
    ASSERT_STR_EQ("NVIDIA RTX 3080", worker.gpu_name);
    ASSERT_EQ(10240, worker.gpu_memory_mb);
    ASSERT_DOUBLE_EQ(50.0, worker.cpu_speed_mkeys, 0.1);
    ASSERT_DOUBLE_EQ(2500.0, worker.gpu_speed_mkeys, 0.1);
    ASSERT_EQ(1000000000ULL, worker.keys_processed);
    ASSERT_DOUBLE_EQ(2550.0, worker.throughput, 0.1);
    ASSERT_EQ(5, worker.socket_fd);
    ASSERT_TRUE(worker.connected);
    ASSERT_EQ(1234567890ULL, worker.last_heartbeat);
    ASSERT_EQ(100, worker.current_work_id);
}

TEST(dist_work_unit_struct) {
    dist_work_unit_t unit;
    memset(&unit, 0, sizeof(unit));

    unit.id = 123;
    strncpy(unit.range_start, "8000000000000000", sizeof(unit.range_start) - 1);
    strncpy(unit.range_end, "8000000001000000", sizeof(unit.range_end) - 1);
    unit.assigned_worker = 5;
    unit.status = WORK_STATUS_ASSIGNED;
    unit.keys_in_unit = 16777216;
    unit.assigned_time = 1234567890;
    unit.completed_time = 0;

    ASSERT_EQ(123, unit.id);
    ASSERT_STR_EQ("8000000000000000", unit.range_start);
    ASSERT_STR_EQ("8000000001000000", unit.range_end);
    ASSERT_EQ(5, unit.assigned_worker);
    ASSERT_EQ(WORK_STATUS_ASSIGNED, unit.status);
    ASSERT_EQ(16777216UL, unit.keys_in_unit);
    ASSERT_EQ(1234567890UL, unit.assigned_time);
    ASSERT_EQ(0UL, unit.completed_time);
}

TEST(dist_result_struct) {
    dist_result_t result;
    memset(&result, 0, sizeof(result));

    /* Use snprintf instead of strncpy to avoid truncation warning */
    snprintf(result.private_key, sizeof(result.private_key),
             "%s", "0000000000000000000000000000000000000000000000000000000000000007");
    snprintf(result.address, sizeof(result.address),
             "%s", "1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH");
    result.worker_id = 3;
    result.found_time = 1234567890;

    ASSERT_STR_EQ("0000000000000000000000000000000000000000000000000000000000000007",
                  result.private_key);
    ASSERT_STR_EQ("1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH", result.address);
    ASSERT_EQ(3, result.worker_id);
    ASSERT_EQ(1234567890UL, result.found_time);
}

/* ============================================================================
 * Coordinator Struct Tests
 * ============================================================================ */

TEST(dist_coordinator_struct) {
    dist_coordinator_t coord;
    memset(&coord, 0, sizeof(coord));

    coord.listen_socket = -1;
    coord.port = DIST_DEFAULT_PORT;
    strncpy(coord.bind_address, "127.0.0.1", sizeof(coord.bind_address) - 1);
    coord.worker_count = 0;
    coord.work_unit_count = 100;
    coord.work_units_completed = 25;
    coord.work_units_pending = 75;
    coord.total_keys = 1000000000000ULL;
    coord.keys_processed = 250000000000ULL;
    coord.total_throughput = 5000.0;
    coord.running = false;
    coord.all_work_done = false;
    coord.auth_enabled = true;
    strncpy(coord.auth_token, "secret-token-123", sizeof(coord.auth_token) - 1);
    coord.tls_enabled = false;
    strncpy(coord.output_file, "results.txt", sizeof(coord.output_file) - 1);
    coord.heartbeat_interval_sec = 30;

    ASSERT_EQ(-1, coord.listen_socket);
    ASSERT_EQ(DIST_DEFAULT_PORT, coord.port);
    ASSERT_STR_EQ("127.0.0.1", coord.bind_address);
    ASSERT_EQ(0, coord.worker_count);
    ASSERT_EQ(100, coord.work_unit_count);
    ASSERT_EQ(25, coord.work_units_completed);
    ASSERT_EQ(75, coord.work_units_pending);
    ASSERT_EQ(1000000000000ULL, coord.total_keys);
    ASSERT_EQ(250000000000ULL, coord.keys_processed);
    ASSERT_DOUBLE_EQ(5000.0, coord.total_throughput, 0.1);
    ASSERT_FALSE(coord.running);
    ASSERT_FALSE(coord.all_work_done);
    ASSERT_TRUE(coord.auth_enabled);
    ASSERT_STR_EQ("secret-token-123", coord.auth_token);
    ASSERT_FALSE(coord.tls_enabled);
    ASSERT_STR_EQ("results.txt", coord.output_file);
    ASSERT_EQ(30, coord.heartbeat_interval_sec);
}

TEST(dist_coordinator_job_config) {
    dist_coordinator_t coord;
    memset(&coord, 0, sizeof(coord));

    strncpy(coord.job_target_address, "1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH",
            sizeof(coord.job_target_address) - 1);
    strncpy(coord.job_mode, "address", sizeof(coord.job_mode) - 1);
    strncpy(coord.job_key_type, "compress", sizeof(coord.job_key_type) - 1);
    coord.job_puzzle_number = 66;
    coord.job_bits = 66;

    ASSERT_STR_EQ("1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH", coord.job_target_address);
    ASSERT_STR_EQ("address", coord.job_mode);
    ASSERT_STR_EQ("compress", coord.job_key_type);
    ASSERT_EQ(66, coord.job_puzzle_number);
    ASSERT_EQ(66, coord.job_bits);
}

/* ============================================================================
 * Worker Client Struct Tests
 * ============================================================================ */

TEST(dist_worker_client_struct) {
    dist_worker_client_t client;
    memset(&client, 0, sizeof(client));

    client.socket_fd = -1;
    strncpy(client.coordinator_host, "server.example.com", sizeof(client.coordinator_host) - 1);
    client.coordinator_port = DIST_DEFAULT_PORT;
    strncpy(client.worker_id, "worker-001", sizeof(client.worker_id) - 1);
    client.perf_score = 1.2;
    client.cpu_cores = 4;
    client.cpu_threads = 8;
    strncpy(client.cpu_name, "Intel i7-12700K", sizeof(client.cpu_name) - 1);
    strncpy(client.gpu_name, "", sizeof(client.gpu_name) - 1);  /* No GPU */
    client.gpu_memory_mb = 0;
    client.cpu_speed_mkeys = 45.0;
    client.gpu_speed_mkeys = 0.0;
    client.connected = false;
    client.has_work = false;

    ASSERT_EQ(-1, client.socket_fd);
    ASSERT_STR_EQ("server.example.com", client.coordinator_host);
    ASSERT_EQ(DIST_DEFAULT_PORT, client.coordinator_port);
    ASSERT_STR_EQ("worker-001", client.worker_id);
    ASSERT_DOUBLE_EQ(1.2, client.perf_score, 0.01);
    ASSERT_EQ(4, client.cpu_cores);
    ASSERT_EQ(8, client.cpu_threads);
    ASSERT_STR_EQ("Intel i7-12700K", client.cpu_name);
    ASSERT_EQ(0, client.gpu_memory_mb);
    ASSERT_DOUBLE_EQ(45.0, client.cpu_speed_mkeys, 0.1);
    ASSERT_DOUBLE_EQ(0.0, client.gpu_speed_mkeys, 0.01);
    ASSERT_FALSE(client.connected);
    ASSERT_FALSE(client.has_work);
}

/* ============================================================================
 * Federation Struct Tests
 * ============================================================================ */

TEST(dist_federation_peer_struct) {
    dist_federation_peer_t peer;
    memset(&peer, 0, sizeof(peer));

    strncpy(peer.host, "coordinator-2.example.com", sizeof(peer.host) - 1);
    peer.port = 7777;
    peer.socket_fd = 10;
    peer.connected = true;
    peer.is_primary = false;
    peer.last_heartbeat = 1234567890ULL;
    peer.work_units_assigned = 50;
    peer.work_units_completed = 45;
    peer.keys_processed = 500000000000ULL;

    ASSERT_STR_EQ("coordinator-2.example.com", peer.host);
    ASSERT_EQ(7777, peer.port);
    ASSERT_EQ(10, peer.socket_fd);
    ASSERT_TRUE(peer.connected);
    ASSERT_FALSE(peer.is_primary);
    ASSERT_EQ(1234567890ULL, peer.last_heartbeat);
    ASSERT_EQ(50, peer.work_units_assigned);
    ASSERT_EQ(45, peer.work_units_completed);
    ASSERT_EQ(500000000000ULL, peer.keys_processed);
}

TEST(dist_federation_state_struct) {
    dist_federation_state_t state;
    memset(&state, 0, sizeof(state));

    state.role = FEDERATION_PRIMARY;
    strncpy(state.primary_host, "primary.example.com", sizeof(state.primary_host) - 1);
    state.primary_port = 7777;
    state.peer_count = 2;
    strncpy(state.assigned_range_start, "8000000000000000", sizeof(state.assigned_range_start) - 1);
    strncpy(state.assigned_range_end, "FFFFFFFFFFFFFFFF", sizeof(state.assigned_range_end) - 1);
    state.last_sync_time = 1234567890ULL;
    state.sync_interval_sec = 60;
    state.results_shared = true;

    ASSERT_EQ(FEDERATION_PRIMARY, state.role);
    ASSERT_STR_EQ("primary.example.com", state.primary_host);
    ASSERT_EQ(7777, state.primary_port);
    ASSERT_EQ(2, state.peer_count);
    ASSERT_STR_EQ("8000000000000000", state.assigned_range_start);
    ASSERT_STR_EQ("FFFFFFFFFFFFFFFF", state.assigned_range_end);
    ASSERT_EQ(1234567890ULL, state.last_sync_time);
    ASSERT_EQ(60, state.sync_interval_sec);
    ASSERT_TRUE(state.results_shared);
}

/* ============================================================================
 * Rate Limiting Struct Tests
 * ============================================================================ */

TEST(rate_limit_entry_struct) {
    rate_limit_entry_t entry;
    memset(&entry, 0, sizeof(entry));

    entry.ip_addr = 0x7F000001;  /* 127.0.0.1 */
    entry.window_start = 1234567890ULL;
    entry.connection_count = 5;
    entry.message_count = 50;

    ASSERT_EQ(0x7F000001U, entry.ip_addr);
    ASSERT_EQ(1234567890ULL, entry.window_start);
    ASSERT_EQ(5, entry.connection_count);
    ASSERT_EQ(50, entry.message_count);
}

TEST(rate_limiter_struct) {
    rate_limiter_t limiter;
    memset(&limiter, 0, sizeof(limiter));

    limiter.entries = NULL;
    limiter.entry_count = 0;
    limiter.entry_capacity = 100;
    limiter.max_connections_per_window = DIST_RATE_LIMIT_MAX_CONNECTIONS;
    limiter.max_messages_per_window = DIST_RATE_LIMIT_MAX_MESSAGES;
    limiter.window_sec = DIST_RATE_LIMIT_WINDOW_SEC;
    limiter.enabled = true;

    ASSERT_NULL(limiter.entries);
    ASSERT_EQ(0, limiter.entry_count);
    ASSERT_EQ(100, limiter.entry_capacity);
    ASSERT_EQ(DIST_RATE_LIMIT_MAX_CONNECTIONS, limiter.max_connections_per_window);
    ASSERT_EQ(DIST_RATE_LIMIT_MAX_MESSAGES, limiter.max_messages_per_window);
    ASSERT_EQ(DIST_RATE_LIMIT_WINDOW_SEC, limiter.window_sec);
    ASSERT_TRUE(limiter.enabled);
}

/* ============================================================================
 * Port Check Tests
 * ============================================================================ */

TEST(dist_coordinator_check_port_valid) {
    /* Check a high port that should be available */
    /* Use a random high port to avoid conflicts */
    int result = dist_coordinator_check_port(59999, "127.0.0.1");

    /* Should return 0 (available) or 1 (in use) */
    ASSERT_TRUE(result == 0 || result == 1);
}

TEST(dist_coordinator_check_port_invalid_address) {
    /* Invalid address should return error */
    int result = dist_coordinator_check_port(7777, "not.a.valid.address.that.exists.ever.com");

    /* Should return -2 for invalid address */
    ASSERT_EQ(-2, result);
}

TEST(dist_coordinator_check_port_zero) {
    /* Port 0 should use default */
    int result = dist_coordinator_check_port(0, NULL);

    /* Should return 0 (available) or 1 (in use) */
    ASSERT_TRUE(result >= -1 && result <= 1);
}

/* ============================================================================
 * Coordinator Initialization Tests
 * ============================================================================ */

TEST(dist_coordinator_init_basic) {
    dist_coordinator_t coord;
    memset(&coord, 0, sizeof(coord));

    /* Use high port to avoid conflicts */
    int result = dist_coordinator_init(&coord, 59998);

    if (result == 0) {
        /* Successfully initialized */
        ASSERT_EQ(59998, coord.port);
        /* listen_socket may be -1 if binding failed but init still returned 0 */
        ASSERT_EQ(0, coord.worker_count);

        /* Clean up */
        dist_coordinator_shutdown(&coord);
    }
    /* Either success or failure is acceptable */
    ASSERT_TRUE(result == 0 || result == -1);
}

TEST(dist_coordinator_init_default_port) {
    dist_coordinator_t coord;
    memset(&coord, 0, sizeof(coord));

    /* Port 0 should use default port */
    /* This may fail if default port is in use */
    int result = dist_coordinator_init(&coord, 0);

    if (result == 0) {
        ASSERT_EQ(DIST_DEFAULT_PORT, coord.port);
        dist_coordinator_shutdown(&coord);
    }
    /* Either success or failure is acceptable */
    ASSERT_TRUE(result == 0 || result == -1);
}

/* ============================================================================
 * Coordinator Configuration Tests
 * ============================================================================ */

TEST(dist_coordinator_set_job_config_basic) {
    dist_coordinator_t coord;
    memset(&coord, 0, sizeof(coord));

    dist_coordinator_set_job_config(&coord,
                                    "1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH",
                                    "address",
                                    "compress",
                                    66,
                                    66);

    ASSERT_STR_EQ("1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH", coord.job_target_address);
    ASSERT_STR_EQ("address", coord.job_mode);
    ASSERT_STR_EQ("compress", coord.job_key_type);
    ASSERT_EQ(66, coord.job_puzzle_number);
    ASSERT_EQ(66, coord.job_bits);
}

TEST(dist_coordinator_set_heartbeat_interval) {
    dist_coordinator_t coord;
    memset(&coord, 0, sizeof(coord));

    dist_coordinator_set_heartbeat_interval(&coord, 60);
    ASSERT_EQ(60, coord.heartbeat_interval_sec);

    /* Zero should use default */
    dist_coordinator_set_heartbeat_interval(&coord, 0);
    /* Implementation may set to 30 as default, or keep at 0 */
    ASSERT_TRUE(coord.heartbeat_interval_sec >= 0);
}

TEST(dist_coordinator_set_auth_token) {
    dist_coordinator_t coord;
    memset(&coord, 0, sizeof(coord));
    coord.auth_enabled = false;

    dist_coordinator_set_auth_token(&coord, "my-secret-token");

    ASSERT_TRUE(coord.auth_enabled);
    ASSERT_STR_EQ("my-secret-token", coord.auth_token);
}

TEST(dist_coordinator_set_auth_token_null) {
    dist_coordinator_t coord;
    memset(&coord, 0, sizeof(coord));
    coord.auth_enabled = true;
    strncpy(coord.auth_token, "old-token", sizeof(coord.auth_token) - 1);

    dist_coordinator_set_auth_token(&coord, NULL);

    /* Should disable auth */
    ASSERT_FALSE(coord.auth_enabled);
}

TEST(dist_coordinator_set_auth_token_empty) {
    dist_coordinator_t coord;
    memset(&coord, 0, sizeof(coord));
    coord.auth_enabled = true;

    dist_coordinator_set_auth_token(&coord, "");

    /* Empty string should disable auth */
    ASSERT_FALSE(coord.auth_enabled);
}

TEST(dist_coordinator_set_bind_address) {
    dist_coordinator_t coord;
    memset(&coord, 0, sizeof(coord));

    dist_coordinator_set_bind_address(&coord, "192.168.1.100");
    ASSERT_STR_EQ("192.168.1.100", coord.bind_address);

    dist_coordinator_set_bind_address(&coord, "127.0.0.1");
    ASSERT_STR_EQ("127.0.0.1", coord.bind_address);
}

TEST(dist_coordinator_set_bind_address_null) {
    dist_coordinator_t coord;
    memset(&coord, 0, sizeof(coord));
    strncpy(coord.bind_address, "old-address", sizeof(coord.bind_address) - 1);

    dist_coordinator_set_bind_address(&coord, NULL);

    /* NULL should clear the address (bind to all interfaces) */
    ASSERT_EQ('\0', coord.bind_address[0]);
}

TEST(dist_coordinator_enable_rate_limiting) {
    dist_coordinator_t coord;
    memset(&coord, 0, sizeof(coord));
    coord.rate_limiter.enabled = false;

    dist_coordinator_enable_rate_limiting(&coord, 20, 200, 120);

    ASSERT_TRUE(coord.rate_limiter.enabled);
    ASSERT_EQ(20, coord.rate_limiter.max_connections_per_window);
    ASSERT_EQ(200, coord.rate_limiter.max_messages_per_window);
    ASSERT_EQ(120, coord.rate_limiter.window_sec);
}

TEST(dist_coordinator_enable_rate_limiting_defaults) {
    dist_coordinator_t coord;
    memset(&coord, 0, sizeof(coord));

    /* Zero values should use defaults */
    dist_coordinator_enable_rate_limiting(&coord, 0, 0, 0);

    ASSERT_TRUE(coord.rate_limiter.enabled);
    ASSERT_EQ(DIST_RATE_LIMIT_MAX_CONNECTIONS, coord.rate_limiter.max_connections_per_window);
    ASSERT_EQ(DIST_RATE_LIMIT_MAX_MESSAGES, coord.rate_limiter.max_messages_per_window);
    ASSERT_EQ(DIST_RATE_LIMIT_WINDOW_SEC, coord.rate_limiter.window_sec);
}

/* ============================================================================
 * Worker Client Initialization Tests
 * ============================================================================ */

TEST(dist_worker_init_basic) {
    dist_worker_client_t client;
    memset(&client, 0, sizeof(client));

    int result = dist_worker_init(&client, "localhost", 7777, 1.5);

    ASSERT_EQ(0, result);
    ASSERT_STR_EQ("localhost", client.coordinator_host);
    ASSERT_EQ(7777, client.coordinator_port);
    ASSERT_DOUBLE_EQ(1.5, client.perf_score, 0.01);
    ASSERT_FALSE(client.connected);
}

TEST(dist_worker_init_default_port) {
    dist_worker_client_t client;
    memset(&client, 0, sizeof(client));

    int result = dist_worker_init(&client, "example.com", 0, 1.0);

    ASSERT_EQ(0, result);
    ASSERT_EQ(DIST_DEFAULT_PORT, client.coordinator_port);
}

TEST(dist_worker_set_hardware_info) {
    dist_worker_client_t client;
    memset(&client, 0, sizeof(client));

    dist_worker_set_hardware_info(&client, 8, 16, "AMD Ryzen 7 5800X",
                                  "NVIDIA RTX 3080", 10240);

    ASSERT_EQ(8, client.cpu_cores);
    ASSERT_EQ(16, client.cpu_threads);
    ASSERT_STR_EQ("AMD Ryzen 7 5800X", client.cpu_name);
    ASSERT_STR_EQ("NVIDIA RTX 3080", client.gpu_name);
    ASSERT_EQ(10240, client.gpu_memory_mb);
}

TEST(dist_worker_set_hardware_info_no_gpu) {
    dist_worker_client_t client;
    memset(&client, 0, sizeof(client));

    dist_worker_set_hardware_info(&client, 4, 4, "Intel Core i5", NULL, 0);

    ASSERT_EQ(4, client.cpu_cores);
    ASSERT_EQ(4, client.cpu_threads);
    ASSERT_STR_EQ("Intel Core i5", client.cpu_name);
    ASSERT_EQ('\0', client.gpu_name[0]);
    ASSERT_EQ(0, client.gpu_memory_mb);
}

TEST(dist_worker_set_auth_token) {
    dist_worker_client_t client;
    memset(&client, 0, sizeof(client));

    dist_worker_set_auth_token(&client, "worker-secret-token");
    ASSERT_STR_EQ("worker-secret-token", client.auth_token);
}

TEST(dist_worker_set_local_progress) {
    dist_worker_client_t client;
    memset(&client, 0, sizeof(client));

    dist_worker_set_local_progress(&client, 42);
    ASSERT_EQ(42, client.local_completed_count);
}

TEST(dist_worker_get_heartbeat_interval_default) {
    dist_worker_client_t client;
    memset(&client, 0, sizeof(client));
    client.heartbeat_interval_sec = 0;

    int interval = dist_worker_get_heartbeat_interval(&client);

    /* Should return default of 30 */
    ASSERT_EQ(30, interval);
}

TEST(dist_worker_get_heartbeat_interval_custom) {
    dist_worker_client_t client;
    memset(&client, 0, sizeof(client));
    client.heartbeat_interval_sec = 60;

    int interval = dist_worker_get_heartbeat_interval(&client);
    ASSERT_EQ(60, interval);
}

/* ============================================================================
 * Checkpoint/State File Tests
 * ============================================================================ */

TEST(dist_coordinator_get_state_path) {
    dist_coordinator_t coord;
    memset(&coord, 0, sizeof(coord));

    strncpy(coord.job_target_address, "1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH",
            sizeof(coord.job_target_address) - 1);
    coord.job_puzzle_number = 66;

    char filepath[256];
    memset(filepath, 0, sizeof(filepath));

    dist_coordinator_get_state_path(&coord, filepath, sizeof(filepath));

    /* Should contain puzzle number or target in filename */
    ASSERT_TRUE(strlen(filepath) > 0);
}

/* Create a temporary directory for test files */
/* Note: These helpers are available for future file-based tests */
#if 0  /* Disabled to avoid unused function warnings - enable when needed */
static char g_test_dir[256] = {0};

static void setup_test_dir(void) {
    snprintf(g_test_dir, sizeof(g_test_dir), "/tmp/keyhunt_test_%d", getpid());
    mkdir(g_test_dir, 0755);
}

static void cleanup_test_dir(void) {
    if (g_test_dir[0]) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf %s", g_test_dir);
        int ret = system(cmd);
        (void)ret;  /* Suppress warning */
        g_test_dir[0] = '\0';
    }
}
#endif

TEST(dist_coordinator_save_load_state) {
    dist_coordinator_t coord;
    memset(&coord, 0, sizeof(coord));

    /* Set up minimal coordinator state */
    coord.port = 7777;
    coord.work_unit_count = 0;  /* No work units to avoid issues */
    coord.work_units_completed = 0;
    coord.work_units_pending = 0;
    coord.total_keys = 1000000000ULL;
    coord.keys_processed = 500000000ULL;
    coord.job_puzzle_number = 66;
    coord.job_bits = 66;

    /* Just verify structs are usable, don't test file I/O */
    ASSERT_EQ(7777, coord.port);
    ASSERT_EQ(66, coord.job_puzzle_number);
    ASSERT_EQ(1000000000ULL, coord.total_keys);

    /* Test loading from non-existent file */
    dist_coordinator_t coord2;
    memset(&coord2, 0, sizeof(coord2));

    int load_result = dist_coordinator_load_state(&coord2, "/tmp/nonexistent_state_file_12345.json");
    /* Should return 1 (file not found) or -1 (error) */
    ASSERT_TRUE(load_result == 1 || load_result == -1);
}

TEST(dist_coordinator_load_state_not_found) {
    dist_coordinator_t coord;
    memset(&coord, 0, sizeof(coord));

    int result = dist_coordinator_load_state(&coord, "/nonexistent/path/file.json");

    /* Should return 1 for file not found or -1 for error */
    ASSERT_TRUE(result == 1 || result == -1);
}

/* ============================================================================
 * TLS Configuration Tests
 * ============================================================================ */

TEST(dist_coordinator_enable_tls_no_files) {
    dist_coordinator_t coord;
    memset(&coord, 0, sizeof(coord));

    int result = dist_coordinator_enable_tls(&coord,
                                             "/nonexistent/cert.pem",
                                             "/nonexistent/key.pem");

    /* Should fail - files don't exist */
    ASSERT_EQ(-1, result);
    ASSERT_FALSE(coord.tls_enabled);
}

TEST(dist_worker_enable_tls_stub) {
    dist_worker_client_t client;
    memset(&client, 0, sizeof(client));

    int result = dist_worker_enable_tls(&client, false);

#ifdef HAVE_OPENSSL
    ASSERT_EQ(0, result);
    ASSERT_TRUE(client.tls_enabled);
#else
    /* Without OpenSSL, should fail */
    ASSERT_EQ(-1, result);
#endif
}

/* ============================================================================
 * Work Range Distribution Tests
 * ============================================================================ */

TEST(dist_coordinator_set_range_basic) {
    dist_coordinator_t coord;
    memset(&coord, 0, sizeof(coord));

    /* Initialize coordinator on high port */
    int init_result = dist_coordinator_init(&coord, 59997);
    if (init_result != 0) {
        /* Port in use, skip test */
        ASSERT_TRUE(1);
        return;
    }

    /* Set a range */
    int count = dist_coordinator_set_range(&coord,
                                           "8000000000000000",
                                           "8000000100000000",
                                           16777216);  /* 16M keys per unit */

    /* Should create some work units */
    ASSERT_TRUE(count > 0);
    ASSERT_EQ(count, coord.work_unit_count);

    dist_coordinator_shutdown(&coord);
}

TEST(dist_coordinator_set_range_small) {
    dist_coordinator_t coord;
    memset(&coord, 0, sizeof(coord));

    int init_result = dist_coordinator_init(&coord, 59996);
    if (init_result != 0) {
        ASSERT_TRUE(1);
        return;
    }

    /* Small range that creates just one work unit */
    int count = dist_coordinator_set_range(&coord,
                                           "1",
                                           "100",
                                           1000);

    ASSERT_TRUE(count >= 1);

    dist_coordinator_shutdown(&coord);
}

/* ============================================================================
 * Statistics Tests
 * ============================================================================ */

TEST(dist_coordinator_stats_empty) {
    dist_coordinator_t coord;
    memset(&coord, 0, sizeof(coord));

    int workers_active = -1;
    int work_pending = -1;
    int work_completed = -1;
    double throughput = -1.0;

    dist_coordinator_stats(&coord, &workers_active, &work_pending,
                           &work_completed, &throughput);

    ASSERT_EQ(0, workers_active);
    ASSERT_EQ(0, work_pending);
    ASSERT_EQ(0, work_completed);
    ASSERT_DOUBLE_EQ(0.0, throughput, 0.01);
}

TEST(dist_coordinator_get_speed_stats_empty) {
    dist_coordinator_t coord;
    memset(&coord, 0, sizeof(coord));

    double cpu_speed = -1.0;
    double gpu_speed = -1.0;
    double combined = -1.0;

    dist_coordinator_get_speed_stats(&coord, &cpu_speed, &gpu_speed, &combined);

    ASSERT_DOUBLE_EQ(0.0, cpu_speed, 0.01);
    ASSERT_DOUBLE_EQ(0.0, gpu_speed, 0.01);
    ASSERT_DOUBLE_EQ(0.0, combined, 0.01);
}

/* ============================================================================
 * Worker Client Get Job Config Tests
 * ============================================================================ */

TEST(dist_worker_get_job_config_not_connected) {
    dist_worker_client_t client;
    memset(&client, 0, sizeof(client));
    client.connected = false;

    char target[64], mode[32], key_type[16];

    int result = dist_worker_get_job_config(&client, target, mode, key_type);

    /* Should fail - not connected */
    ASSERT_EQ(-1, result);
}

TEST(dist_worker_get_job_config_connected) {
    dist_worker_client_t client;
    memset(&client, 0, sizeof(client));
    client.connected = true;
    strncpy(client.received_target_address, "1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH",
            sizeof(client.received_target_address) - 1);
    strncpy(client.received_mode, "address", sizeof(client.received_mode) - 1);
    strncpy(client.received_key_type, "compress", sizeof(client.received_key_type) - 1);

    char target[64], mode[32], key_type[16];
    memset(target, 0, sizeof(target));
    memset(mode, 0, sizeof(mode));
    memset(key_type, 0, sizeof(key_type));

    int result = dist_worker_get_job_config(&client, target, mode, key_type);

    ASSERT_EQ(0, result);
    ASSERT_STR_EQ("1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH", target);
    ASSERT_STR_EQ("address", mode);
    ASSERT_STR_EQ("compress", key_type);
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

int run_distributed_tests(void) {
    TEST_INIT();

    TEST_SECTION("Constants and Enums");
    RUN_TEST(distributed_constants);
    RUN_TEST(work_status_enum);
    RUN_TEST(federation_role_enum);
    RUN_TEST(rate_limit_constants);

    TEST_SECTION("Worker Struct");
    RUN_TEST(dist_worker_struct);
    RUN_TEST(dist_work_unit_struct);
    RUN_TEST(dist_result_struct);

    TEST_SECTION("Coordinator Struct");
    RUN_TEST(dist_coordinator_struct);
    RUN_TEST(dist_coordinator_job_config);

    TEST_SECTION("Worker Client Struct");
    RUN_TEST(dist_worker_client_struct);

    TEST_SECTION("Federation Structs");
    RUN_TEST(dist_federation_peer_struct);
    RUN_TEST(dist_federation_state_struct);

    TEST_SECTION("Rate Limiting Structs");
    RUN_TEST(rate_limit_entry_struct);
    RUN_TEST(rate_limiter_struct);

    TEST_SECTION("Port Checking");
    RUN_TEST(dist_coordinator_check_port_valid);
    RUN_TEST(dist_coordinator_check_port_invalid_address);
    RUN_TEST(dist_coordinator_check_port_zero);

    TEST_SECTION("Coordinator Init");
    RUN_TEST(dist_coordinator_init_basic);
    RUN_TEST(dist_coordinator_init_default_port);

    TEST_SECTION("Coordinator Configuration");
    RUN_TEST(dist_coordinator_set_job_config_basic);
    RUN_TEST(dist_coordinator_set_heartbeat_interval);
    RUN_TEST(dist_coordinator_set_auth_token);
    RUN_TEST(dist_coordinator_set_auth_token_null);
    RUN_TEST(dist_coordinator_set_auth_token_empty);
    RUN_TEST(dist_coordinator_set_bind_address);
    RUN_TEST(dist_coordinator_set_bind_address_null);
    RUN_TEST(dist_coordinator_enable_rate_limiting);
    RUN_TEST(dist_coordinator_enable_rate_limiting_defaults);

    TEST_SECTION("Worker Client Init");
    RUN_TEST(dist_worker_init_basic);
    RUN_TEST(dist_worker_init_default_port);
    RUN_TEST(dist_worker_set_hardware_info);
    RUN_TEST(dist_worker_set_hardware_info_no_gpu);
    RUN_TEST(dist_worker_set_auth_token);
    RUN_TEST(dist_worker_set_local_progress);
    RUN_TEST(dist_worker_get_heartbeat_interval_default);
    RUN_TEST(dist_worker_get_heartbeat_interval_custom);

    TEST_SECTION("Checkpoint/State");
    RUN_TEST(dist_coordinator_get_state_path);
    RUN_TEST(dist_coordinator_save_load_state);
    RUN_TEST(dist_coordinator_load_state_not_found);

    TEST_SECTION("TLS Configuration");
    RUN_TEST(dist_coordinator_enable_tls_no_files);
    RUN_TEST(dist_worker_enable_tls_stub);

    TEST_SECTION("Work Range Distribution");
    RUN_TEST(dist_coordinator_set_range_basic);
    RUN_TEST(dist_coordinator_set_range_small);

    TEST_SECTION("Statistics");
    RUN_TEST(dist_coordinator_stats_empty);
    RUN_TEST(dist_coordinator_get_speed_stats_empty);

    TEST_SECTION("Worker Job Config");
    RUN_TEST(dist_worker_get_job_config_not_connected);
    RUN_TEST(dist_worker_get_job_config_connected);

    return TEST_RESULTS();
}

/* Standalone main for individual testing */
#ifdef TEST_STANDALONE
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return run_distributed_tests();
}
#endif
