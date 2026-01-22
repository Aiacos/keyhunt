# Interactive Wizard Guide

The keyhunt wizard is the easiest way to set up distributed Bitcoin puzzle solving. It guides you through configuration with intelligent defaults and auto-tuning.

## Quick Start

```bash
./keyhunt --wizard    # or ./keyhunt -W
```

## Why Use the Wizard?

| Without Wizard | With Wizard |
|----------------|-------------|
| Remember 10+ command-line flags | Interactive step-by-step setup |
| Manual hardware detection | Auto-detects CPU, GPU, RAM |
| Calculate BSGS N/K yourself | Calculates optimal parameters |
| No progress tracking | Saves/resumes configuration |
| Risk of suboptimal settings | Intelligent recommendations |

## The 5-Step Flow

### Step 1: Puzzle Selection

The wizard downloads the latest puzzle database from BTCPuzzle.info:

```
[Step 1/5] Puzzle Selection
──────────────────────────────────────────

[+] Downloading puzzle database...
Select puzzle to solve:

  > [1] Puzzle #71  |  71 bits | 7.10 BTC
    [2] Puzzle #72  |  72 bits | 7.20 BTC
    ...
    [6] Puzzle #135 | 135 bits | 13.50 BTC [HAS PUBKEY - EASIER!]
```

**Key insight**: Puzzles marked `[HAS PUBKEY]` can be solved **exponentially faster** using BSGS algorithm!

### Step 2: Mode Selection

Choose between server (coordinator) or client (worker):

```
[Step 2/5] Mode Selection
──────────────────────────────────────────

  > [1] SERVER - Start a new search (coordinator + local worker)
    [2] CLIENT - Join an existing server as worker
```

**Note**: The server also runs as a worker, contributing to the search locally.

### Step 3: Server Configuration

Configure the distributed coordination:

```
[Step 3/5] Server Configuration
──────────────────────────────────────────

Server port (1024-65535) [7777]:
Work unit size:
  > [1] 4 billion keys/unit (~80s CPU, ~8s GPU) - Recommended
    [2] 1 billion keys/unit (faster progress updates)
    [3] 16 billion keys/unit (less network overhead)

Also run worker on this machine (server+worker)? [Y/n]:
Checkpoint interval (seconds) (60-3600) [300]:
```

### Step 4: Search Configuration

The wizard auto-detects hardware and calculates optimal parameters:

```
[Step 4/5] Search Configuration
──────────────────────────────────────────

[+] Hardware Detection:
    CPU: 8 physical cores, 16 logical threads
    RAM: 32009 MB total, 26189 MB available
    Cache: L1=32KB L2=256KB L3=16384KB
    Features: AVX2
    GPU: NVIDIA GeForce RTX 2080 SUPER (8192 MB VRAM)
    Performance Score: CPU=12.0 GPU=245.8

[+] Optimal Configuration for 135-bit puzzle:
    ┌──────────────────────────────────────────────────────────┐
    │ Difficulty: HARD (has pubkey)                            │
    │ Best Mode:  bsgs                                         │
    │ Strategy:   BSGS with O(√N) complexity                   │
    │ BSGS N:     0x20000000 (536 M entries)                   │
    │ BSGS K:     1                                            │
    │ Key Type:   compress                                     │
    │ Random:     No (sequential)                              │
    │ Est. Time:  ~9.35e+04 years (need distributed!)          │
    └──────────────────────────────────────────────────────────┘
```

### Step 5: Community Integration

Fetch already-scanned ranges to avoid duplicate work:

```
[Step 5/5] Community Integration
──────────────────────────────────────────

Fetch community progress from BTCPuzzle.info? [Y/n]:

[+] Fetching community data for puzzle #135...
[+] Found 1234 ranges already scanned by community
Add these to exclusion list? [Y/n]:
```

The wizard also integrates with **privatekeys.pw cloud search** to fetch community scanning progress:

```
[+] Refreshing privatekeys.pw progress (daily update)...
[+] Fetching privatekeys.pw cloud search progress...
[+] Parsed: 0.022477% scanned (1234567890 keys)
```

**Features:**
- Extracts "Keys Scanned (Total)" percentage from https://privatekeys.pw/cloud-search
- 24-hour caching to avoid excessive requests
- Cache stored in `~/.keyhunt/privatekeys_progress.json`
- Sequential mode: Automatically starts search after community-scanned region
- Random mode: Avoids generating ranges within already-scanned regions

## Intelligent Auto-Configuration

### Puzzles WITH Public Key

