# Server Setup Guide

This guide covers configuring the keyhunt coordinator server for distributed searches.

## Prerequisites

- Linux system with keyhunt built
- Open network port (default: 2222)
- Target file with addresses/public keys
- Sufficient RAM for bloom filters

## Quick Start with Wizard

The easiest way to set up a server:

```bash
./keyhunt --wizard
```

Select:
1. Choose your target puzzle
2. Select **Server** mode
3. Configure port (default: 2222)
4. Set work unit size
5. Enable/disable local worker

The wizard saves configuration to `keyhunt_wizard.json`.

## Manual Server Setup

### Basic Server Command

```bash
./keyhunt -m address -f target.txt -b 66 \
  --server --port 2222 \
  -t 16 -l compress
```

### Server Options

| Option | Description | Default |
|--------|-------------|---------|
| `--server` | Enable server mode | - |
| `--port N` | Listen port | 2222 |
| `--work-unit-size N` | Size of each work unit (hex) | Auto |
| `--checkpoint-interval N` | Seconds between saves | 60 |
| `--server-also-worker` | Server also searches | Enabled |

## Configuration File

The wizard creates `keyhunt_wizard.json`:

```json
{
  "mode": "server",
  "puzzle": {
    "number": 66,
    "address": "13zb1hQbWVsc2S7ZTZnP2G4undNNpdh5so",
    "range_start": "0x20000000000000000",
    "range_end": "0x3FFFFFFFFFFFFFFFF",
    "has_pubkey": false
  },
  "server": {
    "port": 2222,
    "work_unit_size": "0x100000000000",
    "checkpoint_interval": 60,
    "server_also_worker": true
  },
  "search": {
    "mode": "address",
    "threads": 16,
    "key_type": "compress",
    "random": true
  }
}
```

## Work Unit Sizing

Work unit size affects:
- **Too small**: High coordination overhead
- **Too large**: Poor load balancing, long recovery after crash

### Recommended Sizes

| Range Size | Work Unit Size | Units | Reason |
|------------|----------------|-------|--------|
| 60 bits | 0x10000000 | ~4096 | Small range |
| 66 bits | 0x100000000000 | ~4096 | Medium range |
| 70+ bits | 0x1000000000000 | Large | Reduce overhead |

### Calculation

```
Total Range / Work Unit Size = Number of Units

2^66 / 0x100000000000 = 4096 work units
```

Aim for 1000-10000 work units total.

## Checkpoint Management

### Automatic Checkpoints

Server saves state every 60 seconds (configurable):

```bash
./keyhunt ... --checkpoint-interval 30  # Save every 30 seconds
```

Checkpoint file: `keyhunt_checkpoint.json`

### Checkpoint Contents

```json
{
  "timestamp": "2025-01-22T10:30:00Z",
  "progress": {
    "completed_units": 1523,
    "total_units": 4096,
    "percent": 37.2
  },
  "work_units": [
    {"id": 0, "status": "completed", "client": "worker1"},
    {"id": 1, "status": "completed", "client": "worker2"},
    {"id": 2, "status": "in_progress", "client": "worker3"},
    ...
  ],
  "clients": [
    {"name": "worker1", "cores": 16, "speed": 85.2},
    {"name": "worker2", "cores": 32, "speed": 172.4}
  ]
}
```

### Recovery from Crash

Simply restart the server with same parameters:

```bash
./keyhunt --wizard
# or
./keyhunt -m address -f target.txt -b 66 --server --port 2222
```

The checkpoint is automatically loaded.

## Community Integration

### Fetching Already-Scanned Ranges

The wizard can fetch progress from BTCPuzzle.info:

```bash
./keyhunt --wizard
# Enable "Fetch community progress" option
```

This excludes ranges already scanned by the community.

### Manual Exclusion

Create an exclusion file:

```
# exclusions.txt
0x20000000000000000:0x200000FFFFFFFFFFF
0x20001000000000000:0x200010FFFFFFFFFFF
```

## Server Dashboard

When running, the server displays:

```
╔════════════════════════════════════════════════════════════════╗
║                    KEYHUNT DISTRIBUTED SERVER                   ║
╠════════════════════════════════════════════════════════════════╣
║  Status: Running                     Port: 2222                 ║
║  Puzzle: #66                         Mode: ADDRESS              ║
╠════════════════════════════════════════════════════════════════╣
║  Progress: [████████░░░░░░░░░░░░] 42.3%                        ║
║  Units: 1732/4096 completed                                     ║
║  Speed: 856.4 Mkeys/s (combined)                               ║
╠════════════════════════════════════════════════════════════════╣
║  Connected Clients:                                             ║
║    worker1 (192.168.1.10) - 16 cores - 85.2 Mkeys/s            ║
║    worker2 (192.168.1.11) - 32 cores - 172.4 Mkeys/s           ║
║    gpu-rig (192.168.1.20) - GPU      - 598.8 Mkeys/s           ║
╠════════════════════════════════════════════════════════════════╣
║  Last checkpoint: 45 seconds ago                                ║
╚════════════════════════════════════════════════════════════════╝
```

## Firewall Configuration

### Ubuntu/Debian (ufw)

```bash
sudo ufw allow 2222/tcp
```

### CentOS/RHEL (firewalld)

```bash
sudo firewall-cmd --add-port=2222/tcp --permanent
sudo firewall-cmd --reload
```

### iptables

```bash
sudo iptables -A INPUT -p tcp --dport 2222 -j ACCEPT
```

## Running as a Service

### systemd Service File

Create `/etc/systemd/system/keyhunt-server.service`:

```ini
[Unit]
Description=Keyhunt Distributed Server
After=network.target

[Service]
Type=simple
User=keyhunt
WorkingDirectory=/opt/keyhunt
ExecStart=/opt/keyhunt/keyhunt --wizard
Restart=on-failure
RestartSec=10

[Install]
WantedBy=multi-user.target
```

Enable and start:

```bash
sudo systemctl enable keyhunt-server
sudo systemctl start keyhunt-server
```

## Security Hardening

### Restrict to Trusted IPs

```bash
# Only allow specific IPs
sudo ufw allow from 192.168.1.0/24 to any port 2222
```

### Use SSH Tunnel

For untrusted networks:

```bash
# On client
ssh -L 2222:localhost:2222 user@server

# Connect to localhost:2222 instead of server:2222
```

### VPN

For production distributed setups, use WireGuard or OpenVPN.

## Troubleshooting

### Port Already in Use

```bash
# Find process using port
sudo lsof -i :2222

# Kill if needed
sudo kill <PID>
```

### Clients Can't Connect

1. Check firewall: `sudo ufw status`
2. Verify server is listening: `netstat -tlnp | grep 2222`
3. Test connectivity: `nc -zv server-ip 2222`

### High Memory Usage

Reduce bloom filter size or use smaller target file.

## See Also

- [Client Setup](client-setup.md) - Configure worker clients
- [Protocol](protocol.md) - Communication protocol details
- [Troubleshooting](troubleshooting.md) - Common issues
