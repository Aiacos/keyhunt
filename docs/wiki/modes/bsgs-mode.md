# BSGS Mode

Baby-Step Giant-Step (BSGS) mode provides O(sqrt(N)) complexity for targets with known public keys, making it vastly more efficient than brute force.

## Overview

| Aspect | Details |
|--------|---------|
| Command | `-m bsgs` |
| Input | Public keys (compressed or uncompressed) |
| Complexity | O(sqrt(N)) - square root of range size |
| Best for | Puzzles with exposed public keys (135+) |
| Memory | High (GB to TB depending on range) |

## Algorithm Explanation

The BSGS algorithm trades memory for time:

1. **Baby Steps**: Pre-compute `m = sqrt(N)` points: `P_i = i * G` for i = 0 to m-1
2. **Store**: Save baby steps in a bloom filter + lookup table
3. **Giant Steps**: For target point `Q`, compute `Q - j*m*G` for increasing j
4. **Match**: When giant step matches a baby step, the key is `k = i + j*m`

**Why it's faster**: Instead of N operations, you need only 2*sqrt(N) operations.

| Range Size | Brute Force | BSGS |
|------------|-------------|------|
| 2^60 | 2^60 ops | 2^31 ops |
| 2^80 | 2^80 ops | 2^41 ops |
| 2^100 | 2^100 ops | 2^51 ops |
| 2^125 | 2^125 ops | 2^63 ops |

## Basic Usage

```bash
./keyhunt -m bsgs -f pubkey.txt -b 125
```

## Target File Format

One public key per line (compressed or uncompressed):

```
# Compressed (33 bytes, starts with 02 or 03)
02145d2611c823a396ef6712ce0f712f09b9b4f3135e3e0aa3230fb9b6d08d1e16

# Uncompressed (65 bytes, starts with 04)
04145d2611c823a396ef6712ce0f712f09b9b4f3135e3e0aa3230fb9b6d08d1e16abcdef...
```

## Memory Requirements

BSGS is memory-intensive. The formula:

```
M = sqrt(N)
K = multiplier factor

Total RAM = (M * K * 3.5) + (M * K * 3.5 / 32) + (M * K * 3.5 / 1024) + (M / 32 * K * 16) bytes
```

Simplified: approximately **20 bytes per baby step entry**.

| Range (bits) | sqrt(N) | RAM Needed |
|--------------|---------|------------|
| 60 | 2^30 | ~20 GB |
| 70 | 2^35 | ~640 GB |
| 80 | 2^40 | ~20 TB |
| 125 | 2^62.5 | Impractical |

**Key insight**: You don't need to cover the entire sqrt(N). Use multiple iterations with smaller tables.

## Key Parameters

### N Value (`-n`)

Controls the number of baby steps (table size):

```bash
./keyhunt -m bsgs -f pubkey.txt -b 125 -n 0x100000000000
```

Larger N = more RAM, fewer iterations needed.

### K Factor (`-k`)

Multiplier for table size:

```bash
./keyhunt -m bsgs -f pubkey.txt -b 125 -n 0x100000000000 -k 2
```

K=2 doubles the table size and halves iterations.

### Auto-Tuning

Let keyhunt calculate optimal values based on available RAM:

```bash
./keyhunt -m bsgs -f pubkey.txt -b 125
```

The parameter validator ensures N and K fit in 60% of available RAM.

## Save/Load Tables (`-S`)

Pre-computed tables can be saved and reused:

```bash
# First run: compute and save tables
./keyhunt -m bsgs -f pubkey.txt -b 125 -S

# Subsequent runs: load from disk (much faster startup)
./keyhunt -m bsgs -f pubkey.txt -b 125 -S
```

Saved files:
- `keyhunt_bsgs_*.blm` - Bloom filter data
- `keyhunt_bsgs_*.tbl` - Baby step lookup table

## BSGS Sub-Modes

Control how the giant steps traverse the range:

```bash
./keyhunt -m bsgs -f pubkey.txt -b 125 --bsgs-mode <mode>
```

