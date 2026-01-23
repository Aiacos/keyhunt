# Testing Guide

This document covers testing keyhunt for correctness and performance.

## Quick Validation

### Basic Functionality Test

```bash
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -t 4
```

Expected: Finds 32 private keys quickly. All keys from 0x1 to 0x20 should be found.

### Mode-Specific Tests

```bash
# ADDRESS mode
./keyhunt -m address -f tests/66.txt -b 66 -R -q -s 10

# BSGS mode
./keyhunt -m bsgs -f tests/125.txt -b 125 -q -s 10 -R

# RMD160 mode
./keyhunt -m rmd160 -f tests/66.rmd -b 66 -l compress -R -q

# XPOINT mode
./keyhunt -m xpoint -f tests/120.txt -t 4 -b 125 -R -q
```

## Test Data Files

Located in `tests/` directory:

| File | Mode | Description |
|------|------|-------------|
| `1to32.txt` | ADDRESS | 32 known addresses (keys 1-32) |
| `66.txt` | ADDRESS | Puzzle 66 address |
| `66.rmd` | RMD160 | Puzzle 66 as RIPEMD160 |
| `125.txt` | BSGS | Puzzle 125 public key |
| `120.txt` | XPOINT | X-coordinate test |

### Creating Test Data

```bash
# Generate address from known private key
echo "1" | ./keyhunt -m vanity -v 1  # Shows address for key 0x1

# Convert address to RMD160
# Use Python or online tool
```

## Performance Benchmark

### Built-in Benchmark

```bash
./keyhunt --benchmark
```

Output:
```
Keyhunt Performance Benchmark
=============================

CPU Information:
  Model: AMD Ryzen 9 5900X
  Cores: 12 physical, 24 logical
  Features: SSE2, AVX2, SHA-NI

Testing ADDRESS mode...
  Threads: 1   Speed: 5.2 Mkeys/s
  Threads: 4   Speed: 20.1 Mkeys/s
  Threads: 8   Speed: 39.8 Mkeys/s
  Threads: 16  Speed: 78.2 Mkeys/s
  Threads: 24  Speed: 92.4 Mkeys/s

Testing GPU modes...
  GPU: NVIDIA RTX 3080
  Mode: hash    Speed: 180 Mkeys/s
  Mode: full    Speed: 320 Mkeys/s
  Mode: hybrid  Speed: 412 Mkeys/s

Recommendations:
  CPU optimal threads: 24
  GPU optimal mode: hybrid
  Combined speed: 412 Mkeys/s
```

### Manual Performance Test

```bash
# 10-second ADDRESS mode test
timeout 10 ./keyhunt -m address -f tests/66.txt -b 66 -R -t 16 -q

# 60-second BSGS test
timeout 60 ./keyhunt -m bsgs -f tests/125.txt -b 125 -q -s 10
```

### Profiling

```bash
# Build with profiling
make CXXFLAGS="-O2 -pg"

# Run
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF

# Analyze
gprof keyhunt gmon.out > profile.txt
```

Key metrics to check:
- `ripemd160_avx2` should dominate (hashing is the bottleneck)
- Bloom filter checks should be fast
- Minimal time in memory allocation

## Memory Testing

### Valgrind Memory Check

```bash
# Build for Valgrind
make CXXFLAGS="-O1 -g"

# Run with Valgrind
valgrind --leak-check=full --show-leak-kinds=all \
    ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF
```

Expected: No memory leaks, no invalid reads/writes.

### AddressSanitizer

```bash
# Build with ASan
make CXXFLAGS="-O1 -g -fsanitize=address -fno-omit-frame-pointer"

# Run (automatically checks for errors)
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF
```

### BSGS Memory Validation

```bash
# Should show memory check
./keyhunt -m bsgs -f tests/125.txt -b 125

# Should warn about insufficient memory
./keyhunt -m bsgs -f tests/125.txt -b 125 -n 0x10000000000000000
```

## Correctness Tests

### Known Answer Test

