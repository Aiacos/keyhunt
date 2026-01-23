# Distributed Computing Guide

> **Recommended**: Use the [Interactive Wizard](04-Interactive-Wizard.md) for easy distributed setup!
>
> ```bash
> ./keyhunt --wizard    # Guides you through setup automatically
> ```

## Overview

Keyhunt supports distributed computing to parallelize search across multiple machines. This is essential for large puzzles where single-machine search is impractical. The distributed mode uses a coordinator/worker architecture with TCP communication and a simple JSON-based protocol.

### When to Use Distributed Mode

- Searching key ranges larger than 2^60
- When you have access to multiple machines
- When a single machine would take months/years
- For coordinated team efforts on Bitcoin puzzles

### Key Features

- **Automatic work distribution**: Coordinator divides range into work units
- **Fault tolerance**: Failed work units are automatically reassigned
- **Real-time monitoring**: Live progress, speed, and ETA tracking
- **Result collection**: Found keys reported to coordinator immediately
- **Scalable**: Supports up to 64 concurrent workers

---

## Architecture

```
                      ┌─────────────────────────────────┐
                      │         COORDINATOR             │
                      │    (puzzle71_coordinator)       │
                      │                                 │
                      │  - Manages work units           │
                      │  - Tracks progress              │
                      │  - Collects results             │
                      │  - Port 7777 (configurable)     │
                      └───────────────┬─────────────────┘
                                      │
                          TCP + JSON Protocol
                                      │
           ┌──────────────────────────┼──────────────────────────┐
           │                          │                          │
           ▼                          ▼                          ▼
    ┌─────────────┐            ┌─────────────┐            ┌─────────────┐
    │  Worker 1   │            │  Worker 2   │            │  Worker N   │
    │   keyhunt   │            │   keyhunt   │            │   keyhunt   │
    │             │            │             │            │             │
    │ - Request   │            │ - Request   │            │ - Request   │
    │   work      │            │   work      │            │   work      │
    │ - Execute   │            │ - Execute   │            │ - Execute   │
    │   search    │            │   search    │            │   search    │
    │ - Report    │            │ - Report    │            │ - Report    │
    │   results   │            │   results   │            │   results   │
    └─────────────┘            └─────────────┘            └─────────────┘
```

### Components

| Component | Description | Location |
|-----------|-------------|----------|
| Coordinator | Central server that manages work distribution | `puzzle71_coordinator.c` |
| Worker Script | Shell script that runs keyhunt with assigned ranges | `puzzle71_worker.sh` |
| Distributed Library | C library for network communication | `distributed/distributed.c` |

---

## Setup Instructions

### Step 1: Build the Coordinator

```bash
cd keyhunt
gcc -o puzzle71_coordinator puzzle71_coordinator.c distributed/distributed.c -lpthread -O2
```

This compiles the coordinator with:
- POSIX thread support (`-lpthread`)
- Optimization enabled (`-O2`)

### Step 2: Prepare Target File

Create a file containing the target Bitcoin address(es):

```bash
# For puzzle 71
echo "1PWCx5fovoEaoBowAvF5k91m2Xat9bMgwb" > puzzle71.txt

# For other puzzles, use the appropriate address
# Puzzle 66: 13zb1hQbWVsc2S7ZTZnP2G4undNNpdh5so
# Puzzle 67: 1BY8GQbnueYofwSuFAT3USAhGjPrkxDdW9
```

### Step 3: Start the Coordinator (Central Server)

On your main server (with a public IP or accessible from all workers):

```bash
./puzzle71_coordinator 7777
```

**Expected output:**
```
+==================================================================+
|           KEYHUNT PUZZLE 71 - COORDINATOR                        |
+==================================================================+

[+] Configuration puzzle 71:
    Range: 0x400000000000000000 - 0x7FFFFFFFFFFFFFFFFF
    Size: 2^70 keys

[Coordinator] Created 274877906944 work units for range...
[+] Created 100000 work units
[+] Each work unit: ~4.3 billion keys

[+] Server listening on port 7777
[+] Waiting for worker connections...
[+] Press Ctrl+C to terminate
-----------------------------------------------------------------
```

