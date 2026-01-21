# Keyhunt Interactive Wizard

The wizard provides an easy way to set up distributed Bitcoin puzzle solving without remembering complex command-line parameters.

## Quick Start

```bash
./keyhunt --wizard    # or ./keyhunt -W
```

## Features

### 1. Puzzle Database
- Downloads latest puzzle information from BTCPuzzle.info
- Falls back to built-in database if network is unavailable
- Caches puzzles locally in `puzzles_cache.txt`
- Shows which puzzles have public keys (easier with BSGS)

### 2. Intelligent Configuration
The wizard automatically calculates optimal parameters based on:
- **Puzzle characteristics** (bit range, public key availability)
- **Hardware capabilities** (CPU cores, RAM, GPU, SIMD features)
- **Search strategy** (BSGS vs brute-force)

### 3. Distributed Mode
- **Server Mode**: Coordinates work distribution + runs local worker
- **Client Mode**: Connects to server and processes assigned ranges

### 4. Community Integration
- Fetches already-scanned ranges from BTCPuzzle.info
- Avoids duplicate work by excluding community-tested ranges
- Syncs periodically to stay updated

## Configuration Flow

```
┌─────────────────────────────────────────────────────────────┐
│                   WIZARD 5-STEP FLOW                        │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  Step 1: Puzzle Selection                                   │
│  ├── Download puzzle database                               │
│  ├── Show unsolved puzzles                                  │
│  └── Highlight puzzles with public keys                     │
│                                                             │
│  Step 2: Mode Selection                                     │
│  ├── SERVER: Coordinator + Local Worker                     │
│  └── CLIENT: Worker Only                                    │
│                                                             │
│  Step 3: Server/Client Configuration                        │
│  ├── Port, work unit size                                   │
│  └── Checkpoint interval                                    │
│                                                             │
│  Step 4: Search Configuration                               │
│  ├── Hardware detection                                     │
│  ├── Optimal parameter calculation                          │
│  ├── Mode selection (BSGS/Address/RMD160)                   │
│  └── Thread count, GPU usage                                │
│                                                             │
│  Step 5: Community Integration                              │
│  ├── Fetch scanned ranges                                   │
│  └── Configure sync interval                                │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

## Optimal Configuration Logic

### Puzzles WITH Public Key (BSGS Mode)

When a puzzle has an exposed public key, the wizard automatically:

1. **Selects BSGS mode** - O(√N) complexity vs O(N) for brute-force
2. **Calculates optimal N** - Largest power of 2 that fits in 60% of RAM
3. **Sets K factor** - Balances speed vs memory usage
4. **Disables GPU** - BSGS is CPU-optimized
5. **Disables random mode** - Sequential is better for BSGS

**Example for 135-bit puzzle with 26GB available RAM:**
```
BSGS N:     0x20000000 (536M entries)
BSGS K:     1
Est. Time:  ~93,500 years (single machine)
Strategy:   O(√N) complexity
```

### Puzzles WITHOUT Public Key (Address Mode)

For puzzles without public key exposure:

1. **Selects Address mode** - Standard brute-force search
2. **Enables GPU** - Uses hybrid CPU+GPU for maximum throughput
3. **Enables random mode** - Better coverage for large ranges (>66 bits)
4. **Adjusts work unit size** - Based on bit range

**Example for 71-bit puzzle:**
```
Mode:       address
GPU:        95% (if available)
Random:     Yes (better coverage)
Est. Time:  ~1.5M years (single machine)
Strategy:   Brute-force with GPU acceleration
```

## Work Unit Sizing

| Bit Range | Work Unit Size | Rationale |
|-----------|----------------|-----------|
| ≤50 bits  | 256M keys      | Small range, quick progress |
| 51-70 bits| 4B keys        | Standard, balanced |
| >70 bits  | 16B keys       | Large range, less overhead |

## Server Architecture

The server runs in a dual role:

```
┌─────────────────────────────────────────────────────────────┐
│                    SERVER MACHINE                            │
│                                                             │
│  ┌─────────────────┐     ┌─────────────────────────────┐   │
│  │   Coordinator   │     │      Local Worker           │   │
│  │                 │     │                             │   │
│  │  - Listen :7777 │     │  - Auto-detect hardware     │   │
│  │  - Assign work  │     │  - Process ranges locally   │   │
│  │  - Track progress│    │  - Report to coordinator    │   │
│  │  - Checkpoints  │     │  - Save found keys          │   │
│  └────────┬────────┘     └──────────────┬──────────────┘   │
│           │                              │                  │
│           └──────────┬───────────────────┘                  │
│                      │                                      │
│              ┌───────▼───────┐                              │
│              │  Work Queue   │                              │
│              │  (shared)     │                              │
│              └───────────────┘                              │
└─────────────────────────────────────────────────────────────┘
                       │
                       │ TCP/IP
                       │
    ┌──────────────────┼──────────────────┐
    │                  │                  │
    ▼                  ▼                  ▼
