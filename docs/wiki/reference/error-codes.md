# Error Codes and Return Values Reference

Complete reference for distributed mode function return codes and error handling.

## Overview

All distributed mode functions follow a consistent return value convention:

| Return Code | Meaning |
|-------------|---------|
| **0** | Success - operation completed successfully |
| **1** | Special status - see function-specific details below |
| **-1** | Error - operation failed (check stderr for details) |
| **-2** | Invalid parameter - check function documentation |

## Coordinator Functions

### Port Management

#### dist_coordinator_check_port

```c
int dist_coordinator_check_port(int port, const char *bind_address);
```

**Return Values:**

| Code | Meaning | Action |
|------|---------|--------|
| `0` | Port is available | Safe to proceed with initialization |
| `1` | Port is in use | Choose different port or stop conflicting service |
| `-1` | General error | Check stderr for socket creation errors |
| `-2` | Invalid bind address | Verify IP address format (e.g., "192.168.1.100") |

**Example:**
```c
int status = dist_coordinator_check_port(7777, "192.168.1.100");
if (status == 0) {
    printf("Port 7777 is available\n");
} else if (status == 1) {
    fprintf(stderr, "Port 7777 is already in use\n");
    exit(1);
} else if (status == -2) {
    fprintf(stderr, "Invalid IP address: 192.168.1.100\n");
    exit(1);
} else {
    fprintf(stderr, "Error checking port availability\n");
    exit(1);
}
```

**Common Errors:**
- Port already bound by another process (return 1)
- Invalid IP address format (return -2)
- Permission denied on privileged ports < 1024 (return -1)

