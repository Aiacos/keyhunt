# AMD RX 6000/7000 Series GPU Testing Guide

This guide provides comprehensive testing procedures for validating OpenCL backend support on AMD Radeon RX 6000 series (RDNA 2) and RX 7000 series (RDNA 3) GPUs.

## Target Hardware

### AMD RX 6000 Series (RDNA 2 - gfx1030)
- RX 6500 XT
- RX 6600
- RX 6600 XT
- RX 6650 XT
- RX 6700 XT
- RX 6750 XT
- RX 6800
- RX 6800 XT
- RX 6900 XT
- RX 6950 XT

### AMD RX 7000 Series (RDNA 3 - gfx1100/gfx1101/gfx1102)
- RX 7600
- RX 7600 XT
- RX 7700 XT
- RX 7800 XT
- RX 7900 XT
- RX 7900 XTX
- RX 7900 GRE

## Prerequisites

### 1. ROCm Installation

**Ubuntu/Debian:**
```bash
# Add ROCm repository
wget https://repo.radeon.com/amdgpu-install/latest/ubuntu/jammy/amdgpu-install_5.7.50700-1_all.deb
sudo dpkg -i amdgpu-install_*.deb
sudo apt-get update

# Install ROCm
sudo amdgpu-install --usecase=rocm

# Verify installation
rocm-smi --showproductname
rocminfo | grep "Name:"
```

**Minimum ROCm Version:** 5.4.0 or higher
**Recommended:** ROCm 5.7 or 6.0+

### 2. OpenCL Runtime

```bash
# Install OpenCL headers and ICD loader
sudo apt-get install opencl-headers ocl-icd-libopencl1 ocl-icd-opencl-dev

# Install ROCm OpenCL runtime
sudo apt-get install rocm-opencl rocm-opencl-dev

# Verify OpenCL installation
clinfo -l
```

### 3. Build keyhunt with OpenCL

```bash
# Auto-detect ROCm and build
./build_opencl.sh

# Or manually with specific ROCm path
ROCM_PATH=/opt/rocm ./build_opencl.sh

# Verify OpenCL is linked
ldd ./keyhunt | grep OpenCL
```

## Pre-Test Verification

### 1. GPU Detection

Verify your AMD GPU is detected:

```bash
# ROCm device detection
rocm-smi --showproductname
rocminfo | grep -A 5 "Name:"

# OpenCL device detection
clinfo -l

# keyhunt device enumeration
./keyhunt -L
```

**Expected Output:**
```
Available GPU devices:
Device 0: AMD Radeon RX 7900 XTX (OpenCL)
  Vendor: Advanced Micro Devices, Inc.
  Compute Units: 96
  Global Memory: 24 GB
  Max Work Group Size: 1024
```

### 2. GPU Architecture Verification

Confirm your GPU architecture (critical for kernel optimization):

```bash
rocminfo | grep "Name:" | grep -E "gfx1030|gfx1100|gfx1101|gfx1102"
```

**RX 6000 Series:** Should show `gfx1030`
**RX 7000 Series:** Should show `gfx1100`, `gfx1101`, or `gfx1102`

### 3. Driver Version

```bash
cat /opt/rocm/.info/version  # ROCm version
modinfo amdgpu | grep version  # Kernel driver version
```

**Minimum Kernel Driver:** amdgpu 5.4+

## Running the Automated Test Suite

### Quick Test (30 seconds per test)

```bash
cd /path/to/keyhunt
./tests/test_amd_rx6000_rx7000.sh
```

### Extended Test (60 seconds per test)

```bash
./tests/test_amd_rx6000_rx7000.sh 60
```

### With Stress Test (includes 5-minute stability test)

```bash
RUN_STRESS_TEST=1 ./tests/test_amd_rx6000_rx7000.sh 60
```

## Test Suite Details

### Test 1: Device Detection and Initialization
**Purpose:** Verify OpenCL platform and device enumeration
**Expected:** All AMD GPUs listed with correct specifications
**Duration:** < 1 second

### Test 2: OpenCL Kernel Compilation
**Purpose:** Verify GPU kernels compile for AMD RDNA architecture
**Expected:** No compilation errors, kernels loaded successfully
**Duration:** 5-10 seconds (first run, cached after)

### Test 3: Hash-Only Mode (SHA256 + RIPEMD160)
**Purpose:** Test pure hashing performance
**Command:** `-G hash` mode
**Metrics:** Throughput (Mkey/s), GPU utilization, memory bandwidth
**Duration:** Configurable (default 30s)

### Test 4: Full GPU Search Mode
**Purpose:** Test complete search pipeline with target comparison
**Command:** `-G full` mode
**Metrics:** Throughput, GPU utilization, target hits
**Duration:** Configurable (default 30s)

### Test 5: Hybrid CPU+GPU Mode
**Purpose:** Test coordinated CPU+GPU execution
**Command:** `-G hybrid -t 4` mode
**Metrics:** Combined throughput, work distribution
**Duration:** Configurable (default 30s)

