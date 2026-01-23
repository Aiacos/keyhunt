# Quick Start Guide

Get keyhunt running in 5 minutes.

## Prerequisites

- Built keyhunt executable (see [Installation](installation.md))
- Target file with addresses or public keys
- Understanding of your search range

## 5-Minute Test

Verify your installation works:

```bash
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -t 4
```

This searches for 32 known test addresses in a small range. You should see output like:

```
[+] Found: 1PWCx5fovoEaoBowAvF5k91m2Xat9bMgwb (Private Key: 0x1)
[+] Found: 3CnQvG... (Private Key: 0x2)
...
```

If all 32 keys are found, your installation is working correctly.

## Interactive Wizard (Easiest Method)

The wizard guides you through setup with intelligent defaults:

```bash
./keyhunt --wizard    # or ./keyhunt -W
```

The wizard will:
1. Download the latest puzzle database
2. Auto-detect your hardware (CPU, RAM, GPU)
3. Calculate optimal parameters
4. Set up distributed mode (optional)
5. Start searching

**Recommended for**: New users, distributed setups, puzzles with public keys.

## Manual Setup: Three Common Scenarios

### Scenario 1: Search for Bitcoin Address (Brute Force)

You have a Bitcoin address and want to find its private key within a known range.

```bash
# Create target file
echo "1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH" > target.txt

# Search 66-bit range with 8 threads
./keyhunt -m address -f target.txt -b 66 -t 8 -l compress
```

Key parameters:
- `-m address`: Bitcoin address mode
- `-f target.txt`: File containing target address(es)
- `-b 66`: Search in 66-bit range (keys from 2^65 to 2^66)
- `-t 8`: Use 8 threads
- `-l compress`: Only check compressed keys (faster)

### Scenario 2: BSGS Mode (Known Public Key)

You have the public key (exposed from blockchain transaction). This is **much faster** than address mode.

```bash
# Create target file with public key
echo "02145d2611c823a396ef6712ce0f712f09b9b4f3135e3e0aa3230fb9b6d08d1e16" > pubkey.txt

# Search with BSGS algorithm
./keyhunt -m bsgs -f pubkey.txt -b 125
```

BSGS uses O(sqrt(N)) complexity instead of O(N), making it exponentially faster for known public keys.

### Scenario 3: Random Sampling (Very Large Ranges)

For ranges too large to search sequentially, use random mode:

```bash
./keyhunt -m address -f target.txt -b 71 -R -t 16
```

The `-R` flag enables random key sampling within the range.

## Understanding Output

Typical keyhunt output:

```
[+] Version 0.2.230731 Satoshi Quest
[+] Mode: ADDRESS
[+] Search: COMPRESSED
[+] Threads: 8
[+] Range: 400000000000000000 to 7FFFFFFFFFFFFFFFFF
[+] N = 0x3FFFFFFFFFFFFFFFFF
[+] Loaded 1 addresses
[+] Bloom filter size: 262144 bytes
[+] Speed: 85.42 Mkeys/s (Avg), Total: 856.21 Mkeys (Time: 10.02s)
```

Key metrics:
- **Speed**: Keys checked per second (Mkeys/s = million keys/second)
- **Total**: Total keys checked
- **Time**: Elapsed search time

## Performance Tips

1. **Run benchmark first** to know your system's capabilities:
   ```bash
   ./keyhunt --benchmark
   ```

2. **Use compressed keys** (`-l compress`) for 2x speed when you know the target uses compressed format (most modern wallets).

3. **Match threads to CPU cores**:
   ```bash
   ./keyhunt -m address -f target.txt -b 66 -t $(nproc)
   ```

4. **Use quiet mode** (`-q`) for long searches to reduce output overhead.

5. **Enable GPU** if available:
   ```bash
   ./keyhunt -m address -f target.txt -b 66 -G hybrid
   ```

## Progress Saving

Keyhunt automatically saves progress to `~/.keyhunt/progress/`:
- Progress saved every 60 seconds
- Resume from where you left off on restart
- JSON format for easy inspection

View saved progress:
```bash
ls -la ~/.keyhunt/progress/
```

## Common Issues

### "Not enough memory for BSGS"
Reduce N value or use auto-tuning:
```bash
./keyhunt -m bsgs -f pubkey.txt -b 125 -n 0x10000000000
```

### Low speed
- Check AVX2/AVX-512 detection at startup
- Ensure compiled with `make` not `make legacy`
- Use compressed mode if applicable

### No keys found
- Verify target file format (one address per line)
- Confirm range contains the target key
- Check key type (compressed vs uncompressed)

## Next Steps

- [Basic Usage](basic-usage.md) - Detailed command-line options
- [Address Mode](../modes/address-mode.md) - Full ADDRESS mode guide
- [BSGS Mode](../modes/bsgs-mode.md) - Full BSGS mode guide
- [Distributed Computing](../distributed/overview.md) - Multi-PC setup