### Step 4: Deploy Workers (On Each Machine)

Copy to each worker machine:
1. `keyhunt` binary (compiled with `make`)
2. `puzzle71.txt` target file
3. `puzzle71_worker.sh` script

Make the script executable and start:

```bash
chmod +x puzzle71_worker.sh
./puzzle71_worker.sh <COORDINATOR_IP> 7777
```

**Example:**
```bash
./puzzle71_worker.sh 192.168.1.100 7777
```

**Expected output:**
```
+==================================================================+
|           KEYHUNT PUZZLE 71 - WORKER                             |
+==================================================================+

[+] Worker ID: 12345-hostname
[+] Coordinator: 192.168.1.100:7777
[+] Target file: puzzle71.txt

[+] Requesting work from coordinator...
[+] Work received: 0x400000000000000000 - 0x4000000100000000
[+] Starting search...
```

---

## Protocol Details

The coordinator and workers communicate using TCP sockets with length-prefixed JSON messages.

### Message Format

All messages are prefixed with a 4-byte network-order length, followed by the JSON payload:

```
[4 bytes: length][JSON payload]
```

### Worker Registration

When a worker connects, it sends detailed hardware information. If authentication is enabled, the worker must also include the auth token:

```json
{
  "type": "register",
  "id": "12345-hostname",
  "hostname": "worker-machine",
  "perf_score": 15.5,
  "cpu_cores": 8,
  "cpu_threads": 16,
  "cpu_name": "AMD Ryzen 7 5800X",
  "gpu_name": "NVIDIA GeForce RTX 3080",
  "gpu_memory_mb": 10240,
  "cpu_speed_mkeys": 5.0,
  "gpu_speed_mkeys": 325.0,
  "auth_token": "your-secret-token"
}
```

If the token is invalid, the coordinator responds with:

```json
{
  "type": "auth_failed",
  "message": "Invalid authentication token"
}
```

Coordinator responds with job configuration:

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

The coordinator logs hardware information when workers connect:
```
[Coordinator] Worker 0 connected from hostname (score=15.5)
[Coordinator]   Hardware: AMD Ryzen 7 5800X (16 threads), GPU: NVIDIA GeForce RTX 3080 (10240 MB)
```

### Work Unit Assignment

Worker requests work:

```json
{
  "type": "request_work"
}
```

Coordinator assigns a work unit:

```json
{
  "type": "work_assignment",
  "work_id": 12345,
  "range_start": "400000000000000000",
  "range_end": "4000000100000000"
}
```

Or if no work is available:

```json
{
  "type": "no_work"
}
```

### Result Reporting

When a worker completes a work unit:

```json
{
  "type": "work_done",
  "work_id": 12345,
  "keys_processed": 4294967296,
  "elapsed_ms": 85000
}
```

### Key Found Notification

When a worker finds the target key:

```json
{
  "type": "found",
  "private_key": "4A5B6C7D8E9F...",
  "address": "1PWCx5fovoEaoBowAvF5k91m2Xat9bMgwb"
}
```

### Heartbeat

Workers send periodic heartbeats:

```json
{
  "type": "heartbeat",
  "keys": 100000000
}
```

---

## Work Unit Sizing

Work unit size determines granularity of distribution. The default is `0x100000000` (4G keys).

| Work Unit Size | Keys per Unit | Time per Unit (50 Mkeys/s) | Overhead |
|----------------|---------------|---------------------------|----------|
| `0x10000000` | 256M | ~5 seconds | High |
| `0x100000000` | 4.3B | ~85 seconds | **Recommended** |
| `0x1000000000` | 68.7B | ~23 minutes | Low |
| `0x10000000000` | 1.1T | ~6 hours | Very Low |

### Choosing the Right Size

- **Too small (256M)**: Excessive network overhead, coordinator becomes bottleneck
- **Too large (1T+)**: Poor fault tolerance, long wait if worker fails
- **Optimal (4G)**: Good balance of progress granularity and efficiency