### Test 6: Multi-GPU Mode
**Purpose:** Test multiple AMD GPUs simultaneously (if available)
**Requirements:** 2+ AMD GPUs
**Metrics:** Per-device throughput, load balancing
**Duration:** Configurable (default 30s)

### Test 7: BSGS Mode
**Purpose:** Test BSGS algorithm with GPU acceleration
**Command:** `-m bsgs -b 125 -G full`
**Requirements:** `tests/125.txt` file
**Duration:** Configurable (default 30s)

### Test 8: Auto-Tuning for AMD RDNA
**Purpose:** Verify auto-tuning selects optimal parameters
**Expected:** Vendor-specific tuning for AMD
**Parameters Tested:**
- Blocks per CU: 24-32 (RDNA 2/3)
- Keys per work-item: 1024-2048
- Work-group size: 256

### Test 9: Stress Test (Optional)
**Purpose:** Extended stability under sustained load
**Duration:** 5 minutes
**Monitoring:** Temperature, power, throttling
**Command:** Set `RUN_STRESS_TEST=1`

## GPU Monitoring During Tests

### Real-Time Monitoring

**Terminal 1 - Run tests:**
```bash
./tests/test_amd_rx6000_rx7000.sh 60
```

**Terminal 2 - Monitor GPU:**
```bash
# Continuous monitoring (update every 2 seconds)
watch -n 2 rocm-smi

# Or detailed monitoring
watch -n 2 'rocm-smi --showtemp --showpower --showuse --showmemuse'
```

### Monitor Metrics

| Metric | Tool | Acceptable Range |
|--------|------|------------------|
| GPU Utilization | `rocm-smi --showuse` | 95-100% during tests |
| Temperature | `rocm-smi --showtemp` | < 85°C (junction) |
| Power Consumption | `rocm-smi --showpower` | Varies by model |
| Memory Usage | `rocm-smi --showmemuse` | < 90% of VRAM |
| Clock Speed | `rocm-smi --showclock` | Should be at max boost |

### Expected GPU Utilization

- **Hash-Only Mode:** 98-100% GPU utilization
- **Full Search Mode:** 95-100% GPU utilization
- **Hybrid Mode:** 80-95% GPU utilization (CPU sharing work)

## Manual Testing Procedures

If you prefer manual testing or need to debug specific issues:

### 1. Basic Functionality Test

```bash
# Simple address search
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G full -s 30

# Expected: Should find keys, show ~100+ Mkey/s on RX 6600 XT
```

### 2. Performance Benchmark

```bash
# Hash-only performance
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFFFFFFFFFF -G hash -s 60

# Full search performance
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFFFFFFFFFF -G full -s 60
```

### 3. BSGS Test (if available)

```bash
# BSGS with GPU acceleration
./keyhunt -m bsgs -f tests/125.txt -b 125 -G full -q -s 60
```

### 4. Multi-GPU Test

```bash
# Should auto-detect and use all AMD GPUs
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G full -s 60 -L
```

## Expected Performance Baselines

### Hash-Only Mode Throughput

| GPU Model | Expected Throughput | Compute Units | VRAM |
|-----------|---------------------|---------------|------|
| RX 6500 XT | 60-80 Mkey/s | 16 | 4 GB |
| RX 6600 | 90-110 Mkey/s | 28 | 8 GB |
| RX 6600 XT | 100-120 Mkey/s | 32 | 8 GB |
| RX 6650 XT | 105-125 Mkey/s | 32 | 8 GB |
| RX 6700 XT | 130-160 Mkey/s | 40 | 12 GB |
| RX 6750 XT | 140-170 Mkey/s | 40 | 12 GB |
| RX 6800 | 180-220 Mkey/s | 60 | 16 GB |
| RX 6800 XT | 200-240 Mkey/s | 72 | 16 GB |
| RX 6900 XT | 220-260 Mkey/s | 80 | 16 GB |
| RX 6950 XT | 230-270 Mkey/s | 80 | 16 GB |
| RX 7600 | 120-150 Mkey/s | 32 | 8 GB |
| RX 7600 XT | 130-160 Mkey/s | 32 | 16 GB |
| RX 7700 XT | 180-220 Mkey/s | 54 | 12 GB |
| RX 7800 XT | 220-260 Mkey/s | 60 | 16 GB |
| RX 7900 XT | 240-280 Mkey/s | 84 | 20 GB |
| RX 7900 XTX | 260-300 Mkey/s | 96 | 24 GB |
| RX 7900 GRE | 230-270 Mkey/s | 80 | 16 GB |

**Note:** Performance varies based on ROCm version, driver, and system configuration.

### Full Search Mode

Expect 5-10% lower throughput than hash-only mode due to target comparison overhead.

## Troubleshooting

### Issue 1: No OpenCL Devices Detected

**Symptoms:**
```
No GPU devices available
```

**Solutions:**
1. Check ROCm installation:
   ```bash
   rocm-smi --showproductname
   ```

