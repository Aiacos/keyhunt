# Parameter Validation and Auto-Tuning System

## Overview

keyhunt now includes an intelligent parameter validation system that automatically:
- **Validates** user-specified parameters against available hardware
- **Auto-corrects** dangerous values that would cause crashes (OOM, excessive threads)
- **Warns** about suboptimal configurations
- **Recommends** optimal values based on CPU, RAM, and cache characteristics

This system ensures that keyhunt always runs safely and efficiently on any hardware, from low-end systems to high-end workstations.

## How It Works

### Automatic Detection (at startup)

When keyhunt starts, it automatically detects:
- **CPU**: Physical/logical cores, cache sizes (L1/L2/L3)
- **RAM**: Total and available memory
- **CPU Features**: AVX2, AVX-512, SHA-NI support

### Parameter Validation (after argument parsing)

All user-specified parameters are validated:
- **Threads (-t)**: Checked against available CPU cores
- **Batch Size (CPU_GRP_SIZE)**: Validated for cache efficiency
- **N value (-n)**: Checked against available RAM (BSGS mode)
- **K factor (-k)**: Validated with N to prevent OOM

### Validation Levels

Parameters are classified into 4 levels:

| Status | Symbol | Meaning |
|--------|--------|---------|
| **PARAM_OK** | ✓ (green) | Parameter is optimal for your hardware |
| **PARAM_SUBOPTIMAL** | i (blue) | Parameter works but isn't optimal |
| **PARAM_WARNING** | ⚠ (red) | Parameter may cause performance issues |
| **PARAM_CORRECTED** | ! (yellow) | Parameter was auto-corrected for safety |

## Examples

### Example 1: Excessive Thread Count

```bash
$ ./keyhunt -m rmd160 -f tests/66.rmd -b 66 -R -q -t 32
```

**Output:**
```
[⚠] Threads: Using 32 threads on 16 cores may cause overhead. Optimal: 16
```

**What happened**: System has 16 logical cores. Using 32 threads causes context switching overhead.

**Recommendation**: Use `-t 16` or omit `-t` for auto-tuning.

---

### Example 2: Dangerous N Value (BSGS)

```bash
$ ./keyhunt -m bsgs -f pubkeys.txt -n 0x1000000000000 -k 8192
```

**Output:**
```
[!] N Value: N=0x1000000000000 requires 539072 MB but only 18328 MB available.
    Auto-corrected to 0x10000000000 (33692 MB)
[!] K Factor: K=8192 with N=0x10000000000 requires 33692 MB but only 18328 MB available.
    Auto-corrected to K=2048
[!] Some parameters were adjusted for safety
```

**What happened**:
- Requested N would require 539 GB of RAM, but system only has 18 GB available
- keyhunt auto-corrected to N=0x10000000000 (33 GB)
- K factor also reduced to 2048 to fit in available RAM

**Result**: Program runs safely without OOM crash.

---

### Example 3: Optimal Configuration (Auto-Tuned)

```bash
$ ./keyhunt -m rmd160 -f tests/66.rmd -b 66 -R -q
```

**Output:**
```
[✓] Threads: Using auto-tuned value: 16 threads (optimal for 16 logical cores)
[✓] Batch Size: Batch size validated
[✓] All parameters validated successfully
```

**What happened**: No parameters specified by user, auto-tuning selected optimal values.

**Result**: Maximum performance with safe resource usage.

---

### Example 4: Suboptimal But Valid

```bash
$ ./keyhunt -m rmd160 -f tests/66.rmd -b 66 -R -q -t 8
```

**Output:**
```
[i] Threads: Using 8 threads. Could use up to 16 for better performance.
[✓] Batch Size: Batch size validated
```

**What happened**: User specified 8 threads on a 16-core system. Works fine but not utilizing full CPU.

**Recommendation**: Use `-t 16` or omit `-t` for auto-tuning.

---

## Parameter Reference

### Threads (-t)

**Auto-tuning**: Uses all logical cores (hyperthreading enabled)