The default 4G keys per unit provides:
- ~85 seconds per unit at 50 Mkeys/s
- Fine-grained progress tracking
- Quick recovery from worker failures
- Minimal network overhead

---

## Fault Tolerance

The distributed system handles failures gracefully:

### Heartbeat Mechanism

- Workers send heartbeats every 30 seconds
- Coordinator tracks `last_heartbeat` timestamp
- Workers not responding are marked disconnected

### Work Reassignment

When a worker disconnects or fails:

1. Coordinator detects missing heartbeat or connection close
2. In-progress work unit is marked as `PENDING`
3. Next available worker receives the reassigned unit
4. No duplicate work, no missed ranges

### Coordinator State

The coordinator maintains:

```c
typedef enum {
    WORK_STATUS_PENDING = 0,    // Not yet assigned
    WORK_STATUS_ASSIGNED,        // Given to a worker
    WORK_STATUS_COMPLETED,       // Successfully finished
    WORK_STATUS_FAILED          // Worker died, needs reassignment
} work_status_t;
```

### Recovery Scenarios

| Scenario | Behavior |
|----------|----------|
| Worker crashes mid-unit | Work unit reassigned to another worker |
| Network timeout | Worker marked disconnected, work reassigned |
| Coordinator restart | Must restart from beginning (no persistence yet) |
| Worker reconnects | Gets new worker ID, resumes requesting work |

---

## Scaling Estimates

### Puzzle 71 (2^70 keys)

| Configuration | Total Speed | Time to Complete |
|---------------|-------------|------------------|
| 1 PC (8 cores, AVX2) | ~50 Mkeys/s | ~720,000 years |
| 10 PCs | ~500 Mkeys/s | ~72,000 years |
| 100 PCs | ~5 Gkeys/s | ~7,200 years |
| 1000 PCs | ~50 Gkeys/s | ~720 years |
| 10,000 PCs | ~500 Gkeys/s | ~72 years |

### Puzzle 66 (2^65 keys, much more feasible)

| Configuration | Total Speed | Time to Complete |
|---------------|-------------|------------------|
| 1 PC (8 cores) | ~50 Mkeys/s | ~23,000 years |
| 100 PCs | ~5 Gkeys/s | ~230 years |
| 1000 PCs | ~50 Gkeys/s | ~23 years |
| GPU cluster (100 GPUs) | ~500 Gkeys/s | ~2.3 years |

### Performance Factors

- **CPU with AVX2**: ~5-8 Mkeys/s per core
- **CPU with AVX-512**: ~10-15 Mkeys/s per core
- **High-end GPU**: ~500 Mkeys/s per GPU
- **Network overhead**: ~1% at 4G work unit size

---

## Network Requirements

### Bandwidth

The protocol is extremely lightweight:

| Traffic Type | Size | Frequency |
|--------------|------|-----------|
| Work request | ~50 bytes | Per work unit (~85s) |
| Work assignment | ~150 bytes | Per work unit |
| Completion report | ~100 bytes | Per work unit |
| Heartbeat | ~50 bytes | Every 30 seconds |

**Total per worker**: ~1 KB/s average

### Latency

- **Acceptable latency**: < 1 second round-trip
- **Optimal latency**: < 100ms (local network)
- **High latency impact**: Minimal, only affects work assignment

### Firewall Configuration

On the coordinator machine:

```bash
# Linux (iptables)
sudo iptables -A INPUT -p tcp --dport 7777 -j ACCEPT

# Linux (firewalld)
sudo firewall-cmd --permanent --add-port=7777/tcp
sudo firewall-cmd --reload

# Ubuntu (ufw)
sudo ufw allow 7777/tcp
```

---

## Security Considerations

### Authentication

Token-based authentication is now supported to prevent unauthorized workers from connecting:

**Configuring authentication in the wizard:**

When setting up a server through the wizard, you can set an authentication token in the configuration file (`keyhunt_wizard.json`):

