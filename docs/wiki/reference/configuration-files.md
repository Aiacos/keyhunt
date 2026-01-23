# Configuration Files Reference

This document describes all configuration and data files used by keyhunt.

## File Locations

### User Data Directory

```
~/.keyhunt/
├── progress/           # Search progress persistence
│   └── *.json
├── bloom/              # Cached bloom filter tables
│   └── *.blm
└── config/             # User configuration
    └── defaults.json
```

### Working Directory Files

```
./
├── keyhunt_wizard.json     # Wizard configuration
├── keyhunt_checkpoint.json # Distributed mode checkpoint
├── keyhunt_bsgs_*.blm      # BSGS bloom filter tables
├── keyhunt_bsgs_*.tbl      # BSGS baby step tables
├── KEYFOUNDKEYFOUND.txt    # Found keys (text)
├── found_keys.json         # Found keys (JSON)
└── puzzles_cache.txt       # Puzzle database cache
```

## Wizard Configuration

### keyhunt_wizard.json

Created by `--wizard`, contains all configuration.

```json
{
  "version": "1.0",
  "created": "2025-01-22T10:30:00Z",
  "mode": "server",
  "puzzle": {
    "number": 66,
    "address": "13zb1hQbWVsc2S7ZTZnP2G4undNNpdh5so",
    "range_start": "0x20000000000000000",
    "range_end": "0x3FFFFFFFFFFFFFFFF",
    "has_pubkey": false,
    "pubkey": null
  },
  "server": {
    "port": 2222,
    "bind_address": "0.0.0.0",
    "work_unit_size": "0x100000000000",
    "checkpoint_interval": 60,
    "server_also_worker": true
  },
  "client": {
    "server_ip": "192.168.1.100",
    "server_port": 2222,
    "client_name": "worker1"
  },
  "search": {
    "mode": "address",
    "threads": 16,
    "gpu_mode": "hybrid",
    "key_type": "compress",
    "random": true,
    "batch_size": 1024
  },
  "hardware": {
    "cpu_cores": 16,
    "ram_gb": 32,
    "gpu_available": true,
    "simd_features": ["AVX2", "SHA-NI"]
  }
}
```

## Checkpoint File

### keyhunt_checkpoint.json

Saved by distributed server for crash recovery.

```json
{
  "version": "1.0",
  "created": "2025-01-22T10:30:00Z",
  "last_update": "2025-01-22T10:35:00Z",
  "puzzle": {
    "number": 66,
    "address": "13zb1hQbWVsc2S7ZTZnP2G4undNNpdh5so"
  },
  "progress": {
    "total_units": 4096,
    "completed_units": 1523,
    "in_progress_units": 3,
    "percent_complete": 37.2,
    "total_keys_checked": "1523000000000000",
    "elapsed_seconds": 3600
  },
  "work_units": [
    {
      "id": 0,
      "range_start": "0x20000000000000000",
      "range_end": "0x200000FFFFFFFFFFF",
      "status": "completed",
      "assigned_to": "worker1",
      "completed_at": "2025-01-22T10:31:00Z"
    },
    {
      "id": 1,
      "range_start": "0x20001000000000000",
      "range_end": "0x200010FFFFFFFFFFF",
      "status": "in_progress",
      "assigned_to": "worker2",
      "assigned_at": "2025-01-22T10:34:50Z"
    }
  ],
  "connected_clients": [
    {
      "name": "worker1",
      "ip": "192.168.1.10",
      "connected_at": "2025-01-22T10:00:00Z",
      "cores": 16,
      "current_speed_mkeys": 85.2
    }
  ]
}
```

## Progress Files

### ~/.keyhunt/progress/*.json

Persistent progress for standalone mode.

```json
{
  "session_id": "abc123",
  "started": "2025-01-22T10:00:00Z",
  "last_update": "2025-01-22T10:35:00Z",
  "mode": "address",
  "target_file": "/path/to/target.txt",
  "range": {
    "start": "0x20000000000000000",
    "end": "0x3FFFFFFFFFFFFFFFF",
    "current": "0x25000000000000000"
  },
  "stats": {
    "keys_checked": "5000000000000000",
    "elapsed_seconds": 2100,
    "avg_speed_mkeys": 79.4
  }
}
```

## BSGS Table Files

### keyhunt_bsgs_*.blm

Binary bloom filter data. Created with `-S` flag.

Format (binary):
```
Offset  Size    Description
0       4       Magic number (0x424C4F4D "BLOM")
4       4       Version
8       8       Table size
16      8       Hash count
24      N       Bloom filter data
```

### keyhunt_bsgs_*.tbl

Binary baby step lookup table.

Format (binary):
```
Offset  Size    Description
0       4       Magic number (0x42535447 "BSTG")
4       4       Version
8       8       Entry count
16      N*16    Entries (X-coordinate, partial hash)
```

