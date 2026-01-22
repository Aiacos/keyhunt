# Distributed Protocol Enhancements Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Enhance the keyhunt distributed protocol to support config distribution, detailed hardware reporting, and configurable heartbeat intervals.

**Architecture:** Extend the existing TCP+JSON protocol with new message types. Server sends configuration in `welcome` message. Clients report detailed hardware in `register`. Server configures heartbeat interval. All changes are backwards-compatible with graceful fallbacks.

**Tech Stack:** C, TCP sockets, JSON (hand-rolled), POSIX threads

---

## Task 1: Extend Data Structures for Hardware Info

**Files:**
- Modify: `distributed/distributed.h:47-58` (dist_worker_t)
- Modify: `distributed/distributed.h:107-122` (dist_worker_client_t)

**Step 1: Add hardware fields to dist_worker_t**

```c
/* Worker information (in distributed.h) */
typedef struct {
    int id;
    char hostname[64];
    double perf_score;          /* Performance score from sysinfo */

    /* NEW: Detailed hardware info */
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
} dist_worker_t;
```

**Step 2: Add hardware fields to dist_worker_client_t**

```c
/* Worker client state (in distributed.h) */
typedef struct {
    int socket_fd;
    char coordinator_host[256];
    int coordinator_port;
    char worker_id[64];
    double perf_score;

    /* NEW: Detailed hardware info to report */
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

    /* NEW: Server-configured heartbeat interval */
    int heartbeat_interval_sec;
} dist_worker_client_t;
```

**Step 3: Commit**

```bash
git add distributed/distributed.h
git commit -m "feat(distributed): add hardware info fields to worker structs"
```

---

## Task 2: Add Job Configuration to Coordinator

**Files:**
- Modify: `distributed/distributed.h:80-105` (dist_coordinator_t)

**Step 1: Add job config fields to coordinator struct**

```c
/* Coordinator state (in distributed.h) */
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

    /* NEW: Job configuration (sent to workers) */
    char job_target_address[64];    /* Target address/hash */
    char job_mode[32];              /* "address", "bsgs", "xpoint" */
    char job_key_type[16];          /* "compress", "uncompress", "both" */
    int job_puzzle_number;          /* Puzzle number (informational) */
    int job_bits;                   /* Bit range (informational) */
    int heartbeat_interval_sec;     /* How often workers should heartbeat */
} dist_coordinator_t;
```

**Step 2: Commit**

```bash
git add distributed/distributed.h
git commit -m "feat(distributed): add job config fields to coordinator"
```

---

## Task 3: Add API Functions for Config Distribution

**Files:**
- Modify: `distributed/distributed.h` (add new function declarations)

**Step 1: Add new function declarations**

```c
/* Add after dist_coordinator_set_range declaration */

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
 * @param interval_sec Seconds between heartbeats (0 = disabled)
 */
void dist_coordinator_set_heartbeat_interval(dist_coordinator_t *coordinator,
                                             int interval_sec);

/* Add to worker client section */

/**
 * Initialize worker client with detailed hardware info
 * @param client Output client state
 * @param coordinator_host Coordinator hostname/IP
 * @param coordinator_port Coordinator port (0 = default)
 * @param sysinfo System info structure with hardware details
 * @return 0 on success, -1 on error
 */
int dist_worker_init_with_hardware(dist_worker_client_t *client,
                                   const char *coordinator_host,
                                   int coordinator_port,
                                   const struct system_info *sysinfo);

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
```

**Step 2: Commit**

```bash
git add distributed/distributed.h
git commit -m "feat(distributed): add API functions for config distribution"
```

---

## Task 4: Implement Enhanced Register Message (Client Side)

**Files:**
- Modify: `distributed/distributed.c` (dist_worker_connect function)

**Step 1: Update register message to include hardware details**

Find the `dist_worker_connect` function and update the register message:

```c
/* In dist_worker_connect(), update the register message construction */
static int send_register_message(dist_worker_client_t *client) {
    char msg[DIST_MAX_MSG_SIZE];
    snprintf(msg, sizeof(msg),
        "{"
        "\"type\":\"register\","
        "\"id\":\"%s\","
        "\"hostname\":\"%s\","
        "\"perf_score\":%.2f,"
        "\"cpu_cores\":%d,"
        "\"cpu_threads\":%d,"
        "\"cpu_name\":\"%s\","
        "\"gpu_name\":\"%s\","
        "\"gpu_memory_mb\":%d,"
        "\"cpu_speed_mkeys\":%.2f,"
        "\"gpu_speed_mkeys\":%.2f"
        "}",
        client->worker_id,
        client->worker_id,  /* hostname = worker_id for now */
        client->perf_score,
        client->cpu_cores,
        client->cpu_threads,
        client->cpu_name,
        client->gpu_name,
        client->gpu_memory_mb,
        client->cpu_speed_mkeys,
        client->gpu_speed_mkeys
    );
    return send_message(client->socket_fd, msg);
}
```