```json
{
  "auth_token": "your-secret-token-here"
}
```

**How it works:**
- Coordinator validates tokens using constant-time comparison (prevents timing attacks)
- Workers must provide the correct token during registration
- Invalid tokens result in immediate disconnection with `auth_failed` response
- Local clients spawned by the server receive the token via the `KEYHUNT_AUTH_TOKEN` environment variable

### Network Security

1. **Use VPN for public deployments**
   ```bash
   # Workers connect through VPN
   ./puzzle71_worker.sh 10.8.0.1 7777  # VPN coordinator IP
   ```

2. **Firewall rules**: Only allow known worker IPs
   ```bash
   sudo iptables -A INPUT -p tcp --dport 7777 -s 192.168.1.0/24 -j ACCEPT
   sudo iptables -A INPUT -p tcp --dport 7777 -j DROP
   ```

3. **Enable authentication**: Set `auth_token` in your configuration to require workers to authenticate
   - Use a strong, random token (at least 32 characters)
   - Share token securely with authorized workers only

### Result Validation

The coordinator should verify found keys:

1. When `found` message received, validate the private key
2. Generate address from private key
3. Compare with target address
4. Only accept if verification passes

### Transaction Security

If a private key is found:

1. **Do NOT broadcast to public mempool**
   - Front-running bots monitor mempool
   - Your transaction may be stolen

2. **Use private mempool services**:
   - Flashbots Protect (Ethereum)
   - Private relay services
   - Direct miner submission

3. **Sweep immediately**:
   - Prepare transaction in advance
   - Use maximum fee for fast confirmation
   - Transfer to your secure wallet

---

## Monitoring

### Coordinator Display

The coordinator shows real-time statistics:

```
[HH:MM:SS] Workers: 15 | Progress: 1234/100000 (1.23%) | Speed: 750 Mkeys/s | ETA: 45d 12h
```

Fields explained:

| Field | Description |
|-------|-------------|
| `Workers` | Number of currently connected workers |
| `Progress` | Completed work units / total work units |
| `Speed` | Aggregate throughput from all workers |
| `ETA` | Estimated time to complete all work units |

### Worker Statistics

Each worker reports:
- Keys processed per work unit
- Time taken per work unit
- Throughput in Mkeys/s

### Log Messages

```
[Coordinator] Worker 0 connected from hostname (score=1.5)
[Coordinator] Worker 0 disconnected
[Coordinator] FOUND by worker 3: <private_key> -> <address>
[Coordinator] Shutdown complete. 1 results found.
```

---

## Troubleshooting

### Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| Worker can't connect | Firewall blocking | Open port 7777, check firewall rules |
| Connection refused | Coordinator not running | Start coordinator first |
| Worker can't connect | Wrong IP address | Verify coordinator IP is reachable |
| Slow progress | Few workers | Add more worker machines |
| Workers disconnecting | Network instability | Check network, reduce work unit size |
| "No more work" immediately | Range exhausted | Check range configuration |
| High coordinator CPU | Too many workers | Increase work unit size |

### Debugging

**Test connectivity:**
```bash
# From worker machine
nc -zv <COORDINATOR_IP> 7777
```

**Check coordinator logs:**
```bash
./puzzle71_coordinator 7777 2>&1 | tee coordinator.log
```

**Verbose worker output:**
```bash
./puzzle71_worker.sh <IP> 7777 2>&1 | tee worker.log
```

### Worker Script Issues

| Issue | Solution |
|-------|----------|
| `keyhunt not found` | Ensure keyhunt binary is in current directory |
| `puzzle71.txt not found` | Create target file with address |
| Permission denied | Run `chmod +x puzzle71_worker.sh` |
| Timeout errors | Increase timeout in script (default 300s) |

---

## Advanced Configuration

### Custom Work Unit Size

Edit `puzzle71_coordinator.c`:

