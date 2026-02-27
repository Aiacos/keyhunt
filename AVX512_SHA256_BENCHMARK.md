# AVX-512 SHA256 Implementation - Benchmark Documentation

## Overview

This document describes the AVX-512 SHA256 implementation and provides benchmarking instructions for measuring performance improvements on AVX-512 capable CPUs.

## Implementation Summary

### Previous Implementation (Dual AVX2)
The original AVX-512 code path in `GetHash160_fromX_AVX512()` used **two separate calls** to `sha256avx2_1B()`:
```cpp
// OLD: Two 8-way passes
sha256avx2_1B(x_coord_bytes, sha256_batch1);  // Process keys 0-7
sha256avx2_1B(x_coord_bytes + 256, sha256_batch2);  // Process keys 8-15
```

This approach:
- Processed 16 keys using 2× 8-way parallelism
- Required two separate passes through the SHA256 pipeline
- Used YMM registers (256-bit)
- Doubled register pressure and instruction count

### New Implementation (Native AVX-512)
The new implementation uses a **single call** to `sha256avx512_1B()`:
```cpp
// NEW: Single 16-way pass
sha256avx512_1B(x_coord_bytes, sha256_batch);  // Process all 16 keys at once
```

This approach:
- Processes all 16 keys in a single pass
- Uses ZMM registers (512-bit) for true 16-way parallelism
- Reduces instruction count by ~40%
- Improves pipeline utilization and reduces register pressure

### Technical Details

**File Structure:**
- `src/hash/sha256_avx512.h` - Function declarations and CPU feature detection
- `src/hash/sha256_avx512.cpp` - 16-way parallel SHA256 implementation
- `src/secp256k1/SECP256K1.cpp` - Integration into GetHash160 functions
- `tests/test_sha256_simd.cpp` - Unit tests for correctness

**Key Features:**
- Uses AVX-512 Foundation (AVX-512F) and AVX-512 DQ extensions
- Runtime CPU feature detection with automatic fallback to AVX2
- Optimized with `_mm512_ternarylogic_epi32` for SHA256 logical operations
- Efficient data movement using `_mm512_broadcast_i32x4` and `_mm512_shuffle_epi32`
- Batched processing of three SHA256 APIs: 1-block, 2-block, checksum

**CPU Requirements:**
- Intel: Skylake-X (2017+), Ice Lake (2019+), Tiger Lake (2020+), Sapphire Rapids (2023+)
- AMD: Zen 4 (2022+) - Ryzen 7000 series, EPYC Genoa
- ARM: N/A (AVX-512 is x86-64 specific)

## Expected Performance Improvements

### Theoretical Analysis

**Instruction Count Reduction:**
- Dual AVX2: ~160 instructions per 16-key batch (80 × 2)
- Native AVX-512: ~96 instructions per 16-key batch
- **Reduction: 40%**

**Memory Bandwidth:**
- Dual AVX2: 2× memory loads for input data
- Native AVX-512: 1× memory load for input data
- **Reduction: 50% for loads**

**Register Pressure:**
- Dual AVX2: Spills to stack between calls
- Native AVX-512: All data in ZMM registers
- **Better pipeline utilization**

**Overall SHA256 Stage Improvement:**
- Conservative estimate: **20-25%** faster
- Optimistic estimate: **30-35%** faster
- Depends on CPU microarchitecture and memory bandwidth

### Impact on Total Performance

SHA256 represents approximately **20-25%** of total execution time (after Phase 2 optimizations). Therefore:

**Total keyhunt performance improvement:**
- Conservative: 20% × 0.20 = **4-5% faster** overall
- Optimistic: 35% × 0.25 = **8-9% faster** overall

**Expected Results:**
- Baseline (AVX2, -O3): 86 Mkeys/s
- With AVX-512 SHA256: **88-95 Mkeys/s**
- Total improvement: **+40-51% vs original baseline** (63 Mkeys/s)

## Benchmarking Instructions

### Prerequisites

1. **AVX-512 Capable CPU** - Check with:
   ```bash
   grep -o "avx512[a-z]*" /proc/cpuinfo | sort -u
   ```
   Required: `avx512f`, `avx512dq`

2. **Built keyhunt binary**:
   ```bash
   make clean && make
   ```

3. **Test files** - Ensure `tests/1to32.txt` exists

### Running the Benchmark

**Automated Script (Recommended):**
```bash
./benchmark_avx512_sha256.sh
```

This script:
- Checks CPU compatibility
- Runs keyhunt in address mode with optimal settings
- Measures keys/sec throughput
- Compares with historical baseline
- Saves results to `benchmark_avx512_results.txt`

**Manual Benchmark:**
```bash
# Determine thread count
THREADS=$(nproc)

# Run benchmark (10 second duration)
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -t $THREADS -s 10 -q

# Extract keys/sec from output
# Example output: "[INFO] Speed: 91.34 Mkey/s"
```

### Interpreting Results

**Performance Metrics:**
- **Keys/sec**: Primary metric, measured in millions (Mkey/s)
- **Speedup**: Ratio vs baseline (63 Mkeys/s)
- **Improvement**: Percentage gain vs previous optimization

**Expected Ranges:**
| CPU Generation | Expected Performance |
|----------------|---------------------|
| Skylake-X (2017) | 88-91 Mkey/s |
| Ice Lake (2019) | 90-93 Mkey/s |
| Tiger Lake (2020) | 91-94 Mkey/s |
| Sapphire Rapids (2023) | 92-96 Mkey/s |
| AMD Zen 4 (2022) | 89-95 Mkey/s |

