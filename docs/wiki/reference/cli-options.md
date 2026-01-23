# CLI Options Reference

Complete command-line reference for keyhunt.

## Synopsis

```
keyhunt [OPTIONS]
keyhunt --wizard
keyhunt --benchmark
keyhunt --help
```

## General Options

### -h, --help

Display help message and exit.

```bash
./keyhunt --help
```

### -v, --version

Display version information and exit.

```bash
./keyhunt --version
```

### --wizard, -W

Start interactive wizard for guided setup.

```bash
./keyhunt --wizard
./keyhunt -W
```

### --benchmark

Run performance benchmark.

```bash
./keyhunt --benchmark
```

## Mode Selection

### -m, --mode MODE

Select search mode.

| Mode | Description |
|------|-------------|
| `address` | Bitcoin address search |
| `bsgs` | Baby-Step Giant-Step |
| `xpoint` | X-coordinate search |
| `rmd160` | RIPEMD160 hash search |
| `vanity` | Vanity address generation |
| `pub2rmd` | Public key recovery |

```bash
./keyhunt -m address -f target.txt -b 66
./keyhunt -m bsgs -f pubkey.txt -b 125
```

## Input/Output Options

### -f, --file FILE

Target file containing addresses, public keys, or hashes.

```bash
./keyhunt -m address -f targets.txt -b 66
```

File format depends on mode:
- ADDRESS: Bitcoin addresses
- BSGS/XPOINT: Public keys
- RMD160: RIPEMD160 hashes

### -o, --output FILE

Output file for found keys (default: KEYFOUNDKEYFOUND.txt).

```bash
./keyhunt -m address -f target.txt -b 66 -o found.txt
```

## Range Specification

### -b, --bit BIT

Search N-bit range (from 2^(N-1) to 2^N - 1).

```bash
./keyhunt -m address -f target.txt -b 66
# Searches: 0x20000000000000000 to 0x3FFFFFFFFFFFFFFFF
```

### -r, --range START:END

Explicit hexadecimal range.

```bash
./keyhunt -m address -f target.txt -r 20000000000000000:3FFFFFFFFFFFFFFFF
```

## Performance Options

### -t, --threads N

Number of CPU threads.

```bash
./keyhunt -m address -f target.txt -b 66 -t 16
./keyhunt -m address -f target.txt -b 66 -t $(nproc)
```

### --batch-size N

Keys per batch (default: 1024).

```bash
./keyhunt -m address -f target.txt -b 66 --batch-size 2048
```

## Key Type Options

### -l, --look TYPE

Key type to search.

| Type | Description |
|------|-------------|
| `compress` | Compressed keys only (faster) |
| `uncompress` | Uncompressed keys only |
| `both` | Both types (slower) |

```bash
./keyhunt -m address -f target.txt -b 66 -l compress
```

## Search Strategy

### -R, --random

Random key sampling instead of sequential.

```bash
./keyhunt -m address -f target.txt -b 66 -R
```

### -e, --endomorphism

Enable endomorphism optimization (6 keys per computation).

```bash
./keyhunt -m address -f target.txt -b 66 -e
```

Note: Only beneficial for full-curve searches.

## BSGS Options

### -n, --n-value VALUE

N value (baby step count) in hexadecimal.

```bash
./keyhunt -m bsgs -f pubkey.txt -b 125 -n 0x100000000000
```

### -k, --k-factor K

K multiplier for table size.

```bash
./keyhunt -m bsgs -f pubkey.txt -b 125 -k 2
```

### -S, --save-tables

Save/load BSGS tables to disk.

```bash
./keyhunt -m bsgs -f pubkey.txt -b 125 -S
```

### --bsgs-mode MODE

BSGS search direction.

| Mode | Description |
|------|-------------|
| `sequential` | Start to end |
| `backward` | End to start |
| `both` | Both directions |
| `random` | Random giant steps |
| `dance` | Alternating pattern |

```bash
./keyhunt -m bsgs -f pubkey.txt -b 125 --bsgs-mode both
```

