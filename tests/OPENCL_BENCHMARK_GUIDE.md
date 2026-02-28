# OpenCL vs CUDA Performance Benchmark Guide

This guide explains how to run performance benchmarks comparing OpenCL (AMD GPUs) vs CUDA (NVIDIA GPUs) backends in keyhunt.

## Acceptance Criteria

**Target Performance**: OpenCL backend should achieve **at least 50% of equivalent NVIDIA CUDA performance** on comparable hardware.

## Prerequisites

### For AMD GPU Testing
- AMD GPU: RX 6000 series (RDNA 2, gfx1030) or RX 7000 series (RDNA 3, gfx1100)
- ROCm drivers installed (5.4+)
- OpenCL SDK installed
- Build with OpenCL support: `./build_opencl.sh`

### For NVIDIA GPU Testing
- NVIDIA GPU (for comparison baseline)
- CUDA Toolkit 11.0+
- Build with CUDA support: `./build_cuda.sh`

### For Mixed-Vendor Testing
- Both AMD and NVIDIA GPUs in same system
- Both ROCm and CUDA installed
- Build with both backends: `make clean && make`

## Build Verification

Check which GPU backends are compiled in:

```bash
# Check build configuration
./keyhunt --version

# List detected GPUs
./keyhunt -L
```

Expected output for OpenCL-enabled build:
```
GPU Backend: OpenCL (AMD/Intel support)
Detected GPUs:
  [0] AMD Radeon RX 7900 XTX (96 CUs, 24576 MB VRAM) [OpenCL]
```

Expected output for unified build (CUDA + OpenCL):
```
GPU Backend: Unified (CUDA + OpenCL)
Detected GPUs:
  [0] NVIDIA GeForce RTX 4090 (128 SMs, 24564 MB VRAM) [CUDA]
  [1] AMD Radeon RX 7900 XTX (96 CUs, 24576 MB VRAM) [OpenCL]
```

## Running Benchmarks

### Method 1: Built-in Benchmark (Recommended)

The `--benchmark` flag runs a comprehensive performance test across CPU, GPU, and hybrid modes.

```bash
# Run full benchmark (~30 seconds)
./keyhunt --benchmark

# Quick benchmark (~10 seconds)
./keyhunt --benchmark --quick
```

**Note**: The built-in benchmark currently focuses on CUDA. For OpenCL-specific benchmarking, use Method 2 below.

### Method 2: Manual Performance Testing

#### Test 1: Hash-Only Mode Benchmark

Hash-only mode tests pure hashing performance (SHA256 + RIPEMD160) without search overhead.

```bash
# OpenCL (AMD)
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G hash -s 30 -q > /tmp/opencl_hash.log

# CUDA (NVIDIA) - for comparison
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G hash -s 30 -q > /tmp/cuda_hash.log

# Extract performance metrics
grep "Speed:" /tmp/opencl_hash.log
grep "Speed:" /tmp/cuda_hash.log
```

**Expected metrics**:
- Keys/second throughput
- GPU utilization %
- Memory bandwidth utilization

#### Test 2: Full GPU Search Mode Benchmark

Full GPU mode tests complete search pipeline including target comparison.

```bash
# OpenCL (AMD)
./keyhunt -m address -f tests/66.txt -b 66 -G full -s 30 -R -q > /tmp/opencl_full.log

# CUDA (NVIDIA) - for comparison
./keyhunt -m address -f tests/66.txt -b 66 -G full -s 30 -R -q > /tmp/cuda_full.log
```

#### Test 3: Hybrid CPU+GPU Mode Benchmark

Tests coordinated CPU+GPU execution with work distribution.

```bash
# OpenCL (AMD)
./keyhunt -m address -f tests/66.txt -b 66 -G hybrid -t 16 -s 30 -R -q > /tmp/opencl_hybrid.log

# CUDA (NVIDIA) - for comparison
./keyhunt -m address -f tests/66.txt -b 66 -G hybrid -t 16 -s 30 -R -q > /tmp/cuda_hybrid.log
```

#### Test 4: Multi-GPU Mode Benchmark (Mixed-Vendor)

On systems with both AMD and NVIDIA GPUs, test unified backend:

```bash
# All GPUs (CUDA + OpenCL)
./keyhunt -m address -f tests/66.txt -b 66 -G full -s 30 -R -q --all-gpus > /tmp/multi_vendor.log

# Check work distribution across vendors
grep -E "GPU\[.*\]" /tmp/multi_vendor.log
```

