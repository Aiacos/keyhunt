# AMD RX 6000/7000 Series GPU Testing

This directory contains comprehensive testing infrastructure for validating OpenCL backend support on AMD Radeon RX 6000 series (RDNA 2) and RX 7000 series (RDNA 3) GPUs.

## Quick Start

### Prerequisites
1. AMD RX 6000 or RX 7000 series GPU
2. ROCm 5.4+ drivers installed
3. OpenCL runtime and headers
4. keyhunt built with OpenCL support

### Build with OpenCL
```bash
./build_opencl.sh
```

### Run Tests
```bash
# Quick test (30 seconds per test)
./tests/test_amd_rx6000_rx7000.sh

# Extended test (60 seconds per test)
./tests/test_amd_rx6000_rx7000.sh 60

# With stress test
RUN_STRESS_TEST=1 ./tests/test_amd_rx6000_rx7000.sh 60
```

### Check Results
```bash
# Results saved to timestamped file
ls -lt tests/amd_test_results/

# View latest results
cat tests/amd_test_results/amd_rx_test_*.txt | head -100
```

## Testing Files

### Test Scripts

| File | Purpose |
|------|---------|
| `test_amd_rx6000_rx7000.sh` | Automated test suite for AMD GPUs |
| `run_opencl_cuda_benchmark.sh` | Comparative benchmark (OpenCL vs CUDA) |

### Documentation

| File | Purpose |
|------|---------|
| `AMD_RX6000_RX7000_TEST_GUIDE.md` | Comprehensive testing guide |
| `OPENCL_BENCHMARK_GUIDE.md` | General OpenCL benchmark guide |
| `README_AMD_TESTING.md` | This file - quick reference |
| `README_BENCHMARK.md` | Benchmark workflow guide |

### Templates

| File | Purpose |
|------|---------|
| `amd_gpu_test_results.template.txt` | Template for documenting AMD GPU test results |
| `opencl_cuda_benchmark_results.template.txt` | Template for comparative benchmarks |

### Result Directories

| Directory | Purpose |
|-----------|---------|
| `amd_test_results/` | AMD-specific test results |
| `benchmark_results/` | Comparative benchmark results |

## Test Suite Overview

The automated test suite (`test_amd_rx6000_rx7000.sh`) performs 9 comprehensive tests:

1. **Device Detection** - Verify OpenCL platform and device enumeration
2. **Kernel Compilation** - Verify GPU kernels compile for AMD RDNA
3. **Hash-Only Mode** - Test pure hashing performance (SHA256 + RIPEMD160)
4. **Full GPU Search** - Test complete search pipeline with target comparison
5. **Hybrid CPU+GPU** - Test coordinated CPU+GPU execution
6. **Multi-GPU** - Test multiple AMD GPUs simultaneously (if available)
7. **BSGS Mode** - Test BSGS algorithm with GPU acceleration
8. **Auto-Tuning** - Verify auto-tuning selects optimal parameters for AMD RDNA
9. **Stress Test** - Extended 5-minute stability test (optional, set `RUN_STRESS_TEST=1`)

## Expected Performance Baselines

### RX 6000 Series (RDNA 2 - gfx1030)

| GPU Model | Throughput | Compute Units | VRAM |
|-----------|------------|---------------|------|
| RX 6500 XT | 60-80 Mkey/s | 16 | 4 GB |
| RX 6600 XT | 100-120 Mkey/s | 32 | 8 GB |
| RX 6700 XT | 130-160 Mkey/s | 40 | 12 GB |
| RX 6800 | 180-220 Mkey/s | 60 | 16 GB |
| RX 6800 XT | 200-240 Mkey/s | 72 | 16 GB |
| RX 6900 XT | 220-260 Mkey/s | 80 | 16 GB |

### RX 7000 Series (RDNA 3 - gfx1100)

| GPU Model | Throughput | Compute Units | VRAM |
|-----------|------------|---------------|------|
| RX 7600 | 120-150 Mkey/s | 32 | 8 GB |
| RX 7700 XT | 180-220 Mkey/s | 54 | 12 GB |
| RX 7800 XT | 220-260 Mkey/s | 60 | 16 GB |
| RX 7900 XT | 240-280 Mkey/s | 84 | 20 GB |
| RX 7900 XTX | 260-300 Mkey/s | 96 | 24 GB |