┌────────┐       ┌────────┐        ┌────────┐
│Client 1│       │Client 2│        │Client N│
│Worker  │       │Worker  │        │Worker  │
└────────┘       └────────┘        └────────┘
```

## Configuration Files

### keyhunt_wizard.json
Main configuration file (auto-generated):
```json
{
  "version": 1,
  "puzzle": {
    "number": 71,
    "target_address": "1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU",
    "range_start": "400000000000000000",
    "range_end": "7fffffffffffffffff",
    "bits": 71
  },
  "server": {
    "host": "0.0.0.0",
    "port": 7777,
    "work_unit_size": "100000000",
    "checkpoint_interval": 300,
    "also_worker": true
  },
  "search": {
    "mode": "address",
    "key_type": "compress",
    "random_mode": true,
    "threads": 16,
    "gpu_percent": 95
  },
  "community": {
    "enabled": true,
    "source": "btcpuzzle.info",
    "sync_interval": 3600
  }
}
```

### puzzles_cache.txt
Cached puzzle database for offline use.

### wizard_progress.dat / wizard_excluded.dat
Progress tracking and exclusion lists.

## Usage Examples

### Start a New Server
```bash
./keyhunt --wizard
# Select puzzle #71
# Choose SERVER mode
# Accept defaults or customize
# Wait for clients to connect
```

### Join as Client
```bash
./keyhunt --wizard
# Select same puzzle as server
# Choose CLIENT mode
# Enter server IP and port
# Start processing
```

### Resume Previous Session
```bash
./keyhunt --wizard
# Wizard detects existing keyhunt_wizard.json
# Choose "Resume" to continue
```

## Difficulty Ratings

| Rating | Bits (no pubkey) | Bits (with pubkey) | Estimated Time |
|--------|------------------|--------------------| --------------|
| EASY   | ≤50              | ≤80                | < 1 week      |
| MODERATE | 51-66          | 81-120             | weeks-months  |
| HARD   | 67-72            | 121-160            | years         |
| VERY HARD | 73-80         | >160               | centuries     |
| IMPOSSIBLE | >80          | -                  | heat death    |

## Troubleshooting

### "Failed to configure work range"
The distributed coordinator couldn't parse the range. This usually happens when the range is too large. The wizard UI works correctly; this is a limitation of the distributed backend.

### "No puzzles found in response"
BTCPuzzle.info scraping failed. The wizard uses built-in puzzle database as fallback.

### "Connection failed, retrying"
Client can't reach the server. Check:
- Server is running
- Firewall allows port 7777 (or configured port)
- Correct IP address

## Future Improvements

- [ ] Real keyhunt search integration (currently uses stubs)
- [ ] Kangaroo algorithm support for puzzles with public keys
- [ ] Web dashboard for monitoring distributed progress
- [ ] Automatic GPU workload balancing
- [ ] Resume from specific checkpoint
