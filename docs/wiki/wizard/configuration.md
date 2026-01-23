# Wizard Configuration Reference

This document describes the configuration file format and options for the keyhunt wizard.

## Configuration File Location

Primary configuration: `keyhunt_wizard.json` (current directory)

Alternative locations:
- `~/.keyhunt/wizard.json`
- `/etc/keyhunt/wizard.json`

## File Format

The configuration file is JSON format:

```json
{
  "mode": "server",
  "puzzle": { ... },
  "server": { ... },
  "client": { ... },
  "search": { ... },
  "community": { ... },
  "hardware": { ... }
}
```

## Configuration Sections

### mode

Top-level mode setting.

```json
{
  "mode": "server"
}
```

| Value | Description |
|-------|-------------|
| `"server"` | Run as coordinator (optionally with worker) |
| `"client"` | Run as worker connecting to coordinator |
| `"standalone"` | Run without distributed features |

### puzzle

Target puzzle or custom range configuration.

```json
{
  "puzzle": {
    "number": 66,
    "address": "13zb1hQbWVsc2S7ZTZnP2G4undNNpdh5so",
    "range_start": "0x20000000000000000",
    "range_end": "0x3FFFFFFFFFFFFFFFF",
    "has_pubkey": false,
    "pubkey": null,
    "reward_btc": 6.6
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `number` | int | Puzzle number (bit size) |
| `address` | string | Target Bitcoin address |
| `range_start` | string | Start of search range (hex) |
| `range_end` | string | End of search range (hex) |
| `has_pubkey` | bool | Whether public key is known |
| `pubkey` | string | Public key (if known) |
| `reward_btc` | float | Current reward in BTC |

### server

Server mode configuration.

```json
{
  "server": {
    "port": 2222,
    "bind_address": "0.0.0.0",
    "work_unit_size": "0x100000000000",
    "checkpoint_interval": 60,
    "checkpoint_file": "keyhunt_checkpoint.json",
    "server_also_worker": true,
    "max_clients": 100,
    "heartbeat_timeout": 60,
    "work_unit_timeout": 600
  }
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `port` | int | 2222 | TCP listen port |
| `bind_address` | string | "0.0.0.0" | Interface to bind |
| `work_unit_size` | string | auto | Size of work units (hex) |
| `checkpoint_interval` | int | 60 | Seconds between checkpoints |
| `checkpoint_file` | string | auto | Checkpoint file path |
| `server_also_worker` | bool | true | Run local worker |
| `max_clients` | int | 100 | Maximum connected clients |
| `heartbeat_timeout` | int | 60 | Client timeout (seconds) |
| `work_unit_timeout` | int | 600 | Reassign after (seconds) |

### client

Client mode configuration.

```json
{
  "client": {
    "server_ip": "192.168.1.100",
    "server_port": 2222,
    "client_name": "worker1",
    "auto_reconnect": true,
    "reconnect_delay": 5,
    "progress_interval": 10
  }
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `server_ip` | string | required | Coordinator IP address |
| `server_port` | int | 2222 | Coordinator port |
| `client_name` | string | hostname | Client identifier |
| `auto_reconnect` | bool | true | Reconnect on disconnect |
| `reconnect_delay` | int | 5 | Seconds before reconnect |
| `progress_interval` | int | 10 | Progress report interval |

### search

Search algorithm configuration.

```json
{
  "search": {
    "mode": "address",
    "threads": 16,
    "gpu_mode": "hybrid",
    "key_type": "compress",
    "random": true,
    "batch_size": 1024,
    "quiet": false,
    "status_interval": 10
  }
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `mode` | string | "address" | Search mode |
| `threads` | int | auto | Number of CPU threads |
| `gpu_mode` | string | "off" | GPU mode |
| `key_type` | string | "compress" | Key type to search |
| `random` | bool | false | Random sampling |
| `batch_size` | int | 1024 | Keys per batch |
| `quiet` | bool | false | Reduce output |
| `status_interval` | int | 10 | Status print interval |

#### search.mode

| Value | Description |
|-------|-------------|
| `"address"` | Bitcoin address search |
| `"bsgs"` | Baby-Step Giant-Step |
| `"xpoint"` | X-coordinate search |
| `"rmd160"` | RIPEMD160 hash search |

#### search.gpu_mode

| Value | Description |
|-------|-------------|
| `"off"` | CPU only |
| `"auto"` | Auto-detect best mode |
| `"hash"` | GPU handles hashing |
| `"full"` | GPU handles entire pipeline |
| `"hybrid"` | CPU and GPU parallel |

#### search.key_type

| Value | Description |
|-------|-------------|
| `"compress"` | Compressed keys only |
| `"uncompress"` | Uncompressed keys only |
| `"both"` | Check both types |

### bsgs

BSGS-specific configuration (when search.mode is "bsgs").

```json
{
  "bsgs": {
    "n_value": "0x1000000000000",
    "k_factor": 1,
    "save_tables": true,
    "table_file_prefix": "keyhunt_bsgs",
    "bsgs_mode": "sequential"
  }
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `n_value` | string | auto | Baby step count (hex) |
| `k_factor` | int | 1 | Table size multiplier |
| `save_tables` | bool | true | Save bloom tables |
| `table_file_prefix` | string | auto | Table file prefix |
| `bsgs_mode` | string | "sequential" | Giant step strategy |

#### bsgs.bsgs_mode

| Value | Description |
|-------|-------------|
| `"sequential"` | Start to end |
| `"backward"` | End to start |
| `"both"` | Both directions |
| `"random"` | Random giant steps |
| `"dance"` | Alternating pattern |

### community

Community integration settings.

```json
{
  "community": {
    "enabled": true,
    "fetch_exclusions": true,
    "exclusions_url": "https://btcpuzzle.info/api/exclusions",
    "exclusions_file": "exclusions.txt",
    "exclusions_count": 15234,
    "report_progress": false
  }
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | bool | true | Enable community features |
| `fetch_exclusions` | bool | true | Fetch already-scanned ranges |
| `exclusions_url` | string | default | URL to fetch exclusions |
| `exclusions_file` | string | auto | Local exclusions cache |
| `exclusions_count` | int | 0 | Number of exclusions loaded |
| `report_progress` | bool | false | Report to community |

### hardware

Auto-detected hardware capabilities (read-only, populated by wizard).

```json
{
  "hardware": {
    "cpu_model": "AMD Ryzen 9 5900X",
    "cpu_physical_cores": 12,
    "cpu_logical_threads": 24,
    "cpu_cache_l3_kb": 65536,
    "ram_total_gb": 64,
    "ram_available_gb": 58,
    "gpu_available": true,
    "gpu_name": "NVIDIA RTX 3080",
    "gpu_memory_gb": 10,
    "gpu_cuda_version": "11.4",
    "simd_features": ["SSE2", "AVX2", "SHA-NI"]
  }
}
```

## Example Configurations

### Server for Puzzle 66

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
    "random": true
  },
  "community": {
    "enabled": true,
    "fetch_exclusions": true
  }
}
```

### Client with GPU

```json
{
  "mode": "client",
  "client": {
    "server_ip": "192.168.1.100",
    "server_port": 2222,
    "client_name": "gpu-worker"
  },
  "search": {
    "threads": 16,
    "gpu_mode": "full"
  }
}
```

### BSGS for Puzzle 135

```json
{
  "mode": "server",
  "puzzle": {
    "number": 135,
    "address": "16jY7qLJnxb7CHZyqBP8qca9d51gAjyXQN",
    "range_start": "0x4000000000000000000000000000000000",
    "range_end": "0x7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
    "has_pubkey": true,
    "pubkey": "02abc123..."
  },
  "server": {
    "port": 2222,
    "work_unit_size": "0x10000000000000000"
  },
  "search": {
    "mode": "bsgs",
    "threads": 16
  },
  "bsgs": {
    "n_value": "0x1000000000000",
    "k_factor": 1,
    "save_tables": true
  }
}
```

## Validation

The wizard validates configuration on load:

- Required fields present
- Numeric values in valid ranges
- Hex values properly formatted
- Server reachable (client mode)
- Sufficient resources available

Invalid configuration shows error:

```
[!] Configuration error: server.port must be 1-65535
[!] Using default value: 2222
```

## Environment Variables

Override configuration with environment variables:

| Variable | Overrides |
|----------|-----------|
| `KEYHUNT_THREADS` | search.threads |
| `KEYHUNT_GPU_MODE` | search.gpu_mode |
| `KEYHUNT_SERVER_IP` | client.server_ip |
| `KEYHUNT_SERVER_PORT` | server.port / client.server_port |

Example:
```bash
KEYHUNT_THREADS=32 ./keyhunt --wizard
```

## See Also

- [Wizard Guide](wizard-guide.md) - Interactive wizard walkthrough
- [CLI Options](../reference/cli-options.md) - Command-line equivalents