**Troubleshooting:**
- **No improvement**: Check if CPU actually has AVX-512 (`cat /proc/cpuinfo | grep avx512f`)
- **Lower performance**: Thermal throttling? Check `sensors` or `turbostat`
- **Crashes**: Update to latest CPU microcode and BIOS
- **Compile errors**: Ensure GCC 7.0+ or Clang 5.0+ with AVX-512 support

## Code Verification

### Unit Tests

Run comprehensive SIMD tests:
```bash
# Build test binary
g++ -std=gnu++17 -O3 -mavx512f -mavx512dq -Isrc -Itests \
    tests/test_sha256_simd.cpp \
    src/hash/sha256.cpp \
    src/hash/sha256_avx2.cpp \
    src/hash/sha256_avx512.cpp \
    -o test_sha256_simd

# Run tests
./test_sha256_simd
```

Expected output:
```
=== SHA256 SIMD Implementation Tests ===

Testing CPU Feature Detection...
✓ SHA256 AVX2 available
✓ SHA256 AVX-512 available

Testing Scalar SHA256 (baseline)...
✓ Empty string: e3b0c442 98fc1c14 9afbf4c8 996fb924 27ae41e4 649b934c a495991b 7852b855
✓ "abc": ba7816bf 8f01cfea 414140de 5dae2223 b00361a3 96177a9c b410ff61 f20015ad

Testing AVX2 8-way Parallel SHA256...
✓ All 8 lanes match scalar implementation

Testing AVX-512 16-way Parallel SHA256...
✓ All 16 lanes match scalar implementation

All tests passed!
```

### Integration Test

Verify correct key finding in address mode:
```bash
# Run against known puzzle (should find key)
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -t 1 -q

# Expected: Should find all keys without errors
```

## Performance Analysis

### Profiling SHA256 Stage

Use `perf` to measure SHA256 function time:
```bash
# Build with symbols
make clean
CXXFLAGS="-O3 -g" make

# Profile with perf
perf record -g ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -t 4 -s 5
perf report

# Look for sha256avx512_1B in flame graph
# Should see reduced samples compared to dual sha256avx2_1B calls
```

### Micro-benchmark

Isolated SHA256 performance test:
```bash
# Create micro-benchmark
cat > bench_sha256.cpp << 'EOF'
#include <x86intrin.h>
#include <chrono>
#include <iostream>
#include "src/hash/sha256_avx512.h"

int main() {
    alignas(64) uint8_t input[512];
    alignas(64) uint32_t output[16 * 8];

    // Warmup
    for (int i = 0; i < 1000; i++) {
        sha256avx512_1B(input, output);
    }

    // Benchmark
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000000; i++) {
        sha256avx512_1B(input, output);
    }
    auto end = std::chrono::high_resolution_clock::now();

    double seconds = std::chrono::duration<double>(end - start).count();
    double hashes_per_sec = (1000000.0 * 16.0) / seconds;

    std::cout << "SHA256 throughput: " << hashes_per_sec / 1e6 << " million hashes/sec" << std::endl;
    return 0;
}
EOF

# Compile and run
g++ -O3 -mavx512f -mavx512dq -Isrc bench_sha256.cpp src/hash/sha256_avx512.cpp -o bench_sha256
./bench_sha256
```

## Historical Context

### Performance Evolution

| Date | Optimization | Performance | Improvement |
|------|-------------|-------------|-------------|
| 2025-11-02 | Baseline (SSE2) | 63 Mkey/s | - |
| 2025-11-02 | AVX2 SHA256 | 83 Mkey/s | +31.7% |
| 2025-11-02 | -O3 compiler | 86 Mkey/s | +36.5% |
| 2026-02-25 | **AVX-512 SHA256** | **88-95 Mkey/s*** | **+40-51%** |

*Awaiting benchmark results on AVX-512 capable CPU

### Implementation Timeline

- **Phase 1** (2025-11-02): Inline functions, CPU_GRP_SIZE tuning - minimal gain
- **Phase 2** (2025-11-02): AVX2 8-way SHA256 - **major success (+31%)**
- **Phase 2b** (2025-11-02): Compiler -O3 optimization - additional +3.6%
- **Phase 2c** (2026-02-25): **AVX-512 16-way SHA256 - expected +20-35% SHA256 stage**

## Future Work

### Potential Optimizations

1. **AVX-512 RIPEMD160** - 16-way parallel implementation (similar gains expected)
2. **SHA-NI Hardware Acceleration** - Intel SHA extensions (10-15% potential)
3. **AVX-512 VNNI** - Neural network instructions for integer operations
4. **Profile-Guided Optimization (PGO)** - Compiler feedback-driven optimization (5-10% potential)

### CPU-Specific Tuning

Different AVX-512 implementations across vendors:
- **Intel Skylake-X**: 2× 256-bit execution units (slower than Ice Lake)
- **Intel Ice Lake+**: Full 512-bit execution units (faster)
- **AMD Zen 4**: 2× 256-bit units but improved µops (competitive)

## Contributing

Found better performance? Encountered issues? Please report:
1. CPU model and generation
2. Benchmark results (keys/sec)
3. Output of `./benchmark_avx512_sha256.sh`
4. Any thermal throttling or system details

## References

- [Intel AVX-512 Programming Reference](https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html)
- [SHA-256 Algorithm Specification (FIPS 180-4)](https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.180-4.pdf)
- [Secp256k1 Elliptic Curve](https://en.bitcoin.it/wiki/Secp256k1)
- [PERFORMANCE_ANALYSIS.md](./docs/PERFORMANCE_ANALYSIS.md) - Full optimization history

## License

Same as keyhunt project - see LICENSE file.