| Mode | Description | Use Case |
|------|-------------|----------|
| `sequential` | Start to end | Complete coverage |
| `backward` | End to start | Alternative direction |
| `both` | Both directions simultaneously | Faster coverage |
| `random` | Random giant steps | Very large ranges |
| `dance` | Alternating pattern | Balanced coverage |

## Memory Validation

Keyhunt validates memory requirements before allocation:

```
[+] BSGS Memory Check:
    Available RAM: 32.0 GB
    Required RAM: 24.5 GB
    Status: OK (76% utilization)
```

If insufficient:
```
[!] BSGS Memory Check:
    Available RAM: 8.0 GB
    Required RAM: 24.5 GB
    Status: INSUFFICIENT
    Suggestion: Use -n 0x40000000000 (fits in 6.0 GB)
```

## Performance Optimization

### Batched Processing

BSGS uses batched operations for efficiency:
- 64 points processed per batch
- Prefetching for bloom filter access
- Vectorized inner loops

### Endomorphism (`-e`)

For searching the full curve (not recommended for puzzles):

```bash
./keyhunt -m bsgs -f pubkey.txt -b 125 -e
```

Uses secp256k1 endomorphism to check 6 related keys per computation.

**Note**: Only beneficial for full-curve searches, not bit-range puzzles.

## Example Scenarios

### Bitcoin Puzzle 135

```bash
./keyhunt -m bsgs -f puzzle135_pubkey.txt -b 135 -S -q -s 60
```

### Custom Range with Specific N

```bash
./keyhunt -m bsgs -f pubkey.txt \
  -r 40000000000000000000000000000000:7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF \
  -n 0x1000000000000 -k 1 -S
```

### Multiple Public Keys

```bash
# pubkeys.txt contains 10 public keys
./keyhunt -m bsgs -f pubkeys.txt -b 125 -S
```

Each public key is checked against the same baby step table.

## Output Interpretation

```
[+] Version 0.2.230731 Satoshi Quest
[+] Mode: BSGS
[+] Range: 40000000000000000000000000000000 to 7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF
[+] N = 0x100000000000 (1099511627776)
[+] K = 1
[+] Baby steps: 1099511627776
[+] Giant step size: 0x100000000000
[+] Bloom filter: 3.5 GB
[+] bP Table: 512 MB
[+] Total RAM: 4.0 GB
[+] Loading saved tables... done (2.3s)
[+] Iterations: 1/16777216
[+] Speed: 125.4 Mkeys/s equivalent
```

### When Key is Found

```
[+] Found public key match!
    Public Key: 02145d2611c823a396ef6712ce0f712f09b9b4f3135e3e0aa3230fb9b6d08d1e16
    Private Key: 0x4000000000000000000000000000001234
    Baby step: 1234
    Giant step: 4
    WIF: L1aW4aubDFB7yfras2S1mN3bqg9nwySY8nkoLmJebSLD5BWv3ENZ
```

## Troubleshooting

### "Not enough memory"

Options:
1. Reduce N value: `-n 0x10000000000`
2. Use swap (slow but works)
3. Use distributed mode to split work

### Slow startup

Table computation is slow. Use `-S` to save and reuse tables.

### "Bloom filter file corrupted"

Delete the `.blm` files and let keyhunt regenerate:
```bash
rm keyhunt_bsgs_*.blm keyhunt_bsgs_*.tbl
```

## BSGS vs Other Modes

| Mode | Complexity | Memory | Best For |
|------|------------|--------|----------|
| BSGS | O(sqrt(N)) | High | Known public key, smaller ranges |
| XPOINT | O(N) | Low | Known public key, want simplicity |
| ADDRESS | O(N) | Low | No public key available |

## Theoretical Limits

Even with BSGS optimization:
- **125-bit key**: sqrt(2^125) = 2^62.5 operations still massive
- **Distributed computing** essential for large puzzles
- **Memory** becomes the bottleneck, not CPU

## See Also

- [Address Mode](address-mode.md) - For targets without public keys
- [XPoint Mode](xpoint-mode.md) - Simpler public key search
- [Memory Optimization](../optimization/memory-optimization.md) - BSGS memory tips
- [Distributed Overview](../distributed/overview.md) - Scale across machines
