# Mixed-Vendor GPU Testing - Quick Reference

Quick start guide for testing keyhunt's unified backend with simultaneous NVIDIA (CUDA) and AMD (OpenCL) GPU operation.

## Quick Start

### Prerequisites Check

```bash
# Verify both backends are built
ldd ./keyhunt | grep -E "cuda|OpenCL"

# Should show:
# libcudart.so.12 => ...
# libOpenCL.so.1 => ...
```

### Run Automated Tests

```bash
# Default test (30s per test)
./tests/test_mixed_vendor.sh

# Extended test (60s per test)
./tests/test_mixed_vendor.sh 60

# Quick test (15s per test)
./tests/test_mixed_vendor.sh 15
```

### Verify GPU Detection

```bash
./keyhunt -L
```

**Expected output:**
```
Available GPU devices:
Device 0: NVIDIA GeForce RTX 4090 (CUDA)
Device 1: AMD Radeon RX 7900 XTX (OpenCL)
Backend: Unified
```

## Test Files Overview

| File | Purpose | Size |
|------|---------|------|
| `test_mixed_vendor.sh` | Automated test suite (10 tests) | 678 lines |
| `MIXED_VENDOR_TEST_GUIDE.md` | Comprehensive testing guide | ~1000 lines |
| `mixed_vendor_test_results.template.txt` | Structured results template | ~550 lines |
| `README_MIXED_VENDOR.md` | This quick reference | ~250 lines |

## Test Suite Summary

### Automated Tests (test_mixed_vendor.sh)

1. **Mixed-Vendor Detection** - Verify both NVIDIA and AMD detected
2. **Backend Initialization** - Verify unified backend initializes
3. **Hash-Only Mode** - Test SHA256+RIPEMD160 on all GPUs
4. **Full GPU Search** - Test complete search on all devices
5. **Hybrid Mode** - Test CPU + all GPUs working together
6. **Work Distribution** - Verify balanced distribution
7. **Result Correctness** - Verify correct results from mixed backend
8. **GPU Monitoring** - Collect utilization/temp/power data
9. **Performance Balance** - Verify both vendors utilized effectively
10. **Stress Test** - 5-minute stability test (optional)

### Test Results Location

```
./tests/mixed_vendor_results/
├── mixed_vendor_test_YYYYMMDD_HHMMSS.txt    # Main results
├── nvidia_monitor_YYYYMMDD_HHMMSS.txt       # NVIDIA monitoring
└── amd_monitor_YYYYMMDD_HHMMSS.txt          # AMD monitoring
```

## Manual Testing Commands

### Verify Detection

```bash
# List all GPUs
./keyhunt -L

# NVIDIA GPUs
nvidia-smi --list-gpus

# AMD GPUs
rocm-smi --showproductname

# OpenCL devices
clinfo -l
```

### Test Hash-Only Mode

```bash
# 60-second hash test on all GPUs
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G hash -s 60
```

### Test Full GPU Search

```bash
# 60-second full search on all GPUs
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G full -s 60
```

### Test Hybrid Mode

```bash
# 60-second hybrid (CPU + all GPUs)
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G hybrid -s 60
```

### Monitor GPUs During Test

**Terminal 1: NVIDIA**
```bash
watch -n 1 nvidia-smi
```

**Terminal 2: AMD**
```bash
watch -n 1 'rocm-smi --showuse --showtemp --showpower'
```

**Terminal 3: Run Test**
```bash
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G full -s 120
```

## Expected Performance

### Example Systems

#### RTX 4090 + RX 7900 XTX
```
NVIDIA RTX 4090:  ~1000-1200 Mkey/s (CUDA)
AMD RX 7900 XTX:   ~300-360 Mkey/s (OpenCL)
Combined:        ~1300-1560 Mkey/s
Efficiency:            95-98%
```

#### RTX 3080 + RX 6800 XT
```
NVIDIA RTX 3080:   ~380-450 Mkey/s (CUDA)
AMD RX 6800 XT:    ~220-270 Mkey/s (OpenCL)
Combined:          ~600-720 Mkey/s
Efficiency:             95-98%
```

#### RTX 3070 + RX 6700 XT
```
NVIDIA RTX 3070:   ~220-270 Mkey/s (CUDA)
AMD RX 6700 XT:    ~120-150 Mkey/s (OpenCL)
Combined:          ~340-420 Mkey/s
Efficiency:             95-98%
```

### Performance Expectations

- **CUDA vs OpenCL**: OpenCL typically 50-60% of CUDA performance on equivalent hardware
- **Combined efficiency**: Should be ≥ 90% of sum of individual GPUs
- **GPU utilization**: Both GPUs should show ≥ 90% utilization during full load
- **Work distribution**: Proportional to GPU performance (automatic)

## Troubleshooting Quick Reference

### Only NVIDIA GPU Detected

```bash
# Check OpenCL
ldd ./keyhunt | grep OpenCL
clinfo -l

# Rebuild
make clean && make
```

### Only AMD GPU Detected