## Found Keys Files

### KEYFOUNDKEYFOUND.txt

Human-readable found keys.

```
================================================================================
KEY FOUND!
Date: 2025-01-22 10:35:00 UTC
================================================================================
Address: 13zb1hQbWVsc2S7ZTZnP2G4undNNpdh5so
Private Key (Hex): 0x20000000000000001234
Private Key (Dec): 36893488147419103796
WIF (Compressed): KwDiBf89QgGbjEhKnhXJuH7LrciVrZi3qYjgd9M7rFU73sVHnoWn
WIF (Uncompressed): 5HueCGU8rMjxEXxiPuD5BDku4MkFqeZyd4dZ1jvhTVqvbTLvyTJ
Public Key (Compressed): 02abc123...
Public Key (Uncompressed): 04abc123...
================================================================================
```

### found_keys.json

Machine-readable found keys.

```json
{
  "keys": [
    {
      "found_at": "2025-01-22T10:35:00Z",
      "address": "13zb1hQbWVsc2S7ZTZnP2G4undNNpdh5so",
      "private_key_hex": "0x20000000000000001234",
      "private_key_decimal": "36893488147419103796",
      "wif_compressed": "KwDiBf89QgGbjEhKnhXJuH7LrciVrZi3qYjgd9M7rFU73sVHnoWn",
      "wif_uncompressed": "5HueCGU8rMjxEXxiPuD5BDku4MkFqeZyd4dZ1jvhTVqvbTLvyTJ",
      "public_key_compressed": "02abc123...",
      "public_key_uncompressed": "04abc123...",
      "found_by": "worker1",
      "mode": "address"
    }
  ]
}
```

## Target Files

### Address File (ADDRESS mode)

One Bitcoin address per line.

```
1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH
1PWCx5fovoEaoBowAvF5k91m2Xat9bMgwb
3CnQvGgJkGNzMNpH8NMM9w7vCGxQG3Hmyn
```

### Public Key File (BSGS/XPOINT mode)

One public key per line.

```
02145d2611c823a396ef6712ce0f712f09b9b4f3135e3e0aa3230fb9b6d08d1e16
03abc123...
```

### RIPEMD160 File (RMD160 mode)

One hash per line (40 hex characters).

```
751e76e8199196d454941c45d1b3a323f1433bd6
89abcdef0123456789abcdef0123456789abcdef
```

### X-Coordinate File (XPOINT mode)

X-coordinate only (64 hex characters).

```
145d2611c823a396ef6712ce0f712f09b9b4f3135e3e0aa3230fb9b6d08d1e16
```

## Puzzle Cache

### puzzles_cache.txt

Cached puzzle database from BTCPuzzle.info.

```
# Puzzle cache - updated 2025-01-22
# Format: number|address|range_start|range_end|has_pubkey|pubkey|status|reward
66|13zb1hQbWVsc2S7ZTZnP2G4undNNpdh5so|0x20000000000000000|0x3FFFFFFFFFFFFFFFF|0||unsolved|6.6
67|1BY8GQbnueYofwSuFAT3USAhGjPrkxDdW9|0x40000000000000000|0x7FFFFFFFFFFFFFFFF|0||unsolved|6.7
...
135|16jY7qLJnxb7CHZyqBP8qca9d51gAjyXQN|0x40...|0x7F...|1|02abc...|unsolved|13.5
```

## Environment Configuration

### Shell Environment

```bash
# ~/.bashrc or ~/.zshrc

# Skip hardware detection
export KEYHUNT_SKIP_SYSINFO=1

# Custom progress directory
export KEYHUNT_PROGRESS_DIR=/data/keyhunt/progress

# GPU selection
export CUDA_VISIBLE_DEVICES=0
```

## File Permissions

Recommended permissions:

```bash
# Configuration files (user read/write only)
chmod 600 keyhunt_wizard.json
chmod 600 ~/.keyhunt/config/*

# Found keys (protect!)
chmod 600 KEYFOUNDKEYFOUND.txt
chmod 600 found_keys.json

# Table files (can be shared)
chmod 644 keyhunt_bsgs_*.blm
chmod 644 keyhunt_bsgs_*.tbl
```

## Backup Recommendations

### Essential to Backup

1. `found_keys.json` - Found private keys
2. `KEYFOUNDKEYFOUND.txt` - Found private keys
3. `keyhunt_wizard.json` - Configuration
4. `keyhunt_checkpoint.json` - Progress

### Optional to Backup

1. `keyhunt_bsgs_*.blm` - Regeneratable but slow
2. `keyhunt_bsgs_*.tbl` - Regeneratable but slow
3. `~/.keyhunt/progress/` - Standalone progress

## See Also

- [CLI Options](cli-options.md) - Command-line reference
- [Wizard Configuration](../wizard/configuration.md) - Detailed wizard config
- [FAQ](faq.md) - Common questions
