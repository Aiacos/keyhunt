# OpenCL Benchmark Resources

This directory contains resources for benchmarking the OpenCL backend performance against CUDA.

## Quick Start

### Automated Benchmark (Recommended)

Run the automated benchmark script:

```bash
./tests/run_opencl_cuda_benchmark.sh
```

This will:
- Detect available GPU backends (OpenCL and/or CUDA)
- Collect system information
- Run comprehensive performance tests
- Save results to `tests/benchmark_results/benchmark_TIMESTAMP.txt`

### Manual Benchmark

For step-by-step manual benchmarking, follow the guide:

```bash
# Read the comprehensive guide
cat tests/OPENCL_BENCHMARK_GUIDE.md

# Run specific tests manually
./keyhunt -m address -f tests/66.txt -b 66 -G full -s 30 -R -q
```

## Files in This Directory

### Documentation

- **`OPENCL_BENCHMARK_GUIDE.md`** - Comprehensive guide for OpenCL vs CUDA benchmarking
  - Prerequisites and system requirements
  - Build verification steps
  - Detailed test procedures (hash-only, full GPU, hybrid, multi-GPU)
  - Performance metrics to collect
  - GPU monitoring instructions (rocm-smi, nvidia-smi)
  - Troubleshooting guide
  - Performance baselines for common GPUs

### Templates

- **`opencl_cuda_benchmark_results.template.txt`** - Template for documenting benchmark results
  - Structured format for recording all test results
  - System configuration section
  - Performance metrics for each test
  - Auto-tuning results
  - GPU monitoring data
  - Analysis and conclusions

### Scripts

- **`run_opencl_cuda_benchmark.sh`** - Automated benchmark runner
  - Detects available backends automatically
  - Runs all standard benchmark tests
  - Collects system information
  - Saves results in standardized format
  - Optional GPU monitoring integration

## Benchmark Workflow

### Step 1: Prepare System

Ensure you have the required hardware and software:

```bash
# For AMD GPU testing
# - AMD RX 6000/7000 series GPU
# - ROCm 5.4+ installed
# - Build with: ./build_opencl.sh

# For NVIDIA GPU testing
# - NVIDIA GPU (any CUDA-capable)
# - CUDA 11.0+ installed
# - Build with: ./build_cuda.sh

# For mixed-vendor testing
# - Both AMD and NVIDIA GPUs
# - Both ROCm and CUDA installed
# - Build with: make clean && make
```

### Step 2: Verify Build

Check that GPU backends are properly compiled:

```bash
./keyhunt --version    # Check build info
./keyhunt -L           # List detected GPUs
```

### Step 3: Run Benchmarks

Option A - Automated (easiest):
```bash
./tests/run_opencl_cuda_benchmark.sh -d 60    # 60-second tests
```

Option B - Manual (for fine-grained control):
```bash
# Follow OPENCL_BENCHMARK_GUIDE.md step-by-step
```

### Step 4: Document Results

Fill in the template with your actual results:

```bash
cp tests/opencl_cuda_benchmark_results.template.txt \
   tests/benchmark_results/benchmark_$(date +%Y%m%d).txt

# Edit with your results
nano tests/benchmark_results/benchmark_$(date +%Y%m%d).txt
```

### Step 5: Calculate Efficiency

OpenCL Efficiency = (OpenCL Throughput / CUDA Throughput) × 100%

**Acceptance threshold: ≥50%**

Example:
- AMD RX 7900 XTX: 250 Mkeys/s (OpenCL)
- NVIDIA RTX 4090: 425 Mkeys/s (CUDA)
- Efficiency: (250 / 425) × 100% = **58.8%** ✓ PASS

## Expected Performance

### Target Performance

The OpenCL backend should achieve **at least 50% of equivalent NVIDIA CUDA performance** on comparable hardware.

### Performance Baselines

#### AMD GPUs (OpenCL)
- **RX 7900 XTX** (96 CUs): 200-300 Mkeys/s
- **RX 7900 XT** (84 CUs): 175-250 Mkeys/s
- **RX 6900 XT** (80 CUs): 150-200 Mkeys/s
- **RX 6800 XT** (72 CUs): 125-175 Mkeys/s

#### NVIDIA GPUs (CUDA - for comparison)
- **RTX 4090** (128 SMs): 400-600 Mkeys/s
- **RTX 4080** (76 SMs): 250-400 Mkeys/s
- **RTX 3090** (82 SMs): 200-350 Mkeys/s
- **RTX 3080** (68 SMs): 150-250 Mkeys/s

*Note: Actual performance depends on kernel optimization, power limits, and thermal conditions.*

## Troubleshooting

### No GPUs Detected

```bash
# Check OpenCL runtime
clinfo

# Check ROCm installation
rocminfo

# Check CUDA installation
nvidia-smi

# Verify keyhunt build
ldd ./keyhunt | grep -E 'OpenCL|cuda'
```

### Low Performance

**AMD GPU < 50% of NVIDIA:**

1. Check GPU utilization with `rocm-smi`
2. Verify auto-tuning selected optimal parameters
3. Check for thermal throttling
4. Update ROCm drivers

**Both GPUs underperforming:**

1. Increase batch size: `-B 1048576`
2. Use full GPU mode: `-G full`
3. Check for CPU bottleneck
4. Verify test file is valid

### Build Issues

```bash
# Rebuild with verbose output
make clean
make V=1

# Check for OpenCL headers
ls -la /usr/include/CL/cl.h
ls -la /opt/rocm/opencl/include/CL/cl.h

# Check for CUDA installation
which nvcc
```

## CI/CD Testing

For automated testing without real GPU hardware:

```bash
# Run unit tests (includes mock tests)
./tests/test_opencl_backend

# Run basic integration test
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -s 1 -q
```

Mock tests verify the OpenCL backend compiles and initializes correctly even without real AMD GPUs available.

## Benchmark Results Directory

Results are saved to `tests/benchmark_results/` with timestamp-based filenames:

```
tests/benchmark_results/
├── benchmark_20260228_143022.txt      # Full benchmark results
├── amd_gpu_monitor_20260228_143022.csv    # AMD GPU monitoring data
└── nvidia_gpu_monitor_20260228_143022.csv # NVIDIA GPU monitoring data
```

## Contributing Benchmark Results

If you have access to AMD RX 6000/7000 series GPUs and can run benchmarks:

1. Run the automated benchmark script
2. Fill in the results template
3. Verify OpenCL efficiency ≥ 50%
4. Document any issues or optimizations
5. Submit results via GitHub issue or pull request

Your benchmark data helps validate the OpenCL backend on real hardware!

## References

- [ROCm Documentation](https://rocmdocs.amd.com/)
- [OpenCL Programming Guide](https://www.khronos.org/opencl/)
- [keyhunt OpenCL Backend Architecture](../docs/OPENCL_BACKEND.md) *(to be created)*
- [keyhunt GPU Backend Documentation](../docs/GPU_BACKEND.md) *(if exists)*

## Support

For issues or questions about benchmarking:

1. Check `OPENCL_BENCHMARK_GUIDE.md` for detailed troubleshooting
2. Review existing benchmark results in `benchmark_results/`
3. File an issue with your system configuration and test results

---

**Status**: Benchmark infrastructure complete. Awaiting real AMD hardware testing.

**Last Updated**: 2026-02-28