```bash
# Check CUDA
ldd ./keyhunt | grep cuda
nvidia-smi

# Rebuild
make clean && CUDA_HOME=/usr/local/cuda make
```

### Poor Performance

1. **Check PCIe bandwidth**: `lspci -vv | grep LnkSta`
2. **Check thermals**: `nvidia-smi` and `rocm-smi --showtemp`
3. **Check power limits**: Both tools show power draw
4. **Monitor CPU**: Use `htop` to check for CPU bottleneck

### Unbalanced Work Distribution

- Verify adequate cooling (thermal throttling affects distribution)
- Check PCIe lane allocation (x16 preferred, x8 acceptable)
- Ensure power supply has sufficient capacity
- Monitor with `nvidia-smi` and `rocm-smi` simultaneously

### Crashes or Errors

1. Update drivers (NVIDIA + AMD/ROCm)
2. Reboot system
3. Test GPUs individually
4. Check system logs: `dmesg | tail -50`
5. Run under debugger if needed

## Acceptance Criteria Checklist

Quick checklist for validation:

```
Critical:
[ ] Both NVIDIA and AMD detected
[ ] Unified backend initializes
[ ] All test modes work (hash, full, hybrid)
[ ] Work distributed to both vendors
[ ] Results are correct
[ ] Combined throughput ≥ 90% of sum
[ ] No crashes during 5-minute test

Performance:
[ ] Both GPUs ≥ 90% utilization
[ ] Temperatures within safe limits
[ ] No thermal throttling

Monitoring:
[ ] nvidia-smi shows activity
[ ] rocm-smi shows activity
[ ] Statistics reported for both
```

## File Locations

### Test Infrastructure
```
tests/
├── test_mixed_vendor.sh              # Automated test script
├── MIXED_VENDOR_TEST_GUIDE.md        # Full testing guide
├── mixed_vendor_test_results.template.txt  # Results template
├── README_MIXED_VENDOR.md            # This file
└── mixed_vendor_results/             # Test output directory
```

### Source Code
```
src/gpu/
├── gpu_backend_unified.c             # Unified dispatcher
├── gpu_backend_cuda.cu               # CUDA backend
└── gpu_backend_opencl.c              # OpenCL backend
```

## Common Commands Reference

### Build Commands

```bash
# Clean build with both backends
make clean && make

# Build CUDA only
make clean && CUDA_HOME=/usr/local/cuda make

# Build OpenCL only
make clean && OPENCL=1 make

# Verify both backends linked
ldd ./keyhunt | grep -E "cuda|OpenCL"
```

### GPU Information Commands

```bash
# NVIDIA info
nvidia-smi --query-gpu=name,memory.total,compute_cap --format=csv

# AMD info
rocminfo | grep -E "Name:|Marketing|Compute Unit"

# OpenCL platforms
clinfo --list

# keyhunt detection
./keyhunt -L
```

### Monitoring Commands

```bash
# NVIDIA monitoring
nvidia-smi dmon -s um        # Utilization and memory
nvidia-smi dmon -s pct       # Power, clock, temp

# AMD monitoring
rocm-smi --showuse           # Utilization
rocm-smi --showtemp          # Temperature
rocm-smi --showpower         # Power draw
rocm-smi --showmeminfo       # Memory usage

# Combined monitoring
watch -n 1 'nvidia-smi; echo ""; rocm-smi'
```

### Performance Testing

```bash
# Quick performance test (30s)
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G full -s 30

# Extended performance test (5min)
./keyhunt -m address -f tests/66.txt -r 1:FFFFFFFFFFFF -G full -s 300

# Hash benchmark
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G hash -s 60

# Hybrid benchmark
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G hybrid -s 60
```

## Documentation Links

- **Full Test Guide**: `MIXED_VENDOR_TEST_GUIDE.md` - Comprehensive testing procedures
- **Test Template**: `mixed_vendor_test_results.template.txt` - Results documentation
- **Unified Backend**: `../src/gpu/gpu_backend_unified.c` - Implementation details

## Hardware Validation Status

Current status of hardware testing:

- **Infrastructure**: ✅ Complete (scripts, docs, templates)
- **Testing**: ⏳ Pending mixed-vendor hardware availability
- **Status**: Ready for immediate use when hardware available

## Quick Workflow

1. **Build**: `make clean && make`
2. **Verify**: `./keyhunt -L` (check both vendors listed)
3. **Test**: `./tests/test_mixed_vendor.sh 30`
4. **Monitor**: `nvidia-smi` + `rocm-smi` in separate terminals
5. **Document**: Fill in `mixed_vendor_test_results.template.txt`
6. **Validate**: Check acceptance criteria

## Support

For issues:
1. Check "Troubleshooting Quick Reference" above
2. Review full guide in `MIXED_VENDOR_TEST_GUIDE.md`
3. Test backends individually to isolate issue
4. Check system logs and driver versions

---

**Last Updated**: 2026-02-28
**Status**: Testing infrastructure complete, awaiting mixed-vendor hardware validation
