# Distributed Computing Overview

Keyhunt supports distributed computing to scale searches across multiple machines, dramatically increasing search throughput.

## Architecture

```
                    ┌─────────────────┐
                    │   Coordinator   │
                    │    (Server)     │
                    │  Port: 2222     │
                    └────────┬────────┘
                             │
           ┌─────────────────┼─────────────────┐
           │                 │                 │
    ┌──────▼──────┐   ┌──────▼──────┐   ┌──────▼──────┐
    │   Worker    │   │   Worker    │   │   Worker    │
    │  (Client)   │   │  (Client)   │   │  (Client)   │
    │   16 cores  │   │   32 cores  │   │   GPU       │
    └─────────────┘   └─────────────┘   └─────────────┘
```

## Components

### Coordinator (Server)

The coordinator manages the distributed search:

- **Work Distribution**: Divides the search range into work units
- **Progress Tracking**: Monitors client progress and completion
- **Checkpoint Management**: Saves state for crash recovery
- **Community Integration**: Excludes already-scanned ranges

### Workers (Clients)

Workers perform the actual key search:

- **Auto-Configuration**: Detects local hardware capabilities
- **Work Requests**: Fetches work units from coordinator
- **Progress Reporting**: Sends heartbeat with search progress
- **Key Reporting**: Immediately reports found keys to server

## Communication Protocol

Keyhunt uses a TCP+JSON protocol on port 2222 (configurable):

| Message Type | Direction | Purpose |
|--------------|-----------|---------|
| `register` | Client -> Server | Client announces capabilities |
| `work_request` | Client -> Server | Request next work unit |
| `work_unit` | Server -> Client | Assign range to search |
| `progress` | Client -> Server | Report search progress |
| `key_found` | Client -> Server | Report discovered key |
| `heartbeat` | Bidirectional | Keep connection alive |

## Work Unit Distribution

The search range is divided into work units:

```
Full Range: 0x20000000000000000 - 0x3FFFFFFFFFFFFFFFF (66-bit)

Work Unit Size: 0x100000000000 (configurable)

Work Unit 1: 0x20000000000000000 - 0x200000FFFFFFFFFFF
Work Unit 2: 0x20001000000000000 - 0x200010FFFFFFFFFFF
Work Unit 3: 0x20002000000000000 - 0x200020FFFFFFFFFFF
...
```

Smaller work units = better load balancing but more coordination overhead.

## Features

### Fault Tolerance

- **Checkpointing**: Server saves state every 60 seconds (configurable)
- **Client Recovery**: Clients can disconnect and reconnect
- **Work Reassignment**: Abandoned work units are reassigned
- **Graceful Shutdown**: SIGINT/SIGTERM saves state before exit

### Community Integration

The wizard can fetch already-scanned ranges from BTCPuzzle.info:

```
[+] Fetching community progress from privatekeys.pw...
[+] Excluding 15,234 already-scanned ranges
```

This prevents duplicate work across the community.

### Hybrid Server Mode

The server can also run as a worker:

```
Server Mode: Coordinator + Local Worker
├── Coordinator Thread (distributes work)
└── Worker Thread (searches locally)
```

This maximizes utilization of the server machine.

## Quick Setup

### Using the Wizard (Recommended)

```bash
# On server
./keyhunt --wizard
# Select: Server mode
# Configure port, work unit size, etc.

# On clients
./keyhunt --wizard
# Select: Client mode
# Enter server IP address
```

### Manual Setup

Server:
```bash
./keyhunt -m address -f target.txt -b 66 --server --port 2222
```

Client:
```bash
./keyhunt --client --server-ip 192.168.1.100 --port 2222
```

## Performance Scaling

| Machines | Cores | Expected Throughput |
|----------|-------|---------------------|
| 1 | 16 | 80 Mkeys/s |
| 5 | 80 | 400 Mkeys/s |
| 10 | 160 | 800 Mkeys/s |
| 20 | 320 | 1.6 Gkeys/s |

With GPU workers:
| Machines | GPUs | Expected Throughput |
|----------|------|---------------------|
| 1 | 1x RTX 3080 | 400 Mkeys/s |
| 5 | 5x RTX 3080 | 2 Gkeys/s |
| 10 | 10x RTX 3080 | 4 Gkeys/s |

## Network Requirements

- **Bandwidth**: Minimal (< 1 Kbps per client)
- **Latency**: Tolerant of high latency (work units are large)
- **Firewall**: Open TCP port 2222 on server
- **NAT**: Clients can be behind NAT (outbound connections only)

## Security Considerations

### Network Security

- Communication is **not encrypted** by default
- Use VPN or SSH tunnel for untrusted networks
- Firewall the server port to trusted IPs only

### Trust Model

- Server trusts clients to honestly report progress
- Clients trust server to distribute valid work
- Found keys are reported to server immediately

### Key Storage

Found keys are saved:
- On server: `KEYFOUNDKEYFOUND.txt`, `found_keys.json`
- On client: Local copy also saved

## Limitations

- No built-in encryption (use VPN for security)
- Single coordinator (no HA failover)
- All clients must use same target file
- Work unit size fixed at start

## Next Steps

- [Server Setup](server-setup.md) - Configure the coordinator
- [Client Setup](client-setup.md) - Connect worker clients
- [Protocol](protocol.md) - Technical protocol details
- [Troubleshooting](troubleshooting.md) - Common issues