**Step 2: Implement dist_worker_init_with_hardware**

```c
int dist_worker_init_with_hardware(dist_worker_client_t *client,
                                   const char *coordinator_host,
                                   int coordinator_port,
                                   const system_info_t *sysinfo) {
    if (!client || !coordinator_host) return -1;

    memset(client, 0, sizeof(*client));
    strncpy(client->coordinator_host, coordinator_host, sizeof(client->coordinator_host) - 1);
    client->coordinator_port = coordinator_port > 0 ? coordinator_port : DIST_DEFAULT_PORT;
    client->socket_fd = -1;

    /* Generate worker ID */
    char hostname[64];
    gethostname(hostname, sizeof(hostname));
    snprintf(client->worker_id, sizeof(client->worker_id), "%s-%d", hostname, getpid());

    /* Copy hardware info from sysinfo */
    if (sysinfo) {
        client->perf_score = sysinfo->cpu_score + sysinfo->gpu_score;
        client->cpu_cores = sysinfo->cpu_physical_cores;
        client->cpu_threads = sysinfo->cpu_logical_cores;
        strncpy(client->cpu_name, sysinfo->cpu_model, sizeof(client->cpu_name) - 1);
        strncpy(client->gpu_name, sysinfo->gpu_name, sizeof(client->gpu_name) - 1);
        client->gpu_memory_mb = (int)sysinfo->gpu_memory;
        client->cpu_speed_mkeys = 0;  /* Will be measured at runtime */
        client->gpu_speed_mkeys = 0;  /* Will be measured at runtime */
    }

    /* Default heartbeat interval (may be overridden by server) */
    client->heartbeat_interval_sec = 30;

    return 0;
}
```

**Step 3: Commit**

```bash
git add distributed/distributed.c
git commit -m "feat(distributed): implement hardware info in register message"
```

---

## Task 5: Implement Enhanced Welcome Message (Server Side)

**Files:**
- Modify: `distributed/distributed.c` (handle_new_worker and welcome message)

**Step 1: Update welcome message to include job config**

```c
/* In the coordinator's welcome message handler */
static int send_welcome_message(dist_coordinator_t *coord, int worker_id, int socket_fd) {
    char msg[DIST_MAX_MSG_SIZE];
    snprintf(msg, sizeof(msg),
        "{"
        "\"type\":\"welcome\","
        "\"worker_id\":%d,"
        "\"work_units\":%d,"
        "\"target_address\":\"%s\","
        "\"mode\":\"%s\","
        "\"key_type\":\"%s\","
        "\"puzzle_number\":%d,"
        "\"bits\":%d,"
        "\"heartbeat_interval\":%d"
        "}",
        worker_id,
        coord->work_unit_count,
        coord->job_target_address,
        coord->job_mode,
        coord->job_key_type,
        coord->job_puzzle_number,
        coord->job_bits,
        coord->heartbeat_interval_sec > 0 ? coord->heartbeat_interval_sec : 30
    );
    return send_message(socket_fd, msg);
}
```

**Step 2: Parse hardware info from register message**

```c
/* In handle_register_message() */
static void parse_worker_hardware(dist_worker_t *worker, const char *json) {
    /* Parse new hardware fields */
    worker->cpu_cores = json_get_int(json, "cpu_cores", 0);
    worker->cpu_threads = json_get_int(json, "cpu_threads", 0);
    json_get_string(json, "cpu_name", worker->cpu_name, sizeof(worker->cpu_name));
    json_get_string(json, "gpu_name", worker->gpu_name, sizeof(worker->gpu_name));
    worker->gpu_memory_mb = json_get_int(json, "gpu_memory_mb", 0);
    worker->cpu_speed_mkeys = json_get_double(json, "cpu_speed_mkeys", 0);
    worker->gpu_speed_mkeys = json_get_double(json, "gpu_speed_mkeys", 0);

    /* Log hardware info */
    printf("[Coordinator] Worker %d hardware: %s (%d cores), GPU: %s (%d MB)\n",
           worker->id,
           worker->cpu_name[0] ? worker->cpu_name : "unknown",
           worker->cpu_threads,
           worker->gpu_name[0] ? worker->gpu_name : "none",
           worker->gpu_memory_mb);
}
```

**Step 3: Commit**