## GPU Options

### -G, --gpu MODE

GPU acceleration mode.

| Mode | Description |
|------|-------------|
| `off` | CPU only (default) |
| `auto` | Auto-detect best |
| `hash` | GPU hashing only |
| `full` | GPU entire pipeline |
| `hybrid` | CPU + GPU parallel |

```bash
./keyhunt -m address -f target.txt -b 66 -G hybrid
```

### --gpu-threads N

Threads per GPU block.

```bash
./keyhunt -m address -f target.txt -b 66 -G full --gpu-threads 256
```

### --gpu-blocks N

Number of GPU blocks.

```bash
./keyhunt -m address -f target.txt -b 66 -G full --gpu-blocks 1024
```

## Vanity Options

### -v, --vanity PREFIX

Vanity address prefix.

```bash
./keyhunt -m vanity -v 1Love
```

## Output Control

### -q, --quiet

Reduce output verbosity.

```bash
./keyhunt -m address -f target.txt -b 66 -q
```

### -s, --status SECONDS

Status print interval (default: 10).

```bash
./keyhunt -m address -f target.txt -b 66 -s 30
```

### --verbose

Enable verbose/debug output.

```bash
./keyhunt -m address -f target.txt -b 66 --verbose
```

## Distributed Mode Options

### --server

Run as coordinator server.

```bash
./keyhunt -m address -f target.txt -b 66 --server
```

### --client

Run as worker client.

```bash
./keyhunt --client --server-ip 192.168.1.100
```

### --server-ip IP

Coordinator IP address (for client mode).

```bash
./keyhunt --client --server-ip 192.168.1.100
```

### --port N

Network port (default: 2222).

```bash
./keyhunt -m address -f target.txt -b 66 --server --port 3333
```

### --client-name NAME

Client identifier (default: hostname).

```bash
./keyhunt --client --server-ip 192.168.1.100 --client-name worker1
```

### --work-unit-size SIZE

Work unit size in hexadecimal.

```bash
./keyhunt ... --server --work-unit-size 0x100000000000
```

### --checkpoint-interval N

Checkpoint save interval in seconds (default: 60).

```bash
./keyhunt ... --server --checkpoint-interval 30
```

## Examples

### Basic ADDRESS Mode

```bash
./keyhunt -m address -f targets.txt -b 66 -t 16 -l compress
```

### BSGS with Saved Tables

```bash
./keyhunt -m bsgs -f pubkey.txt -b 125 -S -q -s 60
```

### GPU Hybrid Mode

```bash
./keyhunt -m address -f target.txt -b 66 -G hybrid -t 16
```

### Distributed Server

```bash
./keyhunt -m address -f target.txt -b 66 \
  --server --port 2222 \
  --work-unit-size 0x100000000000 \
  --checkpoint-interval 60
```

### Distributed Client

```bash
./keyhunt --client \
  --server-ip 192.168.1.100 \
  --port 2222 \
  --client-name worker1
```

### Random Search with Quiet Output

```bash
./keyhunt -m address -f target.txt -b 71 -R -t 32 -q -s 60
```

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success (key found or normal exit) |
| 1 | General error |
| 2 | Invalid arguments |
| 3 | File not found |
| 4 | Memory allocation failure |
| 5 | Network error |

## Environment Variables

| Variable | Description |
|----------|-------------|
| `KEYHUNT_SKIP_SYSINFO` | Disable hardware auto-detection |
| `KEYHUNT_PROGRESS_DIR` | Custom progress directory |
| `CUDA_VISIBLE_DEVICES` | Select GPU (e.g., "0" or "0,1") |

```bash
KEYHUNT_SKIP_SYSINFO=1 ./keyhunt -m address -f target.txt -b 66
CUDA_VISIBLE_DEVICES=0 ./keyhunt -m address -f target.txt -b 66 -G full
```

## See Also

- [Quick Start](../getting-started/quick-start.md) - Getting started guide
- [Configuration Files](configuration-files.md) - File formats
- [FAQ](faq.md) - Frequently asked questions