**Note:** Performance varies based on ROCm version, driver, and system configuration.

## GPU Monitoring

Monitor your AMD GPU during tests:

```bash
# Real-time monitoring (update every 2 seconds)
watch -n 2 rocm-smi

# Detailed monitoring
watch -n 2 'rocm-smi --showtemp --showpower --showuse --showmemuse'

# Export monitoring data
rocm-smi --showuse --showmemuse --showtemp --showpower > monitoring.log
```

### Key Metrics

- **GPU Utilization:** Should be 95-100% during tests
- **Temperature:** Should stay below 85°C (junction)
- **Power:** Varies by model (150W-350W typical)
- **Memory Usage:** Should be < 90% of VRAM

## Troubleshooting

### No OpenCL Devices Detected

```bash
# Check ROCm installation
rocm-smi --showproductname

# Verify OpenCL ICD
ls /etc/OpenCL/vendors/

# Check user permissions
sudo usermod -a -G video,render $USER
# Log out and log back in
```

### Kernel Compilation Errors

```bash
# Check ROCm OpenCL compiler
ls /opt/rocm/bin/clang*

# Verify GPU architecture
rocminfo | grep "gfx"

# Update ROCm
sudo apt-get update && sudo apt-get upgrade rocm-opencl
```

### Low Performance

```bash
# Set performance mode
rocm-smi --setperflevel high

# Check for throttling
rocm-smi --showtemp
rocm-smi --showclock

# Verify no other GPU workloads
rocm-smi --showpids
```

See `AMD_RX6000_RX7000_TEST_GUIDE.md` for comprehensive troubleshooting.

## Acceptance Criteria

For subtask-5-3 to be considered complete:

- ✅ AMD RX 6000 or 7000 series GPU detected by OpenCL
- ✅ OpenCL kernels compile successfully (no errors)
- ✅ Device information correctly reported (name, compute units, VRAM)
- ✅ Hash-only mode runs without errors
- ✅ Full GPU search mode runs without errors
- ✅ Throughput within expected range for GPU model
- ✅ GPU utilization > 90% during tests
- ✅ No crashes, hangs, or driver resets
- ✅ Results documented with GPU model and driver version

## Documenting Results

After running tests:

1. **Copy the template:**
   ```bash
   cp tests/amd_gpu_test_results.template.txt tests/amd_test_results/my_results.txt
   ```

2. **Fill in all sections:**
   - System configuration
   - Test results for each test
   - Performance metrics
   - GPU monitoring data
   - Issues encountered
   - Conclusions

3. **Attach log files:**
   - Copy timestamped result file from `amd_test_results/`
   - Include GPU monitoring logs
   - Include any error logs from `dmesg`

4. **Share results:**
   - Update implementation plan with test status
   - Document GPU model, driver version, and key findings
   - Report any issues or optimization opportunities

## Next Steps

After completing AMD GPU testing (subtask-5-3):
1. Proceed to subtask-5-4: Test mixed-vendor systems (NVIDIA + AMD simultaneously)
2. Document any performance optimizations discovered
3. Update OPENCL_BACKEND.md with hardware-specific notes

## Support

For issues not covered in this README:
1. See `AMD_RX6000_RX7000_TEST_GUIDE.md` for detailed troubleshooting
2. Check `dmesg` for driver errors: `dmesg | grep -i amdgpu`
3. Review ROCm logs: `cat /var/log/syslog | grep -i rocm`
4. Run with verbose output: `./keyhunt -m address ... -v`

## Additional Resources

- [AMD RX6000/RX7000 Test Guide](AMD_RX6000_RX7000_TEST_GUIDE.md) - Detailed testing procedures
- [OpenCL Benchmark Guide](OPENCL_BENCHMARK_GUIDE.md) - General OpenCL benchmarking
- [ROCm Documentation](https://rocmdocs.amd.com/) - AMD ROCm platform documentation
- [OpenCL Backend Documentation](../docs/OPENCL_BACKEND.md) - Implementation details

---

**Last Updated:** 2026-02-28
**Test Script Version:** 1.0
**Compatible ROCm Versions:** 5.4.0+
