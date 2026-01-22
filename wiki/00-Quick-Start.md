# Keyhunt Quick Start Guide

This guide will get you up and running with keyhunt in 5 minutes.

## Prerequisites

- **Linux** (Ubuntu 20.04+, Fedora 35+ recommended)
- **GCC** with C++17 support (GCC 8+ or Clang 10+)
- **RAM Requirements**:
  - ADDRESS mode: 2GB minimum
  - BSGS mode: 8GB minimum (16GB+ recommended)

## Build

```bash
git clone https://github.com/albertobsd/keyhunt.git
cd keyhunt
make clean && make
```

The build auto-detects your CPU features (AVX2, AVX-512, SHA-NI) and compiles optimized code paths accordingly.

## 5-Minute Test

Verify your installation works correctly:

```bash
# Test ADDRESS mode (finds keys 1-32)
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -t 4
```

Expected output: The program should quickly find all 32 private keys in the test file. You will see output like:

```
[+] Found: 1PWCx5fovoEaoBowAvF5k91m2Xat9bMgwb (Private Key: 0x1)
[+] Found: 3CnQvG... (Private Key: 0x2)
...
```

If all 32 keys are found, your installation is working correctly.

## Easy Setup: Interactive Wizard (NEW)

The easiest way to configure keyhunt is using the interactive wizard:

```bash
./keyhunt --wizard    # or ./keyhunt -W
```

The wizard will:
1. Download the latest puzzle database
2. Auto-detect your hardware (CPU, RAM, GPU)
3. Calculate optimal parameters for your chosen puzzle
4. Set up distributed mode (server/client)
5. Integrate community progress data

### Run Benchmark First

Before searching, run the benchmark to see your system's performance:

```bash
./keyhunt --benchmark
```

This will test CPU and GPU speed and recommend optimal settings.

**Recommended for:**
- New users
- Distributed multi-PC setups
- Puzzles with public keys (auto-selects BSGS)

See [Interactive Wizard Guide](04-Interactive-Wizard.md) for details.

## Basic Usage Patterns

### 1. ADDRESS Mode (Brute Force)

ADDRESS mode searches for Bitcoin addresses by brute-forcing private keys in a specified range.

```bash
# Basic syntax
./keyhunt -m address -f target.txt -r START:END -t THREADS

# Puzzle 71 example (searching 71-bit range)
./keyhunt -m address -f puzzle71.txt -r 400000000000000000:7FFFFFFFFFFFFFFFFF -t 16 -l compress
```

### 2. BSGS Mode (When Public Key is Known)

BSGS (Baby Step Giant Step) is dramatically faster when you know the target public key. It trades memory for speed.

```bash
# Basic syntax
./keyhunt -m bsgs -f target_pubkey.txt -b BITS -n N_VALUE -k K_FACTOR

# Example for 125-bit range
./keyhunt -m bsgs -f tests/125.txt -b 125 -n 0x100000000000 -k 2

# Let keyhunt auto-tune parameters based on your RAM
./keyhunt -m bsgs -f tests/125.txt -b 125
```

### 3. Random Search Mode

Random mode samples random keys within a range instead of sequential searching. Useful for very large ranges.

```bash
# Random sampling within bit range
./keyhunt -m address -f target.txt -b 71 -R -t 16

# Random with explicit range
./keyhunt -m address -f target.txt -r 400000000000000000:7FFFFFFFFFFFFFFFFF -R -t 16
```

### 4. XPOINT Mode (X-Coordinate Search)

When you know the public key, searching for just the X-coordinate is faster than BSGS for smaller ranges.

```bash
./keyhunt -m xpoint -f tests/120.txt -b 125 -R -q -t 16
```

## Key Parameters Explained

| Parameter | Description | Example |
|-----------|-------------|---------|
| `-m` | Mode (address, bsgs, rmd160, xpoint, vanity) | `-m address` |
| `-f` | Target file (address or pubkey) | `-f puzzle71.txt` |
| `-r` | Range (hex START:END) | `-r 400000000000000000:4FFFFFFFFFFFFFFFF` |
| `-b` | Bit range (alternative to -r) | `-b 71` |
| `-t` | Thread count | `-t 16` |
| `-l` | Key type (compress/uncompress/both) | `-l compress` |
| `-R` | Random search mode | `-R` |
| `-q` | Quiet mode (less output) | `-q` |
| `-n` | BSGS: N value (baby steps) | `-n 0x100000000000` |
| `-k` | BSGS: K multiplier | `-k 2` |
| `-S` | Save/load BSGS bloom tables | `-S` |
| `-s` | Status interval (seconds) | `-s 10` |
| `--benchmark` | Run performance benchmark | `--benchmark` |
| `--wizard` | Interactive setup wizard | `--wizard` |

## Target File Formats

### For ADDRESS mode (one address per line):

```
1PWCx5fovoEaoBowAvF5k91m2Xat9bMgwb
1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH
```

### For BSGS/XPOINT mode (public key, compressed or uncompressed):

```
02145d2611c823a396ef6712ce0f712f09b9b4f3135e3e0aa3230fb9b6d08d1e16
```

Or uncompressed (04 prefix):

```
04145d2611c823a396ef6712ce0f712f09b9b4f3135e3e0aa3230fb9b6d08d1e16b4a2f6b1e6a7...
```

### For RMD160 mode (raw RIPEMD160 hashes):

```
abcd1234567890abcdef1234567890abcdef1234
```

## Performance Tips

1. **Use `-l compress`** for 2x speed when you know the target uses compressed keys (most modern wallets do)

2. **Match thread count to CPU cores**:
   ```bash
   # Check your core count
   nproc
   # Use that number for -t
   ./keyhunt -m address -f target.txt -b 71 -t $(nproc)
   ```

3. **For BSGS, balance N and K for available RAM**:
   - Higher N = more memory, fewer giant steps
   - Higher K = more memory, faster overall
   - Let auto-tuning choose if unsure

4. **Use `-q` (quiet mode)** to reduce output overhead during long searches

5. **Save BSGS tables with `-S`** to avoid recomputing bloom filters on restart:
   ```bash
   ./keyhunt -m bsgs -f target.txt -b 125 -S
   ```

## Choosing the Right Mode

| Scenario | Recommended Mode |
|----------|------------------|
| Only have Bitcoin address | ADDRESS |
| Have public key, small range (<80 bits) | XPOINT |
| Have public key, medium range (80-130 bits) | BSGS |
| Very large range, random sampling | ADDRESS with -R |
| Looking for vanity address | VANITY |

## Common Issues

**"Not enough memory for BSGS"**: Reduce `-n` value or let auto-tuning handle it.

**Program runs but finds nothing**: Verify your target file format and range are correct.

**Low speed**: Check that AVX2/AVX-512 is detected at startup. Ensure you compiled with `make` not `make legacy`.

## Progress Tracking

Keyhunt automatically saves your search progress to `~/.keyhunt/progress/`. This means:

- Progress is saved every 60 seconds automatically
- If you stop and restart, you can resume from where you left off
- Progress files are stored as JSON for easy inspection

To view saved progress:
```bash
ls -la ~/.keyhunt/progress/
```

## Next Steps

- Read [01-Strategy-Guide.md](01-Strategy-Guide.md) for puzzle-solving strategies
- Read [02-Algorithm-Reference.md](02-Algorithm-Reference.md) for technical details
- Read [03-Distributed-Computing.md](03-Distributed-Computing.md) for multi-PC setup
