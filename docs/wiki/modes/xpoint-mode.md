# XPoint Mode

XPoint mode searches for public keys by matching only the X-coordinate, which is faster than full point comparison.

## Overview

| Aspect | Details |
|--------|---------|
| Command | `-m xpoint` |
| Input | Public key X-coordinates (32 bytes hex) |
| Complexity | O(N) - linear with range size |
| Best for | Known public keys, when BSGS memory is unavailable |
| Advantage | Faster than address mode for known pubkeys |

## How It Works

1. **Key Generation**: Generate private keys in the search range
2. **Point Multiplication**: Compute `P = k * G`
3. **X-Coordinate Extraction**: Take only the X-coordinate of point P
4. **Comparison**: Match against target X-coordinates

**Why faster than ADDRESS mode**: Skips the hash pipeline (SHA256 + RIPEMD160 + Base58).

## Basic Usage

```bash
./keyhunt -m xpoint -f xpoints.txt -b 125 -t 16
```

## Target File Format

X-coordinates only (64 hex characters, 32 bytes):

```
145d2611c823a396ef6712ce0f712f09b9b4f3135e3e0aa3230fb9b6d08d1e16
a2b3c4d5e6f7890123456789abcdef0123456789abcdef0123456789abcdef01
```

**Note**: Do not include the `02`/`03` prefix from compressed public keys.

### Extracting X-Coordinate from Public Key

From a compressed public key:
```
02145d2611c823a396ef6712ce0f712f09b9b4f3135e3e0aa3230fb9b6d08d1e16
^^-- prefix (remove this)
  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^-- X-coordinate
```

From uncompressed public key (starts with 04):
```
04[X-coordinate 64 chars][Y-coordinate 64 chars]
```

## Key Parameters

### Range Specification

```bash
# Bit range
./keyhunt -m xpoint -f xpoints.txt -b 125

# Explicit hex range
./keyhunt -m xpoint -f xpoints.txt -r 4000000000000000000000000000000:7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFF
```

### Thread Count

```bash
./keyhunt -m xpoint -f xpoints.txt -b 125 -t $(nproc)
```

### Random Mode

```bash
./keyhunt -m xpoint -f xpoints.txt -b 125 -R
```

## Performance Comparison

| Mode | Operations per Key | Relative Speed |
|------|-------------------|----------------|
| XPOINT | EC multiplication only | Fastest |
| ADDRESS | EC mult + SHA256 + RIPEMD160 + Base58 | ~50% slower |
| BSGS | Lookup only (after setup) | Depends on table size |

## When to Use XPoint

**Use XPoint when**:
- You have the public key
- Memory is insufficient for BSGS
- You want simpler O(N) search without hash overhead

**Use BSGS instead when**:
- You have sufficient RAM
- Range is large enough that sqrt(N) advantage matters

**Use ADDRESS instead when**:
- You only have the Bitcoin address, not the public key

## Example Scenarios

### Quick Puzzle Test

```bash
./keyhunt -m xpoint -f tests/120.txt -b 125 -R -q -t 16
```

### Multiple X-Coordinates

```bash
# xpoints.txt contains 100 X-coordinates
./keyhunt -m xpoint -f xpoints.txt -b 80 -t 32
```

## Output

```
[+] Version 0.2.230731 Satoshi Quest
[+] Mode: XPOINT
[+] Threads: 16
[+] Range: 40000000000000000000000000000000 to 7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF
[+] Loaded 1 xpoints
[+] Speed: 92.5 Mkeys/s
```

### When Match Found

```
[+] Found X-Point match!
    X-Coordinate: 145d2611c823a396ef6712ce0f712f09b9b4f3135e3e0aa3230fb9b6d08d1e16
    Private Key: 0x4000000000000000000000000000001234
    Full Public Key: 02145d2611c823a396ef6712ce0f712f09b9b4f3135e3e0aa3230fb9b6d08d1e16
```

## Limitations

- Still O(N) complexity (no sqrt speedup like BSGS)
- Requires extracting X-coordinate from public key
- Cannot use if you only have the Bitcoin address

## See Also

- [BSGS Mode](bsgs-mode.md) - O(sqrt(N)) for known public keys
- [Address Mode](address-mode.md) - When you only have the address
- [CPU Tuning](../optimization/cpu-tuning.md) - Performance optimization