**Validation rules**:
- If `threads > logical_cores * 2`: Auto-corrected to `logical_cores`
- If `threads > logical_cores`: Warning (context switching overhead)
- If `threads < recommended`: Info (underutilizing CPU)
- If `threads == 0` or omitted: Uses auto-tuned value

**Example**:
```bash
# Auto-tune (recommended)
./keyhunt -m rmd160 -f tests/66.rmd -b 66 -R -q

# Explicit thread count
./keyhunt -m rmd160 -f tests/66.rmd -b 66 -R -q -t 16
```

---

### N Value (-n) - BSGS Mode

**Auto-tuning**: Largest N that fits in 60% of available RAM

**Validation rules**:
- Calculates RAM requirement: `(sqrt(N) * K) * 3.5 bytes` + overhead
- If `required_RAM > available_RAM * 80%`: Auto-corrected to largest safe N
- If `required_RAM < available_RAM * 50%`: Info (could use larger N)
- If N is not a perfect square: Warning (algorithm requires perfect square)

**Example**:
```bash
# Auto-tune (recommended for first run)
./keyhunt -m bsgs -f pubkeys.txt -b 66

# Explicit N (validated against RAM)
./keyhunt -m bsgs -f pubkeys.txt -n 0x10000000000 -k 2048
```

---

### K Factor (-k) - BSGS Mode

**Auto-tuning**: Balanced K based on N and RAM

**Validation rules**:
- If `K < 128`: Auto-corrected to 128 (minimum for efficiency)
- If `K > 8192`: Warning (excessive memory usage)
- If `(N, K) > available_RAM`: Auto-corrected to largest safe K
- Recommends power-of-2 values for better performance

**Example**:
```bash
# Auto-tune K based on N and RAM
./keyhunt -m bsgs -f pubkeys.txt -n 0x10000000000

# Explicit K (validated with N)
./keyhunt -m bsgs -f pubkeys.txt -n 0x10000000000 -k 2048
```

---

### Batch Size (CPU_GRP_SIZE)

**Auto-tuning**: 1024 (proven optimal for most CPUs)

**Validation rules**:
- Must be multiple of 8 (for AVX2 alignment)
- If `batch_size > 4096`: Warning (cache thrashing)
- If `batch_size < 256`: Warning (poor batching efficiency)

**Example**:
```bash
# Uses proven optimal value of 1024
./keyhunt -m rmd160 -f tests/66.rmd -b 66 -R -q
```

Note: Batch size is currently not exposed as command-line parameter (always uses optimal value).

---

## Hardware Profiles

The validator automatically adapts to different hardware profiles:

### Low-End System (4 GB RAM, 4 cores)
```
Auto-tuned: threads=4, N=0x1000000000, K=512
Memory usage: ~2 GB (safe)
```

### Mid-Range System (16 GB RAM, 8 cores)
```
Auto-tuned: threads=8, N=0x10000000000, K=2048
Memory usage: ~7-8 GB (optimal)
```

### High-End System (64 GB RAM, 16 cores)
```
Auto-tuned: threads=16, N=0x100000000000, K=4096
Memory usage: ~40 GB (maximum performance)
```

### Server (256 GB RAM, 64 cores)
```
Auto-tuned: threads=64, N=0x400000000000, K=4096
Memory usage: ~120 GB (enterprise-grade)
```

---

## Best Practices

### 1. Trust Auto-Tuning (First Run)

On first run, omit all tuning parameters:
```bash
./keyhunt -m rmd160 -f tests/66.rmd -b 66 -R -q
```

Observe the auto-tuned values and performance. Only override if you have specific requirements.

---

### 2. Check Validation Messages

Always read the validation output (green ✓, blue i, yellow !, red ⚠).

If you see corrections or warnings, understand why before overriding.

---

### 3. BSGS Mode: Start Small

For BSGS mode, start with auto-tuned N and K:
```bash
./keyhunt -m bsgs -f pubkeys.txt -b 66
```