Verify keys produce correct addresses:

```python
#!/usr/bin/env python3
# test_correctness.py
import subprocess
import re

known_keys = {
    "1": "1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH",
    "2": "1CUNEBjYrCn2y1SdiUMohaKUi4wpP326Lb",
    "3": "19ZewH8Kk1PDbSNdJ97FP4EiCjTRaZMZQA",
}

# Run keyhunt and verify
for key, expected_addr in known_keys.items():
    result = subprocess.run(
        ["./keyhunt", "-m", "address", "-f", "-", "-r", f"{key}:{key}"],
        input=expected_addr.encode(),
        capture_output=True
    )
    assert expected_addr in result.stdout.decode(), f"Failed for key {key}"
    print(f"OK: Key {key} -> {expected_addr}")

print("All correctness tests passed!")
```

### BSGS Correctness

```bash
# Known public key with known private key
# Should find the key
./keyhunt -m bsgs -f tests/known_pubkey.txt -r 12345:12345
```

### Endomorphism Test

```bash
# With endomorphism (-e flag)
./keyhunt -m address -f tests/66.txt -b 66 -e -R -q -s 10
```

## Stress Testing

### Long-Running Test

```bash
# Run for 1 hour
timeout 3600 ./keyhunt -m address -f tests/66.txt -b 66 -R -t 16 -q -s 60
```

Monitor:
- Memory usage stable
- Speed consistent
- No crashes

### Multi-Instance Test

```bash
# Run multiple instances
for i in {1..4}; do
    ./keyhunt -m address -f tests/66.txt -b 66 -R -t 4 -q &
done
wait
```

### Resource Exhaustion Test

```bash
# Maximum threads
./keyhunt -m address -f tests/66.txt -b 66 -t 1000

# Should auto-correct to available cores
```

## Distributed Mode Testing

### Server-Client Test

Terminal 1 (Server):
```bash
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF \
    --server --port 2222 --work-unit-size 0x10000
```

Terminal 2 (Client):
```bash
./keyhunt --client --server-ip 127.0.0.1 --port 2222
```

Verify:
- Client connects successfully
- Work units distributed
- Keys found reported to server

### Wizard Test

```bash
# Interactive test
./keyhunt --wizard
# Follow prompts, verify each step works
```

## Regression Testing

### Before Commits

```bash
#!/bin/bash
# pre-commit-test.sh

echo "Running regression tests..."

# Quick functionality
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -t 4 || exit 1

# Mode tests
./keyhunt -m bsgs -f tests/125.txt -b 125 -n 0x1000000 -q -s 1 || exit 1
./keyhunt -m rmd160 -f tests/66.rmd -b 66 -R -q -s 1 -t 4 || exit 1

# Benchmark (should complete without error)
timeout 30 ./keyhunt --benchmark || exit 1

echo "All tests passed!"
```

### CI Integration

Example GitHub Actions workflow:

```yaml
# .github/workflows/test.yml
name: Tests
on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3

      - name: Build
        run: make

      - name: Quick Test
        run: ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF

      - name: Benchmark
        run: timeout 30 ./keyhunt --benchmark
```

## Test Coverage

### Code Coverage

```bash
# Build with coverage
make CXXFLAGS="-O0 -g --coverage"

# Run tests
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF
./keyhunt -m bsgs -f tests/125.txt -b 125 -n 0x1000000

# Generate report
gcov keyhunt.cpp
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_report
```

## Troubleshooting Tests

### Test Fails to Find Keys

1. Check target file format
2. Verify range contains target
3. Check key type (compressed/uncompressed)

### Performance Below Expected

1. Check SIMD detection at startup
2. Verify CPU frequency scaling
3. Check for thermal throttling

### Memory Errors

1. Check available RAM
2. Reduce N value for BSGS
3. Check for other memory-hungry processes

## See Also

- [Building](building.md) - Build options
- [Architecture](architecture.md) - Code structure
- [Quick Start](../getting-started/quick-start.md) - Basic usage
