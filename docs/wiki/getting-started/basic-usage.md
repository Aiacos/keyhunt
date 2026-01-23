# Basic Usage Guide

This guide explains keyhunt's command-line interface and common usage patterns.

## Command Structure

```bash
./keyhunt [OPTIONS]
```

All options use either short (`-x`) or long (`--xxx`) format.

## Essential Options

### Mode Selection (`-m`)

```bash
./keyhunt -m <mode>
```

| Mode | Description | When to Use |
|------|-------------|-------------|
| `address` | Bitcoin address search | Only have address, no public key |
| `bsgs` | Baby-Step Giant-Step | Have public key, need sqrt(N) speedup |
| `xpoint` | X-coordinate search | Have public key, smaller ranges |
| `rmd160` | RIPEMD160 hash search | Have raw hash160 |
| `vanity` | Vanity address generator | Create custom prefixes |
| `pub2rmd` | Public key recovery | Attempt hash reversal |

### Target File (`-f`)

```bash
./keyhunt -m address -f targets.txt
```

The file format depends on the mode:

**ADDRESS mode** (one address per line):
```
1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH
1PWCx5fovoEaoBowAvF5k91m2Xat9bMgwb
```

**BSGS/XPOINT mode** (public key, compressed or uncompressed):
```
02145d2611c823a396ef6712ce0f712f09b9b4f3135e3e0aa3230fb9b6d08d1e16
```

**RMD160 mode** (raw 40-character hex):
```
abcd1234567890abcdef1234567890abcdef1234
```

### Search Range

Two ways to specify the search range:

**Bit range (`-b`)**:
```bash
./keyhunt -m address -f target.txt -b 66
```
This searches from 2^65 to 2^66 (66-bit keys).

**Explicit hex range (`-r`)**:
```bash
./keyhunt -m address -f target.txt -r 400000000000000000:7FFFFFFFFFFFFFFFFF
```
Format is `START:END` in hexadecimal.

### Thread Count (`-t`)

```bash
./keyhunt -m address -f target.txt -b 66 -t 16
```

Match to your CPU core count for best performance:
```bash
./keyhunt -m address -f target.txt -b 66 -t $(nproc)
```

### Key Type (`-l`)

```bash
./keyhunt -m address -f target.txt -b 66 -l compress
```

| Option | Description | Speed |
|--------|-------------|-------|
| `compress` | Compressed keys only | Fastest (2x) |
| `uncompress` | Uncompressed keys only | Normal |
| `both` | Check both types | Slowest |

Most modern wallets use compressed keys.

## Search Strategies

### Random Mode (`-R`)

```bash
./keyhunt -m address -f target.txt -b 71 -R
```

Samples random keys instead of sequential search. Useful for:
- Very large ranges where sequential search is impractical
- Statistical coverage of a range
- When you don't need to exhaustively search

### BSGS Parameters

For BSGS mode, you can tune memory usage:

```bash
./keyhunt -m bsgs -f pubkey.txt -b 125 -n 0x100000000000 -k 2
```

| Parameter | Description | Effect |
|-----------|-------------|--------|
| `-n` | N value (baby steps) | More N = more RAM, fewer iterations |
| `-k` | K multiplier | More K = more RAM, faster overall |

Let auto-tuning choose if unsure:
```bash
./keyhunt -m bsgs -f pubkey.txt -b 125
```

### Save/Load BSGS Tables (`-S`)

```bash
./keyhunt -m bsgs -f pubkey.txt -b 125 -S
```

Saves bloom filter tables to disk. On restart, tables are loaded instead of recomputed (saves significant startup time).

## Output Control

### Status Interval (`-s`)

```bash
./keyhunt -m address -f target.txt -b 66 -s 30
```

Print status every 30 seconds (default: 10 seconds).

### Quiet Mode (`-q`)

```bash
./keyhunt -m address -f target.txt -b 66 -q
```

Reduces output for long-running searches.

## GPU Options

### GPU Mode (`-G`)

```bash
./keyhunt -m address -f target.txt -b 66 -G hybrid
```

| Mode | Description |
|------|-------------|
| `off` | CPU only (default) |
| `auto` | Auto-detect best mode |
| `hash` | GPU handles hashing only |
| `full` | GPU handles entire pipeline |
| `hybrid` | Combined CPU+GPU (fastest) |

## Special Modes

### Benchmark

```bash
./keyhunt --benchmark
```

Tests your system and shows optimal settings.

### Interactive Wizard

```bash
./keyhunt --wizard
./keyhunt -W
```

Guided setup for distributed puzzle solving.

### Version

```bash
./keyhunt --version
./keyhunt -v
```

Shows version and build information.

### Help

```bash
./keyhunt --help
./keyhunt -h
```

Shows all available options.

## Example Commands

### Puzzle 66 (no public key)
```bash
./keyhunt -m address -f puzzle66.txt \
  -r 20000000000000000:3FFFFFFFFFFFFFFFF \
  -t 16 -l compress -R -q -s 30
```

### Puzzle 135 (has public key)
```bash
./keyhunt -m bsgs -f puzzle135_pubkey.txt \
  -b 135 -S -q -s 60
```

### Distributed client
```bash
./keyhunt --wizard
# Select "Client" mode and enter server IP
```

### Quick XPOINT search
```bash
./keyhunt -m xpoint -f tests/120.txt -b 125 -R -q -t 16
```

## Parameter Validation

Keyhunt automatically validates parameters and provides feedback:

```
[+] Parameter Validation:
  Threads: 16 (optimal for 16-core CPU)
  Batch size: 1024 (cache-aligned)
  N value: 0x100000000000 (fits in 24GB RAM)
```

Symbols:
- Green checkmark: Optimal parameter
- Blue info: Works but not optimal
- Yellow warning: Auto-corrected for safety
- Red warning: May cause performance issues

## Environment Variables

| Variable | Description |
|----------|-------------|
| `KEYHUNT_SKIP_SYSINFO` | Set to `1` to disable hardware auto-detection |
| `KEYHUNT_PROGRESS_DIR` | Custom progress directory |

## Next Steps

- [Address Mode](../modes/address-mode.md) - Detailed ADDRESS mode guide
- [BSGS Mode](../modes/bsgs-mode.md) - Detailed BSGS mode guide
- [CLI Options Reference](../reference/cli-options.md) - Complete option reference