```c
// Default: 0x100000000 (4G keys)
// Smaller for debugging: 0x10000000 (256M keys)
// Larger for production: 0x1000000000 (68G keys)
int num_units = dist_coordinator_set_range(&coord,
    "400000000000000000",
    "7FFFFFFFFFFFFFFFFF",
    0x1000000000ULL);  // Custom size
```

### Custom Port

```bash
# Coordinator
./puzzle71_coordinator 8888

# Workers
./puzzle71_worker.sh 192.168.1.100 8888
```

### Multiple Puzzles

Run separate coordinators for different puzzles:

```bash
# Terminal 1: Puzzle 66
./puzzle66_coordinator 7766

# Terminal 2: Puzzle 71
./puzzle71_coordinator 7771
```

### Worker Thread Control

The worker script uses all available cores by default:

```bash
# In puzzle71_worker.sh, modify:
timeout 300 ./keyhunt -m address -f "$TARGET_FILE" -r "$START:$END" -t 4 -q
#                                                                    ^
#                                                           Thread count
```

---

## API Reference

### Coordinator Functions

```c
// Initialize coordinator on specified port
int dist_coordinator_init(dist_coordinator_t *coord, int port);

// Set search range and work unit size
int dist_coordinator_set_range(dist_coordinator_t *coord,
                               const char *range_start,
                               const char *range_end,
                               uint64_t work_unit_size);

// Start listening for workers
int dist_coordinator_start(dist_coordinator_t *coord);

// Process events (call in loop)
int dist_coordinator_process(dist_coordinator_t *coord, int timeout_ms);

// Get current statistics
void dist_coordinator_stats(const dist_coordinator_t *coord,
                            int *workers_active,
                            int *work_pending,
                            int *work_completed,
                            double *throughput);

// Shutdown and cleanup
void dist_coordinator_shutdown(dist_coordinator_t *coord);
```

### Worker Client Functions

```c
// Initialize worker client
int dist_worker_init(dist_worker_client_t *client,
                     const char *coordinator_host,
                     int coordinator_port,
                     double perf_score);

// Connect to coordinator
int dist_worker_connect(dist_worker_client_t *client);

// Request work unit
int dist_worker_request_work(dist_worker_client_t *client,
                             char *range_start,
                             char *range_end);

// Report completed work
int dist_worker_report_done(dist_worker_client_t *client,
                            uint64_t keys_processed,
                            uint64_t elapsed_ms);

// Report found key
int dist_worker_report_found(dist_worker_client_t *client,
                             const char *private_key,
                             const char *address);

// Send heartbeat
int dist_worker_heartbeat(dist_worker_client_t *client,
                          uint64_t keys_since_last);

// Disconnect from coordinator
void dist_worker_disconnect(dist_worker_client_t *client);
```

---

## Best Practices

### For Coordinators

1. **Run on reliable hardware**: Server should have stable power and network
2. **Monitor continuously**: Watch for disconnected workers
3. **Back up results**: Save found keys immediately
4. **Use screen/tmux**: Keep coordinator running after logout

```bash
screen -S coordinator
./puzzle71_coordinator 7777
# Detach: Ctrl+A, D
# Reattach: screen -r coordinator
```

### For Workers

1. **Maximize threads**: Use all available CPU cores
2. **Close other applications**: Reduce CPU contention
3. **Stable network**: Use wired connection if possible
4. **Auto-restart**: Use systemd or cron for auto-restart

```bash
# Simple restart loop
while true; do
    ./puzzle71_worker.sh 192.168.1.100 7777
    sleep 10  # Wait before reconnect
done
```

### For Networks

1. **Local network preferred**: Minimize latency
2. **Dedicated subnet**: Isolate search traffic
3. **Redundant coordinator**: Consider backup coordinator

---

## Future Improvements

Planned features for distributed mode:

- [ ] Persistent state (coordinator can restart)
- [ ] TLS encryption for secure communication
- [x] Authentication tokens for workers (implemented)
- [ ] Web-based monitoring dashboard
- [ ] Automatic worker deployment scripts
- [ ] Multi-coordinator federation
- [ ] GPU worker support