#### Test 5: BSGS Mode Benchmark (If Applicable)

BSGS mode is memory-intensive and tests different GPU capabilities:

```bash
# OpenCL (AMD)
./keyhunt -m bsgs -f tests/125.txt -b 125 -G full -s 30 -q -R > /tmp/opencl_bsgs.log

# CUDA (NVIDIA) - for comparison
./keyhunt -m bsgs -f tests/125.txt -b 125 -G full -s 30 -q -R > /tmp/cuda_bsgs.log
```

## Performance Metrics to Collect

### Primary Metrics
1. **Throughput** (Mkeys/s or keys/s)
   - GPU-only mode
   - Hybrid mode (CPU+GPU)
   - Per-device breakdown in multi-GPU systems

2. **GPU Utilization** (%)
   - Monitor with `rocm-smi` (AMD) or `nvidia-smi` (NVIDIA)
   - Target: >90% utilization

3. **Memory Bandwidth** (GB/s)
   - Check against theoretical max for GPU model
   - AMD RX 7900 XTX: ~960 GB/s
   - NVIDIA RTX 4090: ~1008 GB/s

4. **Power Consumption** (Watts)
   - AMD: `rocm-smi --showpower`
   - NVIDIA: `nvidia-smi --query-gpu=power.draw --format=csv`

### Secondary Metrics
5. **Kernel Launch Overhead** (microseconds)
6. **Memory Transfer Time** (for GTable upload)
7. **Auto-tuning Results** (blocks/CU, keys/work-item)
8. **Thermal Performance** (temperature under sustained load)

## Monitoring GPU Performance

### AMD GPU Monitoring (ROCm)

```bash
# Real-time monitoring
watch -n 1 rocm-smi

# Detailed info
rocm-smi --showuse --showmeminfo --showtemp --showpower

# Log performance during benchmark
rocm-smi --showuse --showpower --showtemp --json > /tmp/amd_gpu_log.json &
ROCM_PID=$!
./keyhunt -m address -f tests/66.txt -b 66 -G full -s 60 -R -q
kill $ROCM_PID
```

### NVIDIA GPU Monitoring (CUDA)

```bash
# Real-time monitoring
watch -n 1 nvidia-smi

# Log performance during benchmark
nvidia-smi --query-gpu=timestamp,utilization.gpu,utilization.memory,memory.used,power.draw,temperature.gpu --format=csv -l 1 > /tmp/nvidia_gpu_log.csv &
NVIDIA_PID=$!
./keyhunt -m address -f tests/66.txt -b 66 -G full -s 60 -R -q
kill $NVIDIA_PID
```

## Benchmark Result Template

Document your results using the template file:

```bash
cp tests/opencl_cuda_benchmark_results.template.txt tests/benchmark_results_$(date +%Y%m%d).txt
# Edit with your actual results
```

See `opencl_cuda_benchmark_results.template.txt` in this directory for the format.

## Performance Comparison Formula

```
OpenCL Efficiency = (OpenCL Throughput / CUDA Throughput) × 100%
```

**Acceptance threshold**: OpenCL Efficiency ≥ 50%

### Example Calculation

If NVIDIA RTX 4090 achieves 400 Mkeys/s and AMD RX 7900 XTX achieves 250 Mkeys/s:

```
Efficiency = (250 / 400) × 100% = 62.5% ✓ (PASS)
```

## Expected Performance Baselines

### AMD GPUs (OpenCL)
| GPU Model | Architecture | CUs | Expected Throughput | Notes |
|-----------|--------------|-----|---------------------|-------|
| RX 7900 XTX | RDNA 3 (gfx1100) | 96 | 200-300 Mkeys/s | Flagship RDNA 3 |
| RX 7900 XT | RDNA 3 (gfx1100) | 84 | 175-250 Mkeys/s | High-end RDNA 3 |
| RX 6900 XT | RDNA 2 (gfx1030) | 80 | 150-200 Mkeys/s | Flagship RDNA 2 |
| RX 6800 XT | RDNA 2 (gfx1030) | 72 | 125-175 Mkeys/s | High-end RDNA 2 |
| RX 6700 XT | RDNA 2 (gfx1030) | 40 | 75-125 Mkeys/s | Mid-range RDNA 2 |

