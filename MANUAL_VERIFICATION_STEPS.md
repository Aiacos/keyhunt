# Manual Verification Steps for PGO Performance Comparison

## Quick Start

Both binaries are built and ready for benchmarking:

```bash
# 1. Benchmark regular build
./keyhunt --benchmark | tee regular_benchmark.txt

# 2. Benchmark PGO build
./keyhunt_pgo --benchmark | tee pgo_benchmark.txt

# 3. Compare results side-by-side
diff -y regular_benchmark.txt pgo_benchmark.txt
```

## Expected Results

**Performance Improvement:** 5-20% in keys/sec throughput

**Why PGO Works for Keyhunt:**
- Optimizes hot paths in RIPEMD160/SHA256 hashing
- Improves branch prediction in bloom filter checks
- Better code layout for SIMD operations
- Optimized inlining of frequently called EC functions

## Binary Information

| Build Type | Binary | Size | Description |
|------------|--------|------|-------------|
| Regular | `keyhunt` | 691K | Standard -O3 optimization |
| PGO | `keyhunt_pgo` | 723K | +4.7% size for profile-guided optimization |

## Profile Data Quality

✓ **49 .gcda profile files** in `pgo_data/`
✓ **Training covered:**
  - Address mode (compressed/uncompressed)
  - RMD160 mode (~5 Mkeys/s)
  - BSGS mode (baby-step giant-step)
  - XPoint mode (~5 Mkeys/s)
  - All hash functions (SHA256, RIPEMD160)
  - Bloom filter operations
  - Elliptic curve operations

## Recording Results

Use the template: `PGO_PERFORMANCE_RESULTS_TEMPLATE.txt`

1. Fill in system information (CPU, RAM, cores)
2. Paste benchmark outputs
3. Extract key metrics (keys/sec, hash ops/sec)
4. Calculate improvement percentage
5. Document conclusions

## Automated Comparison

For a complete automated comparison with detailed analysis:

```bash
./benchmark_comparison.sh
```

This will:
- Build both versions from scratch
- Run benchmarks on both
- Extract and compare metrics
- Save results to `pgo_performance_results.txt`

## Success Criteria

✅ PGO binary runs without errors
✅ Performance improvement: 5-20% (or any positive improvement)
✅ Identical functional behavior to regular build
✅ Binary size increase is acceptable (only +32KB)

## Documentation

- **Full methodology:** `PGO_BENCHMARK_COMPARISON.md`
- **Results template:** `PGO_PERFORMANCE_RESULTS_TEMPLATE.txt`
- **Automated script:** `benchmark_comparison.sh`

## Notes

- Both binaries are already built and ready
- PGO training already completed (~60 seconds)
- Profile data is already generated (49 files)
- Just need to run benchmarks and compare

---

**Status:** Ready for manual benchmark verification
**Last Updated:** 2026-02-25