2. Verify OpenCL ICD registration:
   ```bash
   ls /etc/OpenCL/vendors/
   # Should show amdocl64.icd
   ```

3. Check permissions:
   ```bash
   sudo usermod -a -G video,render $USER
   # Log out and log back in
   ```

4. Verify kernel driver:
   ```bash
   lsmod | grep amdgpu
   ```

### Issue 2: Kernel Compilation Errors

**Symptoms:**
```
error: OpenCL kernel compilation failed
```

**Solutions:**
1. Check ROCm OpenCL compiler:
   ```bash
   ls /opt/rocm/bin/clang*
   ```

2. Verify GPU architecture support:
   ```bash
   rocminfo | grep "gfx"
   ```

3. Update ROCm to latest version (5.7+)

4. Check kernel source files exist:
   ```bash
   ls src/gpu/gpu_hash_opencl.cl
   ls src/gpu/gpu_secp256k1_opencl.cl
   ```

### Issue 3: Low Performance

**Symptoms:**
- GPU utilization < 80%
- Throughput significantly below expected baseline

**Solutions:**
1. Verify GPU is running at max clocks:
   ```bash
   rocm-smi --showclock
   # Set performance mode if needed:
   rocm-smi --setperflevel high
   ```

2. Check for thermal throttling:
   ```bash
   rocm-smi --showtemp
   # Junction temp should be < 85°C
   ```

3. Verify no other GPU workloads:
   ```bash
   rocm-smi --showpids
   ```

4. Try different work-group sizes:
   ```bash
   # Auto-tuning should handle this, but can force rebuild
   rm -rf ~/.cache/keyhunt/*
   ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G full
   ```

### Issue 4: GPU Crashes or Hangs

**Symptoms:**
- GPU stops responding
- Driver reset messages in `dmesg`

**Solutions:**
1. Check dmesg for errors:
   ```bash
   dmesg | grep -i amdgpu | tail -20
   ```

2. Reduce memory pressure:
   ```bash
   # Test with smaller batch sizes
   # (Auto-tuning should prevent this)
   ```

3. Update kernel driver:
   ```bash
   sudo apt-get update && sudo apt-get upgrade
   ```

4. Check PSU and power delivery (especially for RX 6900 XT / 7900 XTX)

### Issue 5: Memory Errors

**Symptoms:**
```
error: clEnqueueWriteBuffer failed
error: out of memory
```

**Solutions:**
1. Check available VRAM:
   ```bash
   rocm-smi --showmemuse
   ```

2. Reduce search range or target count

3. Close other GPU applications

## Test Results Documentation

After running tests, document your results using the template:

```bash
cp tests/amd_test_results/amd_rx_test_TIMESTAMP.txt tests/amd_test_results/my_gpu_results.txt
```

Include in your test report:
- GPU model and specifications
- ROCm version
- Driver version
- All test results (pass/fail)
- Performance metrics (throughput)
- GPU monitoring data (temp, power, utilization)
- Any errors or warnings encountered
- System configuration (CPU, RAM, OS)

## Acceptance Criteria

For this subtask to be considered complete, the following must be verified:

- ✅ AMD RX 6000 or 7000 series GPU detected by OpenCL
- ✅ OpenCL kernels compile successfully (no errors)
- ✅ Device information correctly reported (name, compute units, VRAM)
- ✅ Hash-only mode runs without errors
- ✅ Full GPU search mode runs without errors
- ✅ Throughput within expected range for GPU model
- ✅ GPU utilization > 90% during tests
- ✅ No crashes, hangs, or driver resets
- ✅ Results documented with GPU model and driver version

## Comparison with NVIDIA CUDA

For reference, compare OpenCL (AMD) performance with CUDA (NVIDIA):

| AMD OpenCL | NVIDIA CUDA | Relative Performance |
|------------|-------------|----------------------|
| RX 6900 XT (240 Mkey/s) | RTX 3080 (260 Mkey/s) | 92% |
| RX 7900 XTX (280 Mkey/s) | RTX 4090 (425 Mkey/s) | 66% |
| RX 6600 XT (110 Mkey/s) | RTX 3060 (140 Mkey/s) | 79% |

**Target:** OpenCL should achieve ≥ 50% of equivalent NVIDIA CUDA performance.

## Next Steps

After completing AMD GPU testing:
1. Document results in `tests/amd_test_results/`
2. Update subtask-5-3 status to "completed"
3. Proceed to subtask-5-4: Test mixed-vendor systems (NVIDIA + AMD simultaneously)

## Additional Resources

- [ROCm Documentation](https://rocmdocs.amd.com/)
- [AMD GPU Architecture](https://www.amd.com/en/technologies/rdna-2)
- [OpenCL Programming Guide](https://www.khronos.org/opencl/)
- [keyhunt OpenCL Backend Documentation](../docs/OPENCL_BACKEND.md)

## Support

If you encounter issues not covered in this guide:
1. Check `dmesg` and `~/.cache/keyhunt/` for error logs
2. Run tests with verbose output: `./keyhunt -m address ... -v`
3. Report issues with full system information and test results
