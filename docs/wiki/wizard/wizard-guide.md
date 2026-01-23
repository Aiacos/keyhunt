# Interactive Wizard Guide

The keyhunt wizard provides a guided 5-step setup for distributed puzzle solving with intelligent defaults based on your hardware.

## Starting the Wizard

```bash
./keyhunt --wizard
# or
./keyhunt -W
```

## Wizard Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                    KEYHUNT INTERACTIVE WIZARD                   │
├─────────────────────────────────────────────────────────────────┤
│  Step 1: Puzzle Selection                                       │
│  Step 2: Mode Selection (Server/Client)                         │
│  Step 3: Server/Client Configuration                            │
│  Step 4: Search Configuration                                   │
│  Step 5: Community Integration                                  │
└─────────────────────────────────────────────────────────────────┘
```

## Step 1: Puzzle Selection

The wizard downloads the latest puzzle database from BTCPuzzle.info.

```
[+] Fetching puzzle database...
[+] Downloaded 160 puzzles

Available Puzzles:
┌────────┬─────────────────────────────────────┬────────┬────────────┐
│ Number │ Address                             │ Status │ Public Key │
├────────┼─────────────────────────────────────┼────────┼────────────┤
│ 66     │ 13zb1hQbWVsc2S7ZTZnP2G4undNNpdh5so  │ Active │ No         │
│ 67     │ 1BY8GQbnueYofwSuFAT3USAhGjPrkxDdW9  │ Active │ No         │
│ ...    │ ...                                 │ ...    │ ...        │
│ 135    │ 16jY7qLJnxb7CHZyqBP8qca9d51gAjyXQN  │ Active │ Yes        │
└────────┴─────────────────────────────────────┴────────┴────────────┘

Enter puzzle number (or 'custom' for custom range): 66
```

### Puzzle Information Displayed

- **Puzzle number**: Bit size of the private key range
- **Address**: Target Bitcoin address
- **Status**: Active (unsolved) or Solved
- **Public Key**: Whether the public key is known (enables BSGS)
- **Range**: The search space
- **Current reward**: Prize in BTC

### Custom Range

Enter `custom` to specify your own range:

```
Enter start (hex): 0x20000000000000000
Enter end (hex): 0x3FFFFFFFFFFFFFFFF
Enter target address: 13zb1hQbWVsc2S7ZTZnP2G4undNNpdh5so
```

## Step 2: Mode Selection

```
Select mode:
  1. Server (Coordinator + optional local worker)
  2. Client (Connect to existing server)

Enter choice [1/2]: 1
```

### Server Mode

The server coordinates distributed search and optionally runs a local worker.

Use server mode when:
- You're starting a new distributed search
- You want to coordinate multiple machines
- You're running on a machine with network accessibility

### Client Mode

The client connects to an existing server and processes work units.

Use client mode when:
- A server is already running
- You want to contribute computing power
- You're behind NAT or firewall

## Step 3: Server Configuration

If you selected Server mode:

```
Server Configuration:
┌────────────────────────────────────────────────────────────────┐
│ Port [2222]: 2222                                              │
│ Work unit size [auto]: auto                                    │
│ Checkpoint interval (seconds) [60]: 60                         │
│ Also run as worker? [Y/n]: Y                                   │
└────────────────────────────────────────────────────────────────┘
```

### Server Options

| Option | Description | Default |
|--------|-------------|---------|
| Port | TCP listen port | 2222 |
| Work unit size | Size of each work chunk | Auto-calculated |
| Checkpoint interval | Seconds between saves | 60 |
| Also run as worker | Server also searches | Yes |

### Auto Work Unit Size

The wizard calculates optimal work unit size:

```
Range size: 2^66
Recommended work units: 4096
Work unit size: 0x100000000000
Expected units: 4096
```

## Step 3: Client Configuration

If you selected Client mode:

```
Client Configuration:
┌────────────────────────────────────────────────────────────────┐
│ Server IP: 192.168.1.100                                       │
│ Server port [2222]: 2222                                       │
│ Client name [hostname]: worker1                                │
└────────────────────────────────────────────────────────────────┘
```

## Step 4: Search Configuration

The wizard auto-detects your hardware:

```
Hardware Detection:
┌────────────────────────────────────────────────────────────────┐
│ CPU: AMD Ryzen 9 5900X                                         │
│   Physical cores: 12                                           │
│   Logical threads: 24                                          │
│   L3 Cache: 64 MB                                              │
│   SIMD: AVX2, SHA-NI                                           │
├────────────────────────────────────────────────────────────────┤
│ Memory: 64 GB total, 58 GB available                           │
├────────────────────────────────────────────────────────────────┤
│ GPU: NVIDIA RTX 3080                                           │
│   CUDA: 11.4                                                   │
│   Memory: 10 GB                                                │
└────────────────────────────────────────────────────────────────┘
```

### Recommended Configuration

Based on hardware and puzzle type:

```
Recommended Configuration:
┌────────────────────────────────────────────────────────────────┐
│ Search Mode: ADDRESS (no public key available)                 │
│ Threads: 24 (all logical cores)                                │
│ GPU Mode: hybrid (CPU + GPU parallel)                          │
│ Key Type: compressed (2x faster)                               │
│ Random: Yes (large range, random sampling)                     │
│ Batch Size: 1024 (AVX2 optimized)                              │
└────────────────────────────────────────────────────────────────┘

