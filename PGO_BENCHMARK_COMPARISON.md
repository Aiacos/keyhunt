# PGO Performance Comparison Documentation

## Overview

This document provides methodology and results for comparing regular build vs PGO (Profile-Guided Optimization) build performance.

## Build Information

### Regular Build
- **Binary:** `keyhunt`
- **Size:** 691K (707,256 bytes)
- **Flags:** `-O3 -ftree-vectorize -funroll-loops -flto=auto -march=native`
- **Build command:** `make clean && make`

### PGO Build
- **Binary:** `keyhunt_pgo`
- **Size:** 723K (740,352 bytes, +4.7% larger)
- **Flags:** `-O3 -ftree-vectorize -funroll-loops -flto=auto -march=native -fprofile-use=pgo_data`
- **Build process:**
  1. `make pgo-generate` - Build instrumented binary
  2. `make pgo-train` - Run training workloads (generates 49 .gcda profile files)
  3. `make pgo-use` - Build optimized binary using profile data

### Training Workload Coverage

The PGO training covers all critical code paths:

1. **Address mode (compressed)** - 15s
   - Elliptic curve operations
   - SHA256 hashing
   - RIPEMD160 hashing
   - Bloom filter lookups
   - Binary search

2. **Address mode (uncompressed)** - 10s
   - Different point serialization path
   - Tests both compressed/uncompressed key handling

3. **RMD160 mode** - 10s
   - Direct hash search
   - ~5 Mkeys/s throughput observed during training

4. **BSGS mode** - 10s
   - Baby-step giant-step algorithm
   - Small table (n=67,108,864)

5. **XPoint mode** - 10s
   - Public key X-coordinate extraction
   - ~5 Mkeys/s throughput observed during training

**Total training time:** ~60 seconds
**Profile files generated:** 49 .gcda files in `pgo_data/`

## Manual Benchmark Procedure

### Step 1: Benchmark Regular Build

```bash
# Build regular version
make clean
make -j$(nproc)

# Run benchmark
./keyhunt --benchmark

# Expected output format:
# - System information (CPU, cores, RAM)
# - Benchmark duration
# - Keys/second throughput
# - Hash operations per second
```

### Step 2: Benchmark PGO Build

```bash
# PGO build is already complete, just run benchmark
./keyhunt_pgo --benchmark

# Compare output with regular build
```

### Step 3: Compare Results

Key metrics to compare:

1. **Keys/second throughput** - Primary performance metric
2. **Hash operations/second** - RIPEMD160/SHA256 performance
3. **Memory usage** - Should be identical
4. **CPU utilization** - Should be identical

## Expected Results

### Performance Improvement

**Expected:** 5-20% improvement in keys/sec throughput

PGO optimizations provide benefits through:

1. **Hot path optimization**
   - Compiler prioritizes frequently executed code
   - Better instruction scheduling for hot loops
   - Optimized register allocation

2. **Branch prediction**
   - Profile data reveals actual branch patterns
   - Compiler can optimize likely/unlikely branches
   - Reduced branch misprediction penalties

3. **Code layout**
   - Hot code placed together for better i-cache locality
   - Cold code moved out of critical paths
   - Improved instruction prefetching

4. **Inlining decisions**
   - Data-driven inlining of hot functions
   - Avoids over-inlining cold paths
   - Better balance between code size and speed

### Binary Size

- PGO binary is ~4.7% larger (32KB difference)
- Size increase is due to:
  - Code duplication for hot path optimization
  - Aggressive inlining of frequently called functions
  - Loop unrolling in hot loops
- **Trade-off:** Slightly larger binary for significant performance gain

### Crypto-Heavy Workload Benefits

PGO is particularly effective for keyhunt because:

- **Tight inner loops** in RIPEMD160/SHA256 hashing
- **Predictable branch patterns** in bloom filter checks
- **Hot paths** in elliptic curve operations (point addition, scalar multiplication)
- **SIMD code** benefits from better instruction scheduling

## Benchmark Comparison Script

A comprehensive benchmark script is provided in `benchmark_comparison.sh`:

```bash
# Make executable
chmod +x benchmark_comparison.sh

# Run full comparison
./benchmark_comparison.sh

# Results saved to: pgo_performance_results.txt
```

The script:
- Builds both versions from scratch
- Runs benchmarks on both
- Extracts and compares performance metrics
- Saves detailed results to file
- Provides summary and interpretation

## Quick Comparison Commands

```bash
# Side-by-side benchmark
echo "=== Regular Build ===" && time ./keyhunt --benchmark 2>&1 | grep -E "keys/s|Mkeys/s|seconds"
echo "=== PGO Build ===" && time ./keyhunt_pgo --benchmark 2>&1 | grep -E "keys/s|Mkeys/s|seconds"
```

## Verification Checklist

- [x] Regular build completes successfully
- [x] PGO instrumented build completes (`make pgo-generate`)
- [x] Training workloads run successfully (`make pgo-train`)
- [x] 49 profile data files generated in `pgo_data/`
- [x] PGO optimized build completes (`make pgo-use`)
- [x] Both binaries exist and are executable
- [ ] Regular benchmark runs and completes (MANUAL)
- [ ] PGO benchmark runs and completes (MANUAL)
- [ ] Performance improvement measured (MANUAL)
- [ ] Results documented (MANUAL)

## Implementation Notes

### Profile Data Quality

The training workload is designed to represent realistic usage:

- Multiple search modes covered
- Both compressed and uncompressed keys
- Varied bit ranges (32-bit to 66-bit)
- All hash functions exercised
- BSGS algorithm profiled

This ensures the profile data accurately reflects production workloads.

### Build System Integration

PGO targets follow existing Makefile patterns:

```makefile
# Similar to sanitize/tsan/coverage targets
pgo-generate: OBJDIR := obj_pgo_gen
pgo-generate: CXXFLAGS += -fprofile-generate=pgo_data
pgo-generate: LDFLAGS += -fprofile-generate=pgo_data

pgo-use: OBJDIR := obj_pgo_use
pgo-use: CXXFLAGS += -fprofile-use=pgo_data -fprofile-correction
pgo-use: LDFLAGS += -fprofile-use=pgo_data
```

### Cleanup

```bash
# Remove PGO artifacts
make pgo-clean

# This removes:
# - obj_pgo_gen/ (instrumented object files)
# - obj_pgo_use/ (optimized object files)
# - pgo_data/ (profile data .gcda files)
# - keyhunt_pgo_gen (instrumented binary)
# - keyhunt_pgo (optimized binary)
```

## Technical Details

### Compiler Flags

**Profile Generation:**
```
-fprofile-generate=pgo_data  # Generate profile data in pgo_data/
```

**Profile Use:**
```
-fprofile-use=pgo_data       # Use profile data from pgo_data/
-fprofile-correction         # Handle minor profile inconsistencies
-Wno-missing-profile         # Suppress warnings for uncovered code
```

### Profile Data Format

GCC generates `.gcda` files (GCC Data Archive) containing:
- Execution counts for basic blocks
- Branch taken/not-taken statistics
- Function call frequencies
- Code coverage data

This data guides the compiler's optimization decisions in the final build.

## References

- CLAUDE.md - Full PGO build documentation
- README.md - General build instructions
- Makefile - PGO target implementations (lines 240-280)
- pgo_train.sh - Training workload script

---

**Last Updated:** 2026-02-25
**Status:** Ready for manual benchmark verification
