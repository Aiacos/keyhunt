# Client Setup Guide

This guide covers configuring keyhunt worker clients to connect to a distributed search coordinator.

## Prerequisites

- Linux system with keyhunt built
- Network access to coordinator server
- Same target file as server (or auto-download)

## Quick Start with Wizard

The easiest way to set up a client:

```bash
./keyhunt --wizard
```

Select:
1. Choose **Client** mode
2. Enter server IP address
3. Enter server port (default: 2222)
4. Configuration is auto-detected

## Manual Client Setup

### Basic Client Command

```bash
./keyhunt --client --server-ip 192.168.1.100 --port 2222
```

### Client Options

| Option | Description | Default |
|--------|-------------|---------|
| `--client` | Enable client mode | - |
| `--server-ip IP` | Coordinator IP address | Required |
| `--port N` | Coordinator port | 2222 |
| `--client-name NAME` | Client identifier | hostname |

## Auto-Configuration

Clients automatically detect local hardware:

```
[+] Hardware Detection:
    CPU: AMD Ryzen 9 5900X (12 cores, 24 threads)
    RAM: 32 GB (28 GB available)
    GPU: NVIDIA RTX 3080 (CUDA 11.4)
    SIMD: AVX2, SHA-NI

[+] Recommended Configuration:
    Threads: 24
    GPU Mode: hybrid
    Batch Size: 1024
```

The client reports these capabilities to the server.

## Connection Process

1. **Connect**: Client establishes TCP connection to server
2. **Register**: Client sends hardware capabilities
3. **Receive Target**: Server sends target file hash
4. **Verify Target**: Client ensures matching target file
5. **Request Work**: Client requests first work unit
6. **Search Loop**: Search, report progress, request more work

```
Client                              Server
  │                                    │
  │──── connect ──────────────────────>│
  │                                    │
  │──── register (capabilities) ──────>│
  │<─── ack (target_hash) ─────────────│
  │                                    │
  │──── work_request ─────────────────>│
  │<─── work_unit (range) ─────────────│
  │                                    │
  │     [... search ...]               │
  │                                    │
  │──── progress (speed, checked) ────>│
  │                                    │
  │──── work_complete ────────────────>│
  │──── work_request ─────────────────>│
  │<─── work_unit (next range) ────────│
```

## Configuration File

Wizard creates `keyhunt_wizard.json`:

```json
{
  "mode": "client",
  "client": {
    "server_ip": "192.168.1.100",
    "server_port": 2222,
    "client_name": "worker1",
    "auto_reconnect": true,
    "reconnect_delay": 5
  },
  "hardware": {
    "cpu_cores": 24,
    "ram_gb": 32,
    "gpu_available": true,
    "gpu_name": "RTX 3080",
    "simd_features": ["AVX2", "SHA-NI"]
  },
  "search": {
    "threads": 24,
    "gpu_mode": "hybrid",
    "batch_size": 1024
  }
}
```

## Progress Reporting

Clients send progress updates:

```json
{
  "type": "progress",
  "client_name": "worker1",
  "work_unit_id": 42,
  "keys_checked": 1500000000,
  "speed_mkeys": 85.2,
  "percent_complete": 68.5
}
```

Progress is sent every 10 seconds (configurable).

## Key Found Handling

When a key is found:

1. **Immediate Report**: Client sends `key_found` message
2. **Local Save**: Key saved to `KEYFOUNDKEYFOUND.txt`
3. **Server Broadcast**: Server notifies all clients
4. **Optional Stop**: Search can stop or continue

```json
{
  "type": "key_found",
  "client_name": "worker1",
  "private_key": "0x2000000000000000000000001234",
  "address": "13zb1hQbWVsc2S7ZTZnP2G4undNNpdh5so",
  "wif": "KwDiBf89..."
}
```

## Reconnection Handling

Clients automatically reconnect on disconnect:

```
[!] Connection lost to server
[+] Reconnecting in 5 seconds...
[+] Connected to server
[+] Resuming work unit 42 from position 0x200001234...
```

Partial work is not lost - the server tracks work unit assignment.

## Running Multiple Clients

### On Same Machine

Use different client names:

```bash
# Terminal 1
./keyhunt --client --server-ip 192.168.1.100 --client-name worker1a

# Terminal 2
./keyhunt --client --server-ip 192.168.1.100 --client-name worker1b
```

### GPU + CPU Split

One client for GPU, one for CPU:

```bash
# GPU client
./keyhunt --client --server-ip 192.168.1.100 --client-name gpu-worker -G full

# CPU client
./keyhunt --client --server-ip 192.168.1.100 --client-name cpu-worker -G off
```

## Client Display

While running:

```
╔════════════════════════════════════════════════════════════════╗
║                    KEYHUNT DISTRIBUTED CLIENT                   ║
╠════════════════════════════════════════════════════════════════╣
║  Status: Connected                   Server: 192.168.1.100:2222 ║
║  Client: worker1                     Mode: ADDRESS              ║
╠════════════════════════════════════════════════════════════════╣
║  Current Work Unit: #42 of 4096                                 ║
║  Range: 0x20002A00000000000 - 0x20002AFFFFFFFFFFF               ║
║  Progress: [████████████░░░░░░░░] 62.4%                        ║
║  Speed: 85.2 Mkeys/s                                            ║
╠════════════════════════════════════════════════════════════════╣
║  Session Stats:                                                 ║
║    Work Units Completed: 15                                     ║
║    Total Keys Checked: 1.24 Tkeys                              ║
║    Session Time: 4:32:15                                        ║
╚════════════════════════════════════════════════════════════════╝
```

## Running as a Service

### systemd Service File

Create `/etc/systemd/system/keyhunt-client.service`:

```ini
[Unit]
Description=Keyhunt Distributed Client
After=network.target

[Service]
Type=simple
User=keyhunt
WorkingDirectory=/opt/keyhunt
ExecStart=/opt/keyhunt/keyhunt --client --server-ip 192.168.1.100 --port 2222
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

Enable and start:

```bash
sudo systemctl enable keyhunt-client
sudo systemctl start keyhunt-client
```

## Optimizing Client Performance

### Thread Count

Match to available cores:

```bash
./keyhunt --client --server-ip 192.168.1.100 -t $(nproc)
```

### GPU Mode

For NVIDIA GPUs:

```bash
./keyhunt --client --server-ip 192.168.1.100 -G hybrid
```

### NUMA Considerations

For multi-socket servers, bind to specific NUMA node:

```bash
numactl --cpunodebind=0 ./keyhunt --client --server-ip 192.168.1.100
```

## Troubleshooting

### Connection Refused

1. Verify server is running: `nc -zv server-ip 2222`
2. Check firewall on server
3. Confirm correct IP/port

### Slow Speed

1. Check CPU utilization: `htop`
2. Verify SIMD detection at startup
3. Check for thermal throttling

### Frequent Disconnects

1. Check network stability
2. Increase heartbeat interval
3. Check for server overload

## See Also

- [Server Setup](server-setup.md) - Configure coordinator server
- [Troubleshooting](troubleshooting.md) - Common issues
- [CPU Tuning](../optimization/cpu-tuning.md) - Optimize performance
