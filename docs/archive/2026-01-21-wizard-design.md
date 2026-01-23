# Keyhunt Wizard System Design

## Overview

Interactive wizard system (`./keyhunt --wizard`) that automates distributed puzzle solving with:
- Guided setup for server/client
- Auto-configuration with JSON persistence
- Community progress integration (BTCPuzzle.info scraping)
- Local progress tracking with range exclusion
- Automatic config sharing between server and clients

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                     WIZARD ENTRY POINT                          │
│                    ./keyhunt --wizard                           │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          ▼
              ┌───────────────────────┐
              │   Mode Selection      │
              │  [S]erver / [C]lient  │
              └───────────┬───────────┘
                          │
          ┌───────────────┴───────────────┐
          ▼                               ▼
┌─────────────────────┐       ┌─────────────────────┐
│   SERVER MODE       │       │   CLIENT MODE       │
│                     │       │                     │
│ - Select puzzle     │       │ - Enter server IP   │
│ - Configure port    │       │ - Auto-fetch config │
│ - Set work unit sz  │       │ - Detect hardware   │
│ - Fetch community   │       │ - Start worker      │
│   progress          │       │                     │
│ - Start coordinator │       │                     │
└─────────────────────┘       └─────────────────────┘
```

## Data Flow

```
┌──────────────────┐     HTTP GET      ┌────────────────────┐
│  BTCPuzzle.info  │ ◄──────────────── │     SERVER         │
│  (Community)     │                   │                    │
└────────┬─────────┘                   │  ┌──────────────┐  │
         │                             │  │ excluded.dat │  │
         │ Scanned ranges              │  │ (bitfield)   │  │
         ▼                             │  └──────────────┘  │
┌──────────────────┐                   │                    │
│ Range Exclusion  │ ──────────────────┤  ┌──────────────┐  │
│ Bitfield         │                   │  │ config.json  │  │
└──────────────────┘                   │  └──────────────┘  │
                                       └─────────┬──────────┘
                                                 │ TCP:7777
                    ┌────────────────────────────┼────────────────┐
                    │                            │                │
                    ▼                            ▼                ▼
            ┌───────────────┐          ┌───────────────┐  ┌───────────────┐
            │   CLIENT 1    │          │   CLIENT 2    │  │   CLIENT N    │
            │               │          │               │  │               │
            │ Auto-receives:│          │ Auto-receives:│  │ Auto-receives:│
            │ - config.json │          │ - config.json │  │ - config.json │
            │ - exclusions  │          │ - exclusions  │  │ - exclusions  │
            └───────────────┘          └───────────────┘  └───────────────┘
```

## Configuration File Format (JSON)

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
    "checkpoint_interval": 300
  },
  "search": {
    "mode": "address",
    "key_type": "compress",
    "random_mode": true
  },
  "community": {
    "enabled": true,
    "source": "btcpuzzle.info",
    "sync_interval": 3600,
    "last_sync": "2026-01-21T20:00:00Z"
  },
  "progress": {
    "total_ranges": 274877906944,
    "local_completed": 1234,
    "community_completed": 212952,
    "excluded_file": "excluded.dat"
  }
}
```

## Exclusion Bitfield Format

For puzzle 71 with 274 billion ranges (at 4G keys per range):
- Each range = 1 bit
- Total bits needed: 274,877,906,944
- Storage: ~32 GB for full bitfield

**Optimization**: Use hierarchical sparse bitfield:
- Level 0: 1 bit per 1M ranges (274K bits = 34 KB)
- Level 1: Detailed bitfield only for partially-scanned segments

## Wizard Flow - Server Mode

```
╔══════════════════════════════════════════════════════════════╗
║              KEYHUNT WIZARD - SERVER MODE                    ║
╚══════════════════════════════════════════════════════════════╝

[1/6] Select puzzle to solve:
      > [71] Puzzle 71 - 7.10 BTC (easiest active)
        [72] Puzzle 72 - 7.20 BTC
        [135] Puzzle 135 - 13.50 BTC (has public key!)

[2/6] Server configuration:
      Port [7777]: _

[3/6] Work unit size (keys per assignment):
      > [1] 4 billion (recommended, ~80s per unit)
        [2] 1 billion (faster feedback, more overhead)
        [3] 16 billion (less overhead, longer units)

[4/6] Fetch community progress from BTCPuzzle.info?
      > [Y] Yes - exclude already-scanned ranges
        [N] No - start fresh

[5/6] Enable automatic checkpointing?
      > [Y] Yes - save progress every 5 minutes
        [N] No

[6/6] Configuration summary:
      ┌────────────────────────────────────────┐
      │ Puzzle: 71                             │
      │ Target: 1PWo3JeB9jrGwfHDNpdGK54CRas7fV │
      │ Port: 7777                             │
      │ Work unit: 4G keys                     │
      │ Community sync: Enabled                │
      │ Excluded ranges: 212,952 (0.6%)        │
      └────────────────────────────────────────┘

      Save and start? [Y/n]: _

[+] Configuration saved to: keyhunt_wizard.json
[+] Fetching community progress...
[+] Downloaded 212,952 excluded ranges
[+] Starting coordinator on port 7777...
[+] Waiting for workers...
```