### NVIDIA GPUs (CUDA - for comparison)
| GPU Model | Architecture | SMs | Expected Throughput | Notes |
|-----------|--------------|-----|---------------------|-------|
| RTX 4090 | Ada (sm_89) | 128 | 400-600 Mkeys/s | Flagship Ada |
| RTX 4080 | Ada (sm_89) | 76 | 250-400 Mkeys/s | High-end Ada |
| RTX 3090 | Ampere (sm_86) | 82 | 200-350 Mkeys/s | Flagship Ampere |
| RTX 3080 | Ampere (sm_86) | 68 | 150-250 Mkeys/s | High-end Ampere |
| RTX 3070 | Ampere (sm_86) | 46 | 100-175 Mkeys/s | Mid-range Ampere |

**Note**: Actual performance depends on:
- Kernel optimization level
- Memory bandwidth utilization
- Power limits and thermal throttling
- Driver version
- System configuration

## Auto-Tuning Verification

Verify that OpenCL auto-tuning selects appropriate parameters for AMD GPUs:

```bash
# Run with verbose output to see auto-tuning results
./keyhunt -m address -f tests/1to32.txt -G full -v 2 2>&1 | grep -A 10 "Auto-tuning"
```

Expected AMD-optimized parameters:
- **RX 6000/7000 series**: 24-32 blocks/CU, 1024-2048 keys/work-item
- **Older AMD GPUs**: 16-24 blocks/CU, 512-1024 keys/work-item

## Troubleshooting Performance Issues

### Low GPU Utilization (<70%)

**Possible causes**:
1. **Work size too small**: Increase batch size with `-B` flag
2. **CPU bottleneck**: Use `-G full` instead of `-G hybrid`
3. **Memory transfer overhead**: Check GTable upload time
4. **Driver issues**: Update ROCm to latest version

**Solutions**:
```bash
# Increase batch size
./keyhunt -m address -f tests/66.txt -B 1048576 -G full

# Force full GPU mode
./keyhunt -m address -f tests/66.txt -G full -t 1
```

### OpenCL Performance Below 50% of CUDA

**Diagnostic steps**:
1. Check kernel compilation: Look for optimization flags
2. Verify auto-tuning: Should use vendor-specific parameters
3. Compare memory bandwidth: Use `rocm-smi` to check actual vs theoretical
4. Profile kernel execution: Use ROCm profiler tools

```bash
# Check OpenCL kernel compilation flags
./keyhunt -m address -f tests/1to32.txt -G full -v 3 2>&1 | grep -i "opencl build"

# Should include: -cl-fast-relaxed-math -cl-mad-enable
```

### System-Specific Issues

**AMD-specific**:
- Ensure ROCm is properly installed: `rocminfo | grep "Name:"`
- Check OpenCL runtime: `clinfo` should list AMD GPUs
- Verify GPU accessibility: `ls -la /dev/kfd /dev/dri/render*`

**Mixed-vendor systems**:
- Verify both backends are compiled: `./keyhunt -L`
- Check device enumeration: Should list all GPUs
- Test individual backends: Use device selection flags

## Reporting Results

When documenting benchmark results, include:

1. **System Configuration**
   - CPU model and core count
   - RAM amount
   - GPU model(s) and VRAM
   - Driver versions (ROCm, CUDA)
   - OS and kernel version

2. **Build Configuration**
   - keyhunt version/commit
   - Compiler version
   - Build flags used
   - Detected backends

3. **Test Results**
   - All benchmark runs (hash, full, hybrid, multi-GPU)
   - GPU utilization and power consumption
   - Comparison vs CUDA baseline
   - Calculated efficiency percentage

4. **Analysis**
   - Performance bottlenecks identified
   - Auto-tuning parameter effectiveness
   - Recommendations for optimization

## Continuous Integration Testing

For automated testing without real AMD hardware, use mock tests:

```bash
# Build with OpenCL support (may use CPU OpenCL runtime)
make clean && make

# Run OpenCL backend unit tests (includes mock tests)
./tests/test_opencl_backend

# Run integration tests that don't require GPU
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -s 1 -q
```

## Next Steps After Benchmarking

After completing benchmarks:

1. **Document results** in `tests/benchmark_results_YYYYMMDD.txt`
2. **Update implementation plan** with actual performance metrics
3. **File issues** for any performance below 50% threshold
4. **Optimize kernels** if needed based on profiling data
5. **Update documentation** with confirmed performance numbers

## References

- [ROCm Documentation](https://rocmdocs.amd.com/)
- [OpenCL Programming Guide](https://www.khronos.org/opencl/)
- [AMD GPU Architecture Guides](https://www.amd.com/en/technologies/rdna-2)
- [keyhunt GPU Backend Documentation](../docs/GPU_BACKEND.md)
