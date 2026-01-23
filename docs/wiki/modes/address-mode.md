# Address Mode

Address mode performs brute-force search for Bitcoin addresses using bloom filters for fast negative lookups.

## Overview

| Aspect | Details |
|--------|---------|
| Command | `-m address` |
| Input | Bitcoin addresses (Base58Check format) |
| Complexity | O(N) - linear with range size |
| Best for | Targets without known public key |
| Typical speed | 50-100 Mkeys/s (CPU), 400+ Mkeys/s (GPU hybrid) |

## How It Works

1. **Key Generation**: Generate private keys sequentially or randomly in the search range
2. **Point Multiplication**: Compute public key: `P = k * G` (where G is generator point)
3. **Hash Pipeline**: `SHA256(compressed_pubkey)` -> `RIPEMD160(sha256_result)` -> Hash160
4. **Address Encoding**: Convert Hash160 to Base58Check Bitcoin address
5. **Bloom Filter Check**: Fast probabilistic check against target addresses
6. **Binary Search**: If bloom filter hits, confirm with exact binary search

The RIPEMD160 step is the bottleneck, optimized with SIMD (SSE2/AVX2/AVX-512).

## Basic Usage

```bash
./keyhunt -m address -f targets.txt -b 66 -t 16 -l compress
```

## Target File Format

One Bitcoin address per line:

```
1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH
1PWCx5fovoEaoBowAvF5k91m2Xat9bMgwb
3CnQvGgJkGNzMNpH8NMM9w7vCGxQG3Hmyn
```

Supports:
- Legacy addresses (starting with 1)
- P2SH addresses (starting with 3)
- Both compressed and uncompressed derivations

## Key Parameters

### Range Specification

**Bit range** (recommended for puzzles):
```bash
./keyhunt -m address -f target.txt -b 66
```
Searches from `2^65` to `2^66 - 1`.

**Explicit hex range**:
```bash
./keyhunt -m address -f target.txt -r 20000000000000000:3FFFFFFFFFFFFFFFF
```

### Key Type Selection

```bash
# Compressed only (2x faster, most common)
./keyhunt -m address -f target.txt -b 66 -l compress

# Uncompressed only
./keyhunt -m address -f target.txt -b 66 -l uncompress

# Both types (slowest)
./keyhunt -m address -f target.txt -b 66 -l both
```

**Important**: 95%+ of modern Bitcoin addresses use compressed keys. Only use `both` if you're unsure.

### Search Strategy

**Sequential** (default):
```bash
./keyhunt -m address -f target.txt -b 66
```
Searches from start to end of range. Guaranteed coverage.

**Random** (`-R`):
```bash
./keyhunt -m address -f target.txt -b 66 -R
```
Random sampling within range. Best for very large ranges where sequential is impractical.

## Performance Optimization

### Thread Count

Match to physical CPU cores:
```bash
./keyhunt -m address -f target.txt -b 66 -t $(nproc)
```

Hyperthreading provides ~20% boost, so using logical core count is fine.

### GPU Acceleration

Enable hybrid mode for best performance:
```bash
./keyhunt -m address -f target.txt -b 66 -G hybrid
```

| Mode | Description | Speed Boost |
|------|-------------|-------------|
| `off` | CPU only | Baseline |
| `hash` | GPU handles SHA256+RIPEMD160 | 2-3x |
| `full` | GPU handles entire pipeline | 3-4x |
| `hybrid` | CPU+GPU parallel | 4-5x |

### Quiet Mode

For long searches, reduce output overhead:
```bash
./keyhunt -m address -f target.txt -b 66 -q -s 60
```

## Bloom Filter Mechanics

Keyhunt uses a 3-tier bloom filter hierarchy for fast negative lookups:

| Level | Purpose | False Positive Rate |
|-------|---------|---------------------|
| bloom1 | Primary filter | ~0.1% |
| bloom2 | Secondary (1/32 size) | ~3% |
| bloom3 | Tertiary (1/1024 size) | ~10% |

When all three filters report a potential match, a binary search confirms against the sorted target list.

**Memory usage**: Approximately 4 bytes per target address for bloom filters.

## Example Scenarios

### Bitcoin Puzzle 66

```bash
./keyhunt -m address -f puzzle66.txt \
  -r 20000000000000000:3FFFFFFFFFFFFFFFF \
  -t 16 -l compress -R -q -s 30
```

### Multiple Target Addresses

```bash
# targets.txt contains 1000 addresses
./keyhunt -m address -f targets.txt -b 70 -t 32 -l compress
```

Bloom filters scale efficiently to thousands of targets.

### Resume After Interruption

Progress is automatically saved. Just restart with same parameters:
```bash
./keyhunt -m address -f target.txt -b 66 -t 16
# Automatically resumes from last saved position
```

## Output Interpretation

```
[+] Version 0.2.230731 Satoshi Quest
[+] Mode: ADDRESS
[+] Search: COMPRESSED
[+] Threads: 16
[+] Range: 20000000000000000 to 3FFFFFFFFFFFFFFFF
[+] N = 0x1FFFFFFFFFFFFFFFF
[+] Loaded 1 addresses
[+] Bloom filter: 262144 bytes (3 levels)
[+] Speed: 86.42 Mkeys/s (Avg), Total: 8.64 Gkeys (Time: 100.00s)
```

### When Key is Found

```
[+] Found: 1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH
    Private Key: 0x20000000000000001
    WIF: KwDiBf89QgGbjEhKnhXJuH7LrciVrZi3qYjgd9M7rFU73sVHnoWn
```

The private key is saved to `KEYFOUNDKEYFOUND.txt` and `found_keys.json`.

## Limitations

- **O(N) complexity**: Exponentially harder as range size increases
- **66+ bit ranges**: Require massive parallelization or luck with random mode
- **No public key advantage**: Cannot use BSGS optimization without public key

## When to Use Another Mode

| Situation | Recommended Mode |
|-----------|------------------|
| Have public key | [BSGS Mode](bsgs-mode.md) (sqrt(N) speedup) |
| Have X-coordinate only | [XPoint Mode](xpoint-mode.md) |
| Have RIPEMD160 hash | [RMD160 Mode](rmd160-mode.md) |
| Want vanity address | [Vanity Mode](vanity-mode.md) |

## See Also

- [BSGS Mode](bsgs-mode.md) - For targets with known public keys
- [CPU Tuning](../optimization/cpu-tuning.md) - Optimize performance
- [GPU Setup](../optimization/gpu-setup.md) - Enable GPU acceleration
- [Distributed Overview](../distributed/overview.md) - Multi-machine search