## Wizard Flow - Client Mode

```
╔══════════════════════════════════════════════════════════════╗
║              KEYHUNT WIZARD - CLIENT MODE                    ║
╚══════════════════════════════════════════════════════════════╝

[1/3] Enter server address:
      Server IP/hostname [localhost]: 192.168.1.100
      Port [7777]: _

[2/3] Connecting to server...
      [+] Connected to 192.168.1.100:7777
      [+] Received configuration:
          Puzzle: 71
          Target: 1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU
          Work unit: 4G keys

[3/3] Hardware detection:
      CPU: Intel i9-9900 (8 cores, 16 threads)
      GPU: NVIDIA RTX 2080 Super (CUDA available)
      RAM: 32 GB

      Recommended settings:
      > [1] CPU only (16 threads) - ~50 Mkeys/s
        [2] GPU only - ~500 Mkeys/s
        [3] Hybrid CPU+GPU - ~550 Mkeys/s (recommended)

[+] Starting worker in hybrid mode...
[+] Registered with server as worker-12345
[+] Receiving work assignments...
```

## Protocol Extensions

### Config Request (Client → Server)
```json
{
  "type": "config_request",
  "worker_id": "12345-hostname",
  "capabilities": {
    "cpu_threads": 16,
    "gpu_available": true,
    "gpu_model": "RTX 2080 Super",
    "ram_gb": 32
  }
}
```

### Config Response (Server → Client)
```json
{
  "type": "config_response",
  "config": { /* full config.json */ },
  "exclusion_checksum": "sha256:abc123...",
  "exclusion_url": "http://server:7778/excluded.dat"
}
```

### Work Assignment (with exclusion awareness)
```json
{
  "type": "work_assign",
  "work_id": 54321,
  "range_id": "45X943C",
  "start": "450000000000000000",
  "end": "45FFFFFFFFFFFFFF",
  "excluded_sub_ranges": [
    "4500000000:450FFFFFFF",
    "4520000000:452FFFFFFF"
  ]
}
```

## Community Data Scraping

### BTCPuzzle.info Scraper
```
GET https://btcpuzzle.info/puzzle/71
Parse: <script id="__NEXT_DATA__" type="application/json">
Extract: props.pageProps.data.scannedRanges[]
```

### Scanned Range Format (from BTCPuzzle)
```json
{
  "rangeId": "45X943C",
  "wallet": "bc1qznf9...wlgkaye4",
  "gpu": "RTX 3090",
  "startTime": "2026-01-20T10:00:00Z",
  "endTime": "2026-01-20T10:05:00Z",
  "proofKey": "sha256:..."
}
```

### Conversion to Exclusion
```
Range ID "45X943C" → Hex range 0x45X943C where X = [0-F]
Expands to: 0x4500943C0000000000 - 0x45F0943CFFFFFFFFFF
Mark these bits in exclusion bitfield
```

## File Structure

```
keyhunt/
├── wizard/
│   ├── wizard.c          # Main wizard logic
│   ├── wizard.h          # Public interface
│   ├── wizard_server.c   # Server mode implementation
│   ├── wizard_client.c   # Client mode implementation
│   ├── community_sync.c  # BTCPuzzle.info scraper
│   ├── exclusion.c       # Bitfield management
│   └── exclusion.h
├── keyhunt_wizard.json   # Generated config (gitignored)
├── excluded.dat          # Exclusion bitfield (gitignored)
└── ...
```

## Implementation Plan

### Phase 1: Core Wizard
1. `wizard.h/c` - Entry point, menu system, config I/O
2. JSON config save/load using existing config.c patterns
3. Interactive terminal UI with colors

### Phase 2: Server Mode
1. Extend `distributed.c` to serve config on connect
2. Add exclusion bitfield management
3. Range assignment with exclusion awareness

### Phase 3: Client Mode
1. Auto-fetch config from server
2. Hardware detection (reuse sysinfo.c)
3. Automatic worker startup

### Phase 4: Community Integration
1. HTTP client for BTCPuzzle.info
2. JSON parsing for scanned ranges
3. Periodic sync with configurable interval

## Testing Strategy

1. Unit tests for exclusion bitfield operations
2. Integration test: server + 2 clients locally
3. Scraper test with cached BTCPuzzle response
4. End-to-end: find key in small test range with exclusions