Observe memory usage with `htop` or `free -h`. If you have spare RAM, increase N gradually.

---

### 4. Performance Tuning: Benchmark

Test different configurations and compare performance:
```bash
# Baseline (auto-tuned)
./keyhunt -m rmd160 -f tests/66.rmd -b 66 -R -q -s 10

# Custom threads
./keyhunt -m rmd160 -f tests/66.rmd -b 66 -R -q -s 10 -t 12
```

Compare `keys/s` output to find optimal configuration for your workload.

---

### 5. Avoid These Common Mistakes

❌ **Don't**: Set threads far above core count
```bash
# BAD: 64 threads on 16-core system
./keyhunt -m rmd160 -f tests/66.rmd -b 66 -R -t 64
```

✓ **Do**: Use auto-tuned or reasonable value
```bash
# GOOD: Auto-tuned or explicit reasonable value
./keyhunt -m rmd160 -f tests/66.rmd -b 66 -R
```

❌ **Don't**: Set N without considering RAM
```bash
# BAD: Likely to cause OOM
./keyhunt -m bsgs -f pubkeys.txt -n 0xFFFFFFFFFFFF -k 8192
```

✓ **Do**: Use auto-tuned or validated value
```bash
# GOOD: Validator ensures it fits in RAM
./keyhunt -m bsgs -f pubkeys.txt -n 0x10000000000 -k 2048
```

---

## Technical Details

### Memory Calculation (BSGS Mode)

The validator uses this formula to calculate BSGS memory requirements:

```
M = sqrt(N)
bloom1_size = (M * K) * 3.5 bytes
bloom2_size = bloom1_size / 32
bloom3_size = bloom1_size / 1024
bP_table_size = (M / 32 * K) * 16 bytes

total_memory = bloom1_size + bloom2_size + bloom3_size + bP_table_size
```

**Safety margin**: Validator uses max 80% of available RAM to leave headroom for OS and other processes.

---

### Thread Optimization

**Hyperthreading**: Modern CPUs benefit from hyperthreading for this workload. Auto-tuning uses all logical cores.

**Core pinning**: Not yet implemented. Future enhancement will pin threads to specific cores.

**NUMA awareness**: Not yet implemented. On multi-socket systems, use `numactl` to bind to single node.

---

### Cache Optimization

**L3 cache**: Batch size (1024) is optimized to fit working set in L3 cache

**Cache line alignment**: Point arrays are 64-byte aligned for optimal cache utilization

**Prefetching**: Memory prefetch hints reduce cache miss penalties

---

## Troubleshooting

### Validation Messages Not Appearing

If you don't see validation output, check:
1. You're using the latest build (`make clean && make`)
2. You're not redirecting stdout/stderr (`>` or `2>`)
3. Your terminal supports ANSI colors (most modern terminals do)

---

### "Some parameters were auto-corrected" Warning

This message appears when dangerous values were corrected. Check the detailed output above to see what was corrected and why.

**Action**: Review your command-line arguments and adjust or trust the auto-correction.

---

### Performance Lower Than Expected

If performance is lower after parameter changes:
1. Check validation messages for warnings
2. Compare against auto-tuned baseline
3. Reduce thread count if `threads > cores`
4. For BSGS: ensure N and K fit comfortably in RAM

---

## Future Enhancements

Planned improvements to the validation system:

- [ ] CPU affinity and core pinning
- [ ] NUMA-aware thread distribution
- [ ] GPU detection and recommendations
- [ ] Workload-specific tuning profiles
- [ ] Historical performance database
- [ ] Machine learning-based parameter optimization

---

## Related Documentation

- [AUTO-TUNING.md](AUTO-TUNING.md) - Hardware detection details
- [BSGS_MEMORY_CHECK.md](BSGS_MEMORY_CHECK.md) - BSGS memory validation
- [PERFORMANCE_ANALYSIS.md](PERFORMANCE_ANALYSIS.md) - Performance optimization guide
- [README.md](README.md) - General usage and examples