When you select a puzzle with an exposed public key (like #135, #140, #145...):

| Setting | Value | Why |
|---------|-------|-----|
| Mode | BSGS | O(√N) vs O(N) complexity |
| GPU | Disabled | BSGS is CPU-optimized |
| Random | No | Sequential better for BSGS |
| BSGS N | Auto-calculated | Based on 60% of RAM |
| BSGS K | Auto-calculated | Balances speed/memory |

### Puzzles WITHOUT Public Key

For puzzles like #71, #72, #73... (no public key):

| Setting | Value | Why |
|---------|-------|-----|
| Mode | Address | Standard brute-force |
| GPU | Enabled | Hybrid CPU+GPU faster |
| Random | Yes (>66 bits) | Better coverage for large ranges |
| Work Unit | Based on bits | Optimized for range size |

## Difficulty Ratings Explained

The wizard shows a difficulty rating:

| Rating | Without Pubkey | With Pubkey | Meaning |
|--------|----------------|-------------|---------|
| EASY | ≤50 bits | ≤80 bits | Solvable in days |
| MODERATE | 51-66 bits | 81-120 bits | Weeks to months |
| HARD | 67-72 bits | 121-160 bits | Years |
| VERY HARD | 73-80 bits | - | Centuries |
| IMPOSSIBLE | >80 bits | - | Don't waste electricity |

## Multi-PC Distributed Setup

### On the Server Machine

```bash
# Start the wizard
./keyhunt --wizard

# Select your target puzzle
# Choose SERVER mode
# Accept or customize settings
# Note the port number (default: 7777)
```

### On Each Client Machine

```bash
# Start the wizard
./keyhunt --wizard

# Select the SAME puzzle as server
# Choose CLIENT mode
# Enter server IP: 192.168.1.100  # Replace with actual IP
# Enter port: 7777
```

### Network Requirements

- All machines on same network (or VPN)
- Port 7777 (or configured port) must be open
- Firewall must allow incoming connections on server

## Configuration File Format

The wizard saves configuration to `keyhunt_wizard.json`:

```json
{
  "version": 1,
  "puzzle": {
    "number": 135,
    "target_address": "16jY7qLJnxb7CHZyqBP8qca9d51gAjyXQN",
    "range_start": "40000000000000000000000000000000000",
    "range_end": "7ffffffffffffffffffffffffffffffffff",
    "bits": 135
  },
  "server": {
    "host": "0.0.0.0",
    "port": 7777,
    "work_unit_size": "1073741824",
    "checkpoint_interval": 300,
    "also_worker": true
  },
  "search": {
    "mode": "bsgs",
    "key_type": "compress",
    "random_mode": false,
    "threads": 16,
    "gpu_percent": 0
  },
  "community": {
    "enabled": true,
    "source": "btcpuzzle.info",
    "sync_interval": 3600
  }
}
```

## Resuming a Session

When you run the wizard again, it detects existing configuration:

```
[+] Found existing configuration:

Configuration Summary:
  ┌─────────────────────────────────────────────────────────┐
  │ Puzzle: #135 (135 bits)                                 │
  │ Target: 16jY7qLJnxb7CHZyqBP8qca9d51gAjyXQN...           │
  │ Mode:   SERVER (+worker)                                │
  │ Port:   7777                                            │
  │ ...                                                     │
  └─────────────────────────────────────────────────────────┘

Resume with this configuration? [Y/n]:
```

## Comparison: Wizard vs Manual

### With Wizard (30 seconds)
```bash
./keyhunt --wizard
# Click through 5 steps
# Done!
```

### Without Wizard (manual)
```bash
./keyhunt -m bsgs \
  -f tests/135_pubkey.txt \
  -b 135 \
  -n 0x20000000 \
  -k 1 \
  -t 16 \
  -l compress \
  -q \
  -s 10
# Plus you need to calculate N/K yourself
# Plus no distributed coordination
# Plus no community exclusions
# Plus no progress persistence
```

## Tips & Best Practices

1. **Always use wizard for distributed mode** - It handles coordination automatically

2. **Check the difficulty rating** - If it says "PRACTICALLY IMPOSSIBLE", consider:
   - Joining a pool
   - Using a puzzle with public key
   - Contributing to community exclusion lists

3. **Keep community sync enabled** - Avoids wasting time on already-scanned ranges

4. **Server also as worker** - Always enable this unless you're running on a dedicated server with no compute power

5. **Checkpoint interval** - 300 seconds (5 min) is good balance between safety and performance

## Troubleshooting

### "Connection failed, retrying"
- Verify server IP address is correct
- Check firewall allows port 7777
- Ensure server is running

### "No puzzles found in response"
- BTCPuzzle.info scraping failed
- Wizard uses built-in database automatically
- Try again later for updated puzzles

### "Failed to configure work range"
- Range parsing issue in distributed backend
- Wizard configuration is still saved
- Manual intervention may be needed for very large ranges

## See Also

- [03-Distributed-Computing.md](03-Distributed-Computing.md) - Manual distributed setup
- [01-Strategy-Guide.md](01-Strategy-Guide.md) - Puzzle solving strategies
- [02-Algorithm-Reference.md](02-Algorithm-Reference.md) - Algorithm details