```bash
git add distributed/distributed.c
git commit -m "feat(distributed): implement job config in welcome message"
```

---

## Task 6: Implement Config Setter Functions

**Files:**
- Modify: `distributed/distributed.c`

**Step 1: Implement dist_coordinator_set_job_config**

```c
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

    printf("[Coordinator] Job config set: puzzle #%d (%d bits), mode=%s, target=%s\n",
           puzzle_number, bits, mode, target_address);
}
```

**Step 2: Implement dist_coordinator_set_heartbeat_interval**

```c
void dist_coordinator_set_heartbeat_interval(dist_coordinator_t *coordinator,
                                             int interval_sec) {
    if (!coordinator) return;
    coordinator->heartbeat_interval_sec = interval_sec > 0 ? interval_sec : 30;
    printf("[Coordinator] Heartbeat interval set to %d seconds\n",
           coordinator->heartbeat_interval_sec);
}
```

**Step 3: Commit**

```bash
git add distributed/distributed.c
git commit -m "feat(distributed): implement coordinator config setter functions"
```

---

## Task 7: Implement Client Config Getters

**Files:**
- Modify: `distributed/distributed.c`

**Step 1: Add config storage to client struct and parse welcome**

```c
/* Add to dist_worker_client_t in .h file first:
    char received_target_address[64];
    char received_mode[32];
    char received_key_type[16];
    int received_puzzle_number;
    int received_bits;
*/

/* In dist_worker_connect(), parse welcome message */
static int parse_welcome_config(dist_worker_client_t *client, const char *json) {
    /* Parse job config from welcome */
    json_get_string(json, "target_address", client->received_target_address,
                    sizeof(client->received_target_address));
    json_get_string(json, "mode", client->received_mode, sizeof(client->received_mode));
    json_get_string(json, "key_type", client->received_key_type, sizeof(client->received_key_type));
    client->received_puzzle_number = json_get_int(json, "puzzle_number", 0);
    client->received_bits = json_get_int(json, "bits", 0);
    client->heartbeat_interval_sec = json_get_int(json, "heartbeat_interval", 30);

    if (client->received_target_address[0]) {
        printf("[Worker] Received job config: puzzle #%d, mode=%s, target=%.20s...\n",
               client->received_puzzle_number,
               client->received_mode,
               client->received_target_address);
    }

    return 0;
}
```

**Step 2: Implement getter functions**

```c
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
```

**Step 3: Commit**

```bash
git add distributed/distributed.c distributed/distributed.h
git commit -m "feat(distributed): implement client config getters"
```

---

## Task 8: Update Wizard Server to Set Job Config

**Files:**
- Modify: `wizard/wizard_server.c`

**Step 1: Call config setters after coordinator init**

```c
/* In wizard_server_run(), after dist_coordinator_init() */

/* Set job configuration for workers */
dist_coordinator_set_job_config(&coord,
    cfg->target_address,
    cfg->mode,
    cfg->key_type,
    cfg->puzzle_number,
    cfg->bits);

/* Set heartbeat interval (30 seconds default) */
dist_coordinator_set_heartbeat_interval(&coord, 30);
```

**Step 2: Commit**

```bash
git add wizard/wizard_server.c
git commit -m "feat(wizard): server sends job config to workers"
```

---

## Task 9: Update Wizard Client to Use Received Config

**Files:**
- Modify: `wizard/wizard_client.c`

**Step 1: Use dist_worker_init_with_hardware**

```c
/* In wizard_client_run(), replace dist_worker_init with hardware version */
dist_worker_client_t client;
if (dist_worker_init_with_hardware(&client, cfg->server_host, cfg->server_port,
                                    &sysinfo) != 0) {
    printf("[-] Failed to initialize worker client\n");
    return -1;
}
```

**Step 2: Use received config if available**

```c
/* After dist_worker_connect() succeeds */
char server_target[64], server_mode[32], server_key_type[16];
if (dist_worker_get_job_config(&client, server_target, server_mode, server_key_type) == 0) {
    printf("[+] Using configuration from server:\n");
    printf("    Target: %s\n", server_target);
    printf("    Mode: %s\n", server_mode);
    printf("    Key type: %s\n", server_key_type);

    /* Override local config with server config */
    strncpy(cfg->target_address, server_target, sizeof(cfg->target_address) - 1);
    strncpy(cfg->mode, server_mode, sizeof(cfg->mode) - 1);
    strncpy(cfg->key_type, server_key_type, sizeof(cfg->key_type) - 1);
} else {
    printf("[+] Using local configuration (server didn't provide config)\n");
}

/* Use server's heartbeat interval */
int heartbeat_interval = dist_worker_get_heartbeat_interval(&client);
printf("[+] Heartbeat interval: %d seconds\n", heartbeat_interval);
```