**Cross-References:**
- [Distributed Mode Guide](../distributed/README.md)
- [CLI Options](./cli-options.md#distributed-options)

---

#### dist_coordinator_init

```c
int dist_coordinator_init(dist_coordinator_t *coordinator, int port);
```

**Return Values:**

| Code | Meaning |
|------|---------|
| `0` | Coordinator initialized successfully |
| `-1` | Initialization failed (Windows platform or internal error) |

**Platform Support:**
- **Linux/POSIX:** Fully supported
- **Windows:** Not supported (always returns -1)

**Example:**
```c
dist_coordinator_t coord;
if (dist_coordinator_init(&coord, 7777) != 0) {
    fprintf(stderr, "Failed to initialize coordinator\n");
    exit(1);
}
```

**Common Errors:**
- Platform not supported (Windows returns -1)
- Invalid coordinator pointer (NULL)

---

### Work Distribution

#### dist_coordinator_set_range

```c
int dist_coordinator_set_range(dist_coordinator_t *coordinator,
                               const char *range_start,
                               const char *range_end,
                               uint64_t work_unit_size);
```

**Return Values:**

| Code | Meaning |
|------|---------|
| `> 0` | Number of work units created |
| `-1` | Error - invalid range or allocation failure |

**Example:**
```c
int units = dist_coordinator_set_range(&coord,
                                       "20000000000000000",
                                       "3FFFFFFFFFFFFFFFF",
                                       0x100000000);  /* 4B keys per unit */
if (units < 0) {
    fprintf(stderr, "Failed to set work range\n");
    exit(1);
}
printf("Created %d work units\n", units);
```

**Common Errors:**
- Invalid hex string format (return -1)
- range_start >= range_end (return -1)
- Memory allocation failure for work units (return -1)
- work_unit_size is 0 (return -1)

**Cross-References:**
- [Work Unit Sizing Guide](../distributed/work-units.md)

---

#### dist_coordinator_start

```c
int dist_coordinator_start(dist_coordinator_t *coordinator);
```

**Return Values:**

| Code | Meaning |
|------|---------|
| `0` | Server started, listening for workers |
| `-1` | Failed to start (socket error or already started) |

**Example:**
```c
if (dist_coordinator_start(&coord) != 0) {
    fprintf(stderr, "Failed to start coordinator\n");
    exit(1);
}
printf("Coordinator listening on port %d\n", coord.port);
```

**Common Errors:**
- Port already in use (return -1) - use `dist_coordinator_check_port` first
- Socket creation failure (return -1)
- Bind failure (return -1) - check permissions and bind address
- Listen queue full (return -1)

---

#### dist_coordinator_process

```c
int dist_coordinator_process(dist_coordinator_t *coordinator, int timeout_ms);
```

**Return Values:**

| Code | Meaning | Action |
|------|---------|--------|
| `0` | Normal operation | Continue calling in loop |
| `1` | All work complete | Exit main loop, print results |
| `-1` | Error occurred | Check stderr, may need to restart |

**Example:**
```c
while (1) {
    int status = dist_coordinator_process(&coord, 1000);  /* 1 second timeout */
    if (status == 1) {
        printf("All work completed!\n");
        break;
    } else if (status < 0) {
        fprintf(stderr, "Error processing coordinator events\n");
        break;
    }
    /* status == 0: continue */
}
```

**Common Errors:**
- select() failure (return -1)
- Worker connection errors (logged to stderr, continues with return 0)
- Message parsing errors (logged to stderr, continues with return 0)

**Performance Notes:**
- `timeout_ms = 0`: Non-blocking, returns immediately (busy loop - high CPU)
- `timeout_ms = 1000`: 1 second timeout (recommended for main loop)
- `timeout_ms = -1`: Infinite timeout (blocks until event)

---

### State Persistence

#### dist_coordinator_save_state

```c
int dist_coordinator_save_state(const dist_coordinator_t *coordinator,
                                const char *filepath);
```

**Return Values:**

| Code | Meaning |
|------|---------|
| `0` | State saved successfully |
| `-1` | Failed to save (file error or JSON serialization failure) |

**Example:**
```c
if (dist_coordinator_save_state(&coord, "state.json") != 0) {
    fprintf(stderr, "Warning: Could not save state\n");
    /* Non-fatal - continue operation */
}
```

**Common Errors:**
- Permission denied writing to filepath (return -1)
- Directory does not exist (return -1)
- Disk full (return -1)
- Invalid coordinator state (return -1)

**Cross-References:**
- [State File Format](../distributed/state-format.md)

---

#### dist_coordinator_load_state

```c
int dist_coordinator_load_state(dist_coordinator_t *coordinator,
                                const char *filepath);
```

**Return Values:**

| Code | Meaning | Action |
|------|---------|--------|
| `0` | State loaded successfully | Resume from saved progress |
| `1` | File not found | Start fresh (not an error) |
| `-1` | Parse error or corrupted file | Start fresh, warn user |

**Example:**
```c
int status = dist_coordinator_load_state(&coord, "state.json");
if (status == 0) {
    printf("Resumed from saved state\n");
} else if (status == 1) {
    printf("No previous state found, starting fresh\n");
} else {
    fprintf(stderr, "Warning: State file corrupted, starting fresh\n");
}
```

**Common Errors:**
- JSON parse error (return -1) - corrupted file
- Version mismatch (return -1) - incompatible state file
- Missing required fields (return -1) - corrupted or old format

---

### TLS/SSL Functions

#### dist_coordinator_enable_tls

```c
int dist_coordinator_enable_tls(dist_coordinator_t *coordinator,
                                const char *cert_file,
                                const char *key_file);
```

**Return Values:**

| Code | Meaning |
|------|---------|
| `0` | TLS enabled successfully |
| `-1` | TLS not available (not compiled with HAVE_OPENSSL) or certificate error |

**Build Requirements:**
```bash
# Build with TLS support
make ENABLE_TLS=1

# Build without TLS support (default)
make
```

**Example:**
```c
if (dist_coordinator_enable_tls(&coord, "server.crt", "server.key") != 0) {
    fprintf(stderr, "Failed to enable TLS\n");
    fprintf(stderr, "Ensure keyhunt was built with: make ENABLE_TLS=1\n");
    exit(1);
}
printf("TLS encryption enabled\n");
```

**Common Errors:**
- Not compiled with OpenSSL (return -1) - rebuild with `ENABLE_TLS=1`
- Certificate file not found (return -1)
- Private key file not found (return -1)
- Certificate/key mismatch (return -1)
- Invalid PEM format (return -1)

**Cross-References:**
- [TLS Setup Guide](../distributed/tls-setup.md)
- [Certificate Generation](../distributed/tls-setup.md#generating-certificates)

---

## Worker Client Functions

### Connection Management

#### dist_worker_init

```c
int dist_worker_init(dist_worker_client_t *client,
                     const char *coordinator_host,
                     int coordinator_port,
                     double perf_score);
```

**Return Values:**

| Code | Meaning |
|------|---------|
| `0` | Worker client initialized |
| `-1` | Initialization failed (Windows platform) |

**Example:**
```c
dist_worker_client_t worker;
if (dist_worker_init(&worker, "192.168.1.100", 7777, 1.0) != 0) {
    fprintf(stderr, "Failed to initialize worker\n");
    exit(1);
}
```

---

#### dist_worker_connect

```c
int dist_worker_connect(dist_worker_client_t *client);
```

**Return Values:**

| Code | Meaning |
|------|---------|
| `0` | Connected to coordinator and registered |
| `-1` | Connection failed (network error, authentication failure, or rejection) |

**Example:**
```c
if (dist_worker_connect(&worker) != 0) {
    fprintf(stderr, "Failed to connect to coordinator\n");
    fprintf(stderr, "Check network connectivity and coordinator status\n");
    exit(1);
}
printf("Connected to coordinator\n");
```

**Common Errors:**
- Coordinator not reachable (return -1) - check host/port
- Authentication failed (return -1) - check auth token
- Connection timeout (return -1) - coordinator may be overloaded
- TLS handshake failure (return -1) - certificate mismatch
- Coordinator rejected connection (return -1) - rate limited or banned

**Troubleshooting:**
```bash
# Test connectivity
ping 192.168.1.100

# Check if port is open
telnet 192.168.1.100 7777

# Check coordinator logs
tail -f coordinator.log
```

---

#### dist_worker_request_work

```c
int dist_worker_request_work(dist_worker_client_t *client,
                             char *range_start,
                             char *range_end);
```

**Return Values:**

| Code | Meaning | Action |
|------|---------|--------|
| `0` | Work assigned | Start processing range |
| `1` | No work available | Wait or disconnect gracefully |
| `-1` | Communication error | Reconnect or exit |

**Example:**
```c
char range_start[65], range_end[65];
int status = dist_worker_request_work(&worker, range_start, range_end);

if (status == 0) {
    printf("Assigned range: %s to %s\n", range_start, range_end);
    /* Process work unit */
} else if (status == 1) {
    printf("No work available, all units assigned\n");
    sleep(30);  /* Wait for other workers to complete */
} else {
    fprintf(stderr, "Failed to request work\n");
    /* Reconnect or exit */
}
```

**Common Scenarios:**
- `return 0`: Work assigned successfully
- `return 1`: All work assigned to other workers (temporary)
- `return 1`: All work completed (permanent - coordinator will shut down)
- `return -1`: Network error, coordinator disconnected

---

### Progress Reporting

#### dist_worker_report_done

```c
int dist_worker_report_done(dist_worker_client_t *client,
                            uint64_t keys_processed,
                            uint64_t elapsed_ms);
```

**Return Values:**

| Code | Meaning |
|------|---------|
| `0` | Work completion acknowledged |
| `-1` | Communication error |

**Example:**
```c
uint64_t start_time = get_timestamp_ms();
/* ... process work unit ... */
uint64_t elapsed = get_timestamp_ms() - start_time;

if (dist_worker_report_done(&worker, keys_processed, elapsed) != 0) {
    fprintf(stderr, "Failed to report work completion\n");
    /* Work will be reassigned after timeout */
}
```

---

#### dist_worker_heartbeat

```c
int dist_worker_heartbeat(dist_worker_client_t *client,
                          uint64_t keys_since_last);
```

**Return Values:**

| Code | Meaning |
|------|---------|
| `0` | Heartbeat acknowledged |
| `-1` | Communication error (connection lost) |

**Example:**
```c
/* Send heartbeat every 30 seconds during work processing */
uint64_t last_heartbeat = 0;
uint64_t keys_since_heartbeat = 0;

while (processing) {
    /* ... check keys ... */
    keys_since_heartbeat++;

    if (time(NULL) - last_heartbeat >= 30) {
        if (dist_worker_heartbeat(&worker, keys_since_heartbeat) != 0) {
            fprintf(stderr, "Lost connection to coordinator\n");
            break;
        }
        last_heartbeat = time(NULL);
        keys_since_heartbeat = 0;
    }
}
```

**Importance:**
- Prevents coordinator from assuming worker crashed
- Allows coordinator to track real-time progress
- Workers that miss 3+ heartbeats are marked as dead
- Work from dead workers is reassigned

---

#### dist_worker_report_found

```c
int dist_worker_report_found(dist_worker_client_t *client,
                             const char *private_key,
                             const char *address);
```

**Return Values:**

| Code | Meaning |
|------|---------|
| `0` | Key found report acknowledged |
| `-1` | Communication error |

**Example:**
```c
if (key_matches_target) {
    char private_key[65];
    char address[36];
    /* ... format key and address ... */

    if (dist_worker_report_found(&worker, private_key, address) != 0) {
        fprintf(stderr, "Failed to report found key\n");
        /* Save locally as backup */
        save_key_to_file(private_key, address, "KEYFOUNDKEYFOUND.txt");
    }

    printf("Key found and reported to coordinator!\n");
}
```

**Critical Notes:**
- **ALWAYS save locally** before or after reporting
- Network failure doesn't mean key is lost if saved locally
- Coordinator will broadcast to all workers when key is found

---

#### dist_worker_leave

```c
int dist_worker_leave(dist_worker_client_t *client, const char *reason);
```

**Return Values:**

| Code | Meaning |
|------|---------|
| `0` | Graceful departure acknowledged |
| `-1` | Communication error (still safe to disconnect) |

**Example:**
```c
/* User pressed Ctrl+C or requested shutdown */
if (dist_worker_leave(&worker, "User requested shutdown") != 0) {
    fprintf(stderr, "Warning: Could not notify coordinator of departure\n");
    /* Continue with disconnect anyway */
}
dist_worker_disconnect(&worker);
```

**Benefits of Graceful Departure:**
- Coordinator immediately reassigns work (no timeout wait)
- Cleaner logs and statistics
- Preserves current position for resume

---

### Configuration Getters

#### dist_worker_get_job_config

```c
int dist_worker_get_job_config(const dist_worker_client_t *client,
                               char *target_address,
                               char *mode,
                               char *key_type);
```

**Return Values:**

| Code | Meaning |
|------|---------|
| `0` | Job configuration retrieved |
| `-1` | Not connected or no configuration received |

**Example:**
```c
char target[64], mode[32], keytype[16];

if (dist_worker_get_job_config(&worker, target, mode, keytype) != 0) {
    fprintf(stderr, "No job configuration available\n");
    exit(1);
}

printf("Target: %s\n", target);
printf("Mode: %s\n", mode);
printf("Key type: %s\n", keytype);
```

---

#### dist_worker_get_heartbeat_interval

```c
int dist_worker_get_heartbeat_interval(const dist_worker_client_t *client);
```

**Return Values:**

| Value | Meaning |
|-------|---------|
| `> 0` | Heartbeat interval in seconds |
| `30` | Default interval (if not configured) |

**Example:**
```c
int interval = dist_worker_get_heartbeat_interval(&worker);
printf("Heartbeat every %d seconds\n", interval);

/* Use in heartbeat loop */
while (processing) {
    /* ... work ... */
    if (elapsed_time >= interval) {
        dist_worker_heartbeat(&worker, keys_since_last);
    }
}
```

---

### TLS Functions

#### dist_worker_enable_tls

```c
int dist_worker_enable_tls(dist_worker_client_t *client, bool verify_server);
```

**Return Values:**

| Code | Meaning |
|------|---------|
| `0` | TLS enabled for client |
| `-1` | TLS not available (not compiled with HAVE_OPENSSL) |

**Example:**
```c
/* Enable TLS with server verification */
if (dist_worker_enable_tls(&worker, true) != 0) {
    fprintf(stderr, "Failed to enable TLS\n");
    fprintf(stderr, "Ensure keyhunt was built with: make ENABLE_TLS=1\n");
    exit(1);
}

/* Or enable TLS without server verification (testing only) */
if (dist_worker_enable_tls(&worker, false) != 0) {
    fprintf(stderr, "TLS not available\n");
    exit(1);
}
```

**Security Notes:**
- `verify_server = true`: Verify coordinator certificate (production)
- `verify_server = false`: Skip verification (testing only, insecure)
- Must call BEFORE `dist_worker_connect()`

---

## Federation Functions

### Federation Initialization

#### dist_federation_init_primary

```c
int dist_federation_init_primary(dist_coordinator_t *coordinator,
                                 int federation_port);
```

**Return Values:**

| Code | Meaning |
|------|---------|
| `0` | Primary federation initialized |
| `-1` | Federation initialization failed |

**Example:**
```c
/* Initialize as primary coordinator for federation */
if (dist_federation_init_primary(&coord, 7778) != 0) {
    fprintf(stderr, "Failed to initialize primary federation\n");
    exit(1);
}
printf("Primary coordinator ready for federation on port 7778\n");
```

**Use Case:**
- Multi-datacenter distributed computing
- Load balancing across geographic regions
- Hierarchical work distribution

---

#### dist_federation_init_secondary

```c
int dist_federation_init_secondary(dist_coordinator_t *coordinator,
                                   const char *primary_host,
                                   int primary_port);
```

**Return Values:**

| Code | Meaning |
|------|---------|
| `0` | Secondary federation initialized |
| `-1` | Federation initialization failed |

**Example:**
```c
/* Initialize as secondary coordinator connecting to primary */
if (dist_federation_init_secondary(&coord, "primary.example.com", 7778) != 0) {
    fprintf(stderr, "Failed to initialize secondary federation\n");
    exit(1);
}
printf("Secondary coordinator configured\n");
```

---

#### dist_federation_add_peer

```c
int dist_federation_add_peer(dist_coordinator_t *coordinator,
                             const char *host,
                             int port);
```

**Return Values:**

| Code | Meaning |
|------|---------|
| `>= 0` | Peer index (peer added successfully) |
| `-1` | Failed to add peer (max peers reached or invalid) |

**Example:**
```c
/* Add secondary coordinators to federation */
int peer1 = dist_federation_add_peer(&coord, "worker-dc1.example.com", 7777);
int peer2 = dist_federation_add_peer(&coord, "worker-dc2.example.com", 7777);

if (peer1 < 0 || peer2 < 0) {
    fprintf(stderr, "Failed to add federation peers\n");
    exit(1);
}
printf("Added %d federation peers\n", 2);
```

**Limitations:**
- Maximum 8 peers (DIST_MAX_FEDERATION)
- Exceeding limit returns -1

---

#### dist_federation_process

```c
int dist_federation_process(dist_coordinator_t *coordinator,
                            int timeout_ms);
```

**Return Values:**

| Code | Meaning |
|------|---------|
| `0` | Normal operation |
| `1` | Result found by peer (key discovered) |
| `-1` | Error processing federation |

**Example:**
```c
while (1) {
    int status = dist_federation_process(&coord, 1000);
    if (status == 1) {
        printf("Key found by federation peer!\n");
        break;
    } else if (status < 0) {
        fprintf(stderr, "Federation error\n");
        break;
    }
}
```

---

#### dist_federation_share_result

```c
int dist_federation_share_result(dist_coordinator_t *coordinator,
                                 const char *private_key,
                                 const char *address);
```

**Return Values:**

| Code | Meaning |
|------|---------|
| `0` | Result shared with all peers |
| `-1` | Failed to share (network error) |

**Example:**
```c
/* When coordinator finds a key, share with federation */
if (key_found) {
    if (dist_federation_share_result(&coord, private_key, address) != 0) {
        fprintf(stderr, "Warning: Could not share result with peers\n");
        /* Non-fatal - key is still found locally */
    }
}
```

---

## Error Handling Best Practices

### 1. Always Check Return Values

**❌ Bad:**
```c
dist_coordinator_init(&coord, 7777);
dist_coordinator_start(&coord);  /* May fail if init failed */
```

**✅ Good:**
```c
if (dist_coordinator_init(&coord, 7777) != 0) {
    fprintf(stderr, "Initialization failed\n");
    exit(1);
}

if (dist_coordinator_start(&coord) != 0) {
    fprintf(stderr, "Failed to start coordinator\n");
    exit(1);
}
```

### 2. Distinguish Between Fatal and Non-Fatal Errors

**Fatal errors** (must exit):
- Initialization failures
- Port binding failures
- TLS setup failures (if TLS required)

**Non-fatal errors** (can continue):
- State save failures (log warning, continue)
- Individual worker connection errors (coordinator continues)
- Heartbeat failures (worker reconnects)

**Example:**
```c
/* Fatal error */
if (dist_coordinator_start(&coord) != 0) {
    fprintf(stderr, "FATAL: Cannot start coordinator\n");
    exit(1);
}

/* Non-fatal error */
if (dist_coordinator_save_state(&coord, "state.json") != 0) {
    fprintf(stderr, "WARNING: Could not save state (continuing)\n");
    /* Continue operation */
}
```

### 3. Provide Actionable Error Messages

**❌ Bad:**
```c
if (dist_coordinator_enable_tls(&coord, cert, key) != 0) {
    fprintf(stderr, "TLS failed\n");
    exit(1);
}
```

**✅ Good:**
```c
if (dist_coordinator_enable_tls(&coord, cert, key) != 0) {
    fprintf(stderr, "ERROR: Failed to enable TLS\n");
    fprintf(stderr, "Possible causes:\n");
    fprintf(stderr, "  1. keyhunt not built with TLS: make ENABLE_TLS=1\n");
    fprintf(stderr, "  2. Certificate file not found: %s\n", cert);
    fprintf(stderr, "  3. Private key file not found: %s\n", key);
    fprintf(stderr, "  4. Certificate/key mismatch\n");
    exit(1);
}
```

### 4. Handle "No Work Available" Gracefully

```c
while (1) {
    char range_start[65], range_end[65];
    int status = dist_worker_request_work(&worker, range_start, range_end);

    if (status == 0) {
        /* Work assigned - process it */
        process_work_unit(range_start, range_end);
        dist_worker_report_done(&worker, keys_processed, elapsed_ms);
    } else if (status == 1) {
        /* No work available */
        printf("No work available, waiting...\n");
        sleep(30);  /* Wait before retrying */

        /* Optional: Check if all work is done */
        if (all_work_complete) {
            printf("All work completed by cluster\n");
            break;
        }
    } else {
        /* Error - reconnect or exit */
        fprintf(stderr, "Communication error, attempting reconnect\n");
        dist_worker_disconnect(&worker);
        sleep(5);
        if (dist_worker_connect(&worker) != 0) {
            fprintf(stderr, "Reconnection failed, exiting\n");
            exit(1);
        }
    }
}
```

### 5. Save Found Keys Locally Before Reporting

```c
/* CRITICAL: Always save locally first! */
if (key_matches_target) {
    /* Save locally FIRST (in case network fails) */
    save_key_to_file(private_key, address, "KEYFOUNDKEYFOUND.txt");
    printf("Key saved locally\n");

    /* Then report to coordinator (best effort) */
    if (dist_worker_report_found(&worker, private_key, address) != 0) {
        fprintf(stderr, "WARNING: Could not report key to coordinator\n");
        fprintf(stderr, "Key is safe in local file: KEYFOUNDKEYFOUND.txt\n");
    } else {
        printf("Key reported to coordinator successfully\n");
    }
}
```

---

## Platform Support

### Linux/POSIX

All distributed functions are fully supported on Linux and POSIX-compliant systems.

**Required headers:**
- `<sys/socket.h>` - Network sockets
- `<poll.h>` - Event polling
- `<netinet/in.h>` - Internet addresses
- `<arpa/inet.h>` - IP address conversion

### Windows

Distributed mode is **NOT supported** on Windows.

All distributed functions return `-1` with error message:
```
[distributed] Not supported on Windows
```

**Workaround:**
- Use WSL2 (Windows Subsystem for Linux)
- Use Docker container on Windows
- Use virtual machine with Linux

**Example:**
```bash
# Run in WSL2
wsl
cd /mnt/c/Users/YourName/keyhunt
make
./keyhunt --wizard
```

---

## Return Code Summary Table

| Function | Success | Special | Error | Notes |
|----------|---------|---------|-------|-------|
| `dist_coordinator_check_port` | 0 | 1 (in use) | -1, -2 | -2 = invalid address |
| `dist_coordinator_init` | 0 | - | -1 | -1 on Windows |
| `dist_coordinator_set_range` | > 0 | - | -1 | Returns unit count |
| `dist_coordinator_start` | 0 | - | -1 | Starts listening |
| `dist_coordinator_process` | 0 | 1 (done) | -1 | Main event loop |
| `dist_coordinator_save_state` | 0 | - | -1 | Non-fatal failure |
| `dist_coordinator_load_state` | 0 | 1 (no file) | -1 | 1 = not found |
| `dist_coordinator_enable_tls` | 0 | - | -1 | Requires OpenSSL |
| `dist_worker_init` | 0 | - | -1 | -1 on Windows |
| `dist_worker_connect` | 0 | - | -1 | Network/auth error |
| `dist_worker_request_work` | 0 | 1 (no work) | -1 | 1 = temporary |
| `dist_worker_report_done` | 0 | - | -1 | Communication error |
| `dist_worker_report_found` | 0 | - | -1 | Save locally first! |
| `dist_worker_heartbeat` | 0 | - | -1 | Connection lost |
| `dist_worker_leave` | 0 | - | -1 | Non-fatal failure |
| `dist_worker_get_job_config` | 0 | - | -1 | Not connected |
| `dist_worker_get_heartbeat_interval` | > 0 | - | - | Never fails |
| `dist_worker_enable_tls` | 0 | - | -1 | Requires OpenSSL |
| `dist_federation_init_primary` | 0 | - | -1 | Federation setup |
| `dist_federation_init_secondary` | 0 | - | -1 | Federation setup |
| `dist_federation_add_peer` | >= 0 | - | -1 | Returns peer index |
| `dist_federation_connect` | 0 | - | -1 | Connect to primary |
| `dist_federation_process` | 0 | 1 (found) | -1 | Federation events |
| `dist_federation_share_result` | 0 | - | -1 | Broadcast result |

---

## Cross-References

### Related Documentation

- [Distributed Mode Overview](../distributed/README.md)
- [Protocol Specification](../distributed/protocol.md)
- [TLS Setup Guide](../distributed/tls-setup.md)
- [Federation Guide](../distributed/federation.md)
- [CLI Options Reference](./cli-options.md)
- [Troubleshooting Guide](../distributed/troubleshooting.md)

### Code Examples

- [Simple Coordinator](../examples/coordinator-simple.c)
- [Simple Worker](../examples/worker-simple.c)
- [Coordinator with TLS](../examples/coordinator-tls.c)
- [Federation Example](../examples/federation.c)

### API Headers

- [distributed.h](../../../src/distributed/distributed.h) - Complete API reference

---

## Version History

| Version | Changes |
|---------|---------|
| 0.2.x | Added TLS support, federation, rate limiting |
| 0.1.x | Initial distributed mode implementation |

---

**Last Updated:** 2026-02-28
**Maintained By:** keyhunt development team