Accept recommended settings? [Y/n]: Y
```

### BSGS Configuration (When Public Key Available)

```
Recommended Configuration (BSGS):
┌────────────────────────────────────────────────────────────────┐
│ Search Mode: BSGS (public key available, sqrt(N) speedup)      │
│ N value: 0x1000000000000 (fits in 58 GB RAM)                   │
│ K factor: 1                                                    │
│ Memory usage: 48 GB (83% of available)                         │
│ Iterations needed: 16384                                       │
│ Save tables: Yes (faster restart)                              │
└────────────────────────────────────────────────────────────────┘
```

### Manual Override

If you decline recommended settings:

```
Configure manually:
  Threads [24]: 16
  GPU mode (off/hash/full/hybrid) [hybrid]: off
  Key type (compress/uncompress/both) [compress]: compress
  Random mode? [Y/n]: Y
  Batch size [1024]: 1024
```

## Step 5: Community Integration

The wizard offers to fetch already-scanned ranges:

```
Community Integration:
┌────────────────────────────────────────────────────────────────┐
│ Fetch already-scanned ranges from community? [Y/n]: Y          │
│                                                                │
│ [+] Fetching from privatekeys.pw...                            │
│ [+] Found 15,234 already-scanned ranges                        │
│ [+] These ranges will be excluded from search                  │
│                                                                │
│ Community coverage: 12.3% of puzzle 66 range                   │
└────────────────────────────────────────────────────────────────┘
```

### Benefits

- Avoid duplicate work
- Focus on unexplored ranges
- Contribute to community progress tracking

### Data Sources

- BTCPuzzle.info
- privatekeys.pw (if available)

## Configuration Summary

Before starting, the wizard shows a summary:

```
Configuration Summary:
╔════════════════════════════════════════════════════════════════╗
║ Puzzle: #66                                                    ║
║ Target: 13zb1hQbWVsc2S7ZTZnP2G4undNNpdh5so                     ║
║ Range: 0x20000000000000000 - 0x3FFFFFFFFFFFFFFFF               ║
╠════════════════════════════════════════════════════════════════╣
║ Mode: Server (Coordinator + Worker)                            ║
║ Port: 2222                                                     ║
║ Work unit size: 0x100000000000                                 ║
╠════════════════════════════════════════════════════════════════╣
║ Search: ADDRESS mode, compressed keys                          ║
║ Threads: 24                                                    ║
║ GPU: hybrid mode                                               ║
║ Random: Yes                                                    ║
╠════════════════════════════════════════════════════════════════╣
║ Community exclusions: 15,234 ranges                            ║
╚════════════════════════════════════════════════════════════════╝

Start search? [Y/n]: Y
```

## Configuration Persistence

The wizard saves configuration to `keyhunt_wizard.json`:

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
    "threads": 24,
    "gpu_mode": "hybrid",
    "key_type": "compress",
    "random": true,
    "batch_size": 1024
  },
  "community": {
    "enabled": true,
    "exclusions_count": 15234
  }
}
```

### Resume Previous Session

If configuration exists, the wizard offers to resume:

```
Previous configuration found:
  Puzzle: #66
  Mode: Server on port 2222
  Progress: 1523/4096 units (37.2%)

Resume previous session? [Y/n]: Y
```

## Puzzle Database Cache

Puzzle data is cached in `puzzles_cache.txt`:

- Refreshed daily by default
- Force refresh with `--wizard --refresh-puzzles`
- Fallback to cache if network unavailable

## Error Handling

If puzzle fetch fails:

```
[!] Failed to fetch puzzle database
[+] Using cached puzzle data (2 days old)
[+] Or enter puzzle details manually
```

## See Also

- [Configuration](configuration.md) - Configuration file format
- [Server Setup](../distributed/server-setup.md) - Manual server setup
- [Client Setup](../distributed/client-setup.md) - Manual client setup