**Step 3: Update heartbeat loop to use configured interval**

```c
/* In the main work loop, replace hardcoded 30 */
if (now - last_heartbeat >= heartbeat_interval) {
    dist_worker_heartbeat(&client, keys_checked);
    last_heartbeat = now;
}
```

**Step 4: Commit**

```bash
git add wizard/wizard_client.c
git commit -m "feat(wizard): client uses server-provided config"
```

---

## Task 10: Add sysinfo Fields for GPU Name

**Files:**
- Modify: `sysinfo.h`
- Modify: `sysinfo.c`

**Step 1: Add gpu_name field to system_info_t**

```c
/* In sysinfo.h, add to system_info_t struct */
char gpu_name[64];          /* GPU model name */
uint64_t gpu_memory;        /* GPU memory in MB */
```

**Step 2: Implement GPU detection in sysinfo.c**

```c
/* Add GPU detection function */
static void detect_gpu_info(system_info_t *info) {
    info->gpu_name[0] = '\0';
    info->gpu_memory = 0;

    /* Try nvidia-smi for NVIDIA GPUs */
    FILE *fp = popen("nvidia-smi --query-gpu=name,memory.total --format=csv,noheader,nounits 2>/dev/null", "r");
    if (fp) {
        char line[256];
        if (fgets(line, sizeof(line), fp)) {
            /* Parse: "GeForce RTX 2080, 8192" */
            char *comma = strchr(line, ',');
            if (comma) {
                *comma = '\0';
                strncpy(info->gpu_name, line, sizeof(info->gpu_name) - 1);
                info->gpu_memory = strtoull(comma + 1, NULL, 10);
            }
        }
        pclose(fp);
    }
}
```

**Step 3: Call in sysinfo_init**

```c
/* In sysinfo_init(), add call to detect_gpu_info */
detect_gpu_info(info);
```

**Step 4: Commit**

```bash
git add sysinfo.h sysinfo.c
git commit -m "feat(sysinfo): add GPU name and memory detection"
```

---

## Task 11: Integration Testing

**Step 1: Build and verify compilation**

```bash
./build_cuda.sh 2>&1 | tail -20
```

**Step 2: Test server mode with new config distribution**

```bash
# Terminal 1: Start server
timeout 30 ./keyhunt --wizard <<EOF
1
1
7777
y

EOF
```

**Step 3: Test client mode receiving config**

```bash
# Terminal 2: Start client (in another terminal)
timeout 20 ./keyhunt --wizard <<EOF
2
localhost
7777
EOF
```

**Step 4: Verify logs show**
- Server: "Job config set: puzzle #71..."
- Server: "Worker X hardware: ... (N cores), GPU: ..."
- Client: "Received job config: puzzle #71, mode=address..."
- Client: "Heartbeat interval: 30 seconds"

**Step 5: Final commit**

```bash
git add -A
git commit -m "feat(distributed): complete protocol enhancements

- Config distribution: server sends target/mode/key_type to clients
- Hardware reporting: clients send CPU cores/threads/name, GPU name/memory
- Configurable heartbeat: server sets interval, clients use it
- Backwards compatible: missing fields use sensible defaults"
```

---

## Summary of Changes

| File | Changes |
|------|---------|
| `distributed/distributed.h` | New fields in worker structs, new API functions |
| `distributed/distributed.c` | Enhanced register/welcome messages, config setters/getters |
| `wizard/wizard_server.c` | Call config setters before starting |
| `wizard/wizard_client.c` | Use hardware init, apply received config |
| `sysinfo.h` | Add gpu_name, gpu_memory fields |
| `sysinfo.c` | Implement GPU detection via nvidia-smi |

## Protocol Changes

**Enhanced `register` message (client → server):**
```json
{
  "type": "register",
  "id": "hostname-pid",
  "hostname": "worker-machine",
  "perf_score": 42.5,
  "cpu_cores": 8,
  "cpu_threads": 16,
  "cpu_name": "AMD Ryzen 7 5800X",
  "gpu_name": "NVIDIA GeForce RTX 3080",
  "gpu_memory_mb": 10240,
  "cpu_speed_mkeys": 0,
  "gpu_speed_mkeys": 0
}
```

**Enhanced `welcome` message (server → client):**
```json
{
  "type": "welcome",
  "worker_id": 0,
  "work_units": 100000,
  "target_address": "1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU",
  "mode": "address",
  "key_type": "compress",
  "puzzle_number": 71,
  "bits": 71,
  "heartbeat_interval": 30
}
```
