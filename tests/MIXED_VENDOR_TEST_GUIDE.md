# Mixed-Vendor GPU Testing Guide (NVIDIA + AMD)

This guide provides comprehensive testing procedures for validating the unified GPU backend that enables simultaneous operation of NVIDIA (CUDA) and AMD (OpenCL) GPUs in the same system.

## Overview

The unified backend dispatcher (`src/gpu/gpu_backend_unified.c`) enables keyhunt to utilize all available GPUs regardless of vendor. This is a critical feature for:

- **Multi-vendor workstations**: Systems with both NVIDIA and AMD GPUs
- **Maximum utilization**: Using all available hardware simultaneously
- **Flexibility**: Supporting users with mixed hardware configurations

## Target Hardware Configurations

### Recommended Test Configurations

#### Configuration 1: Entry-Level Mixed System
- **NVIDIA**: RTX 3060 / RTX 3060 Ti
- **AMD**: RX 6600 / RX 6600 XT
- **Use Case**: Budget-friendly dual-vendor testing

#### Configuration 2: Mid-Range Mixed System
- **NVIDIA**: RTX 3070 / RTX 3080
- **AMD**: RX 6700 XT / RX 6800
- **Use Case**: Balanced performance testing

#### Configuration 3: High-End Mixed System
- **NVIDIA**: RTX 4080 / RTX 4090
- **AMD**: RX 7900 XT / RX 7900 XTX
- **Use Case**: Maximum performance validation

#### Configuration 4: Multi-GPU Mixed System
- **NVIDIA**: 2x RTX 3080 or RTX 4090
- **AMD**: 2x RX 6800 XT or RX 7900 XTX
- **Use Case**: Extreme multi-device work distribution testing

### Minimum Requirements

- **At least 1 NVIDIA GPU** (CUDA compute capability 3.5+)
- **At least 1 AMD GPU** (RDNA 1/2/3 or Vega architecture)
- **CUDA Toolkit**: 11.0 or higher
- **ROCm**: 5.4 or higher
- **OpenCL Runtime**: 2.0 or higher
- **System RAM**: 16 GB minimum
- **PCIe Slots**: Sufficient for all GPUs with adequate cooling

## Prerequisites

### 1. NVIDIA CUDA Setup

**Ubuntu/Debian:**
```bash
# Install NVIDIA drivers
sudo ubuntu-drivers autoinstall

# Install CUDA Toolkit
wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-keyring_1.1-1_all.deb
sudo dpkg -i cuda-keyring_1.1-1_all.deb
sudo apt-get update
sudo apt-get install cuda-toolkit-12-3

# Verify installation
nvidia-smi
nvcc --version
```

**Minimum CUDA Version:** 11.0
**Recommended:** CUDA 12.0 or higher

### 2. AMD ROCm Setup

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

**Minimum ROCm Version:** 5.4
**Recommended:** ROCm 5.7 or 6.0+

### 3. OpenCL Runtime

```bash
# Install OpenCL headers and ICD loader
sudo apt-get install opencl-headers ocl-icd-libopencl1 ocl-icd-opencl-dev

# Install ROCm OpenCL runtime
sudo apt-get install rocm-opencl rocm-opencl-dev

# Verify OpenCL installation
clinfo -l
```

Expected output should show BOTH NVIDIA and AMD platforms:
```
Platform #0: NVIDIA CUDA
 `-- Device #0: NVIDIA GeForce RTX 4090

Platform #1: AMD Accelerated Parallel Processing
 `-- Device #0: AMD Radeon RX 7900 XTX
```

### 4. Build keyhunt with Both Backends

**Option A: Automatic Build (Recommended)**
```bash
# Clean previous builds
make clean

# Build with both CUDA and OpenCL
# Makefile will auto-detect both if properly installed
make
```

**Option B: Manual Verification**
```bash
# Verify CUDA is detected
make -n | grep HAVE_CUDA_BACKEND

# Verify OpenCL is detected
make -n | grep HAVE_OPENCL

# Build
make
```

**Verify Both Backends Are Linked:**
```bash
ldd ./keyhunt | grep -E "cuda|OpenCL"
```

Expected output:
```
libcudart.so.12 => /usr/local/cuda/lib64/libcudart.so.12
libOpenCL.so.1 => /usr/lib/x86_64-linux-gnu/libOpenCL.so.1
```

## Pre-Test Verification

### Step 1: GPU Detection

Verify all GPUs are detected by the system:

```bash
# NVIDIA GPUs
nvidia-smi --list-gpus

# AMD GPUs
rocm-smi --showproductname

# OpenCL devices
clinfo -l

# keyhunt unified detection
./keyhunt -L
```

**Expected keyhunt Output:**
```
Available GPU devices:
Device 0: NVIDIA GeForce RTX 4090 (CUDA)
  Compute Capability: 8.9
  Compute Units: 128
  Global Memory: 24 GB

Device 1: AMD Radeon RX 7900 XTX (OpenCL)
  Vendor: Advanced Micro Devices, Inc.
  Compute Units: 96
  Global Memory: 24 GB

Backend: Unified (CUDA + OpenCL)
Total devices: 2
```

### Step 2: Driver Verification

Check driver versions:

```bash
# NVIDIA driver
nvidia-smi --query-gpu=driver_version --format=csv,noheader

# CUDA version
nvcc --version

# ROCm version
cat /opt/rocm/.info/version

# OpenCL platform versions
clinfo | grep "Platform Version"
```

### Step 3: Backend Detection

Verify keyhunt detected both backends:

```bash
./keyhunt -L 2>&1 | grep -i backend
```

Expected output should mention "Unified" or show both CUDA and OpenCL.

## Automated Test Suite

### Running the Automated Tests

```bash
# Default: 30 seconds per test
./tests/test_mixed_vendor.sh

# Longer tests (60 seconds each)
./tests/test_mixed_vendor.sh 60

# Short tests (15 seconds each) - for quick validation
./tests/test_mixed_vendor.sh 15
```

### Test Suite Overview

The automated test suite includes 10 comprehensive tests:

#### Test 1: Mixed-Vendor Detection ✓
- **Purpose**: Verify both NVIDIA and AMD GPUs are detected
- **Critical**: Yes (test suite aborts if this fails)
- **Duration**: ~5 seconds
- **Pass Criteria**: Both vendor types appear in `keyhunt -L` output

#### Test 2: Unified Backend Initialization ✓
- **Purpose**: Verify unified backend initializes without errors
- **Critical**: Yes
- **Duration**: ~5 seconds
- **Pass Criteria**: No initialization errors

#### Test 3: Hash-Only Mode (All GPUs) ✓
- **Purpose**: Test SHA256+RIPEMD160 hashing on all GPUs
- **Mode**: `-G hash`
- **Duration**: Configurable (default 30s)
- **Pass Criteria**: Test completes without errors, shows throughput

#### Test 4: Full GPU Search Mode (All GPUs) ✓
- **Purpose**: Test complete GPU-accelerated search on all devices
- **Mode**: `-G full`
- **Duration**: Configurable (default 30s)
- **Pass Criteria**: Search runs on all GPUs, no device errors

#### Test 5: Hybrid Mode (CPU + All GPUs) ✓
- **Purpose**: Test coordinated CPU+GPU operation with mixed vendors
- **Mode**: `-G hybrid`
- **Duration**: Configurable (default 30s)
- **Pass Criteria**: CPU and all GPUs show activity

#### Test 6: Work Distribution Verification ✓
- **Purpose**: Verify work is distributed proportionally across different vendors
- **Duration**: 60 seconds minimum
- **Pass Criteria**: Multiple devices show activity, reasonable distribution

#### Test 7: Result Correctness Verification ✓
- **Purpose**: Verify unified backend produces correct results
- **Method**: Search for known key in small range
- **Pass Criteria**: Correct key found, matches expected value

#### Test 8: GPU Monitoring ✓
- **Purpose**: Collect utilization, temperature, power data during operation
- **Tools**: nvidia-smi, rocm-smi
- **Output**: Monitoring logs saved to results directory
- **Pass Criteria**: Data collection completes

#### Test 9: Performance Balance Check ✓
- **Purpose**: Verify both vendors are being utilized effectively
- **Duration**: 90 seconds
- **Pass Criteria**: Both NVIDIA and AMD GPUs show activity and reasonable throughput

#### Test 10: Stress Test (Optional) ✓
- **Purpose**: Extended stability testing under load
- **Duration**: 5 minutes
- **Interactive**: User confirmation required
- **Pass Criteria**: Completes without crashes or thermal throttling

### Test Results

Results are saved to:
```
./tests/mixed_vendor_results/mixed_vendor_test_YYYYMMDD_HHMMSS.txt
```

Monitoring logs are saved to:
```
./tests/mixed_vendor_results/nvidia_monitor_YYYYMMDD_HHMMSS.txt
./tests/mixed_vendor_results/amd_monitor_YYYYMMDD_HHMMSS.txt
```

## Manual Testing Procedures

### Test 1: Manual GPU Enumeration

```bash
./keyhunt -L
```

**Verify:**
- Both NVIDIA and AMD devices listed
- Correct device information (name, memory, compute units)
- Backend type shown correctly (CUDA for NVIDIA, OpenCL for AMD)

### Test 2: Manual Hash-Only Test

```bash
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G hash -s 60
```

**Verify:**
- Both GPUs show activity in nvidia-smi and rocm-smi
- Throughput reported for both devices
- No errors in output

**Monitor in separate terminals:**
```bash
# Terminal 1: NVIDIA monitoring
watch -n 1 nvidia-smi

# Terminal 2: AMD monitoring
watch -n 1 'rocm-smi --showuse --showtemp --showpower'
```

### Test 3: Manual Full GPU Search

```bash
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFFFF -G full -s 120
```

**Verify:**
- Work distribution across both GPUs
- Balanced GPU utilization (no idle GPUs)
- Consistent throughput from both vendors
- No CUDA or OpenCL errors

### Test 4: Manual Hybrid Mode Test

```bash
./keyhunt -m address -f tests/66.txt -r 1:FFFFFFFFFFFF -G hybrid -s 120
```

**Verify:**
- CPU threads active (check with `htop`)
- Both NVIDIA and AMD GPUs active
- Coordinated operation (no conflicts)
- Combined throughput higher than GPU-only mode

### Test 5: Manual BSGS Test (If Supported)

```bash
./keyhunt -m bsgs -f tests/125.txt -b 125 -G full -q -s 60
```

**Verify:**
- BSGS mode initializes correctly
- Both GPUs participate in BSGS computation
- Bloom filter operations work on both backends

### Test 6: Manual Result Verification

```bash
# Search for known key (puzzle #10)
./keyhunt -m address -f tests/1to32.txt -r 3FF:400 -G full -q
```

**Verify:**
- Correct key found: `3FF`
- Result matches known solution
- Finding occurs on either NVIDIA or AMD GPU (verify which in output)

## GPU Monitoring

### Real-Time Monitoring Setup

**Terminal 1: NVIDIA Monitoring**
```bash
watch -n 1 'nvidia-smi --query-gpu=index,name,utilization.gpu,temperature.gpu,power.draw,memory.used --format=csv,noheader'
```

**Terminal 2: AMD Monitoring**
```bash
watch -n 1 'rocm-smi --showuse --showtemp --showpower --showmemuse'
```

**Terminal 3: Combined System Monitor**
```bash
htop  # Monitor CPU and system resources
```

### Metrics to Monitor

#### NVIDIA GPU Metrics
- **GPU Utilization**: Should be 90-100% during full GPU mode
- **Temperature**: < 80°C recommended, < 85°C acceptable
- **Power Draw**: Close to TDP during full load
- **Memory Usage**: Check for sufficient VRAM allocation

#### AMD GPU Metrics
- **GPU Utilization**: Should be 90-100% during full GPU mode
- **Temperature**: < 85°C recommended, < 90°C acceptable (higher than NVIDIA is normal)
- **Power Draw**: Close to TDP during full load
- **Memory Usage**: Check for sufficient VRAM allocation

### Expected Performance Baselines

#### NVIDIA RTX 4000 Series (CUDA)
| GPU Model | Compute Units | Hash Rate (Mkey/s) | Full Search (Mkey/s) |
|-----------|---------------|-------------------|---------------------|
| RTX 4060 | 24 SMs | 180-220 | 150-180 |
| RTX 4060 Ti | 34 SMs | 250-300 | 200-250 |
| RTX 4070 | 46 SMs | 350-420 | 280-350 |
| RTX 4070 Ti | 60 SMs | 480-560 | 380-450 |
| RTX 4080 | 76 SMs | 620-720 | 500-600 |
| RTX 4090 | 128 SMs | 1000-1200 | 800-1000 |

#### NVIDIA RTX 3000 Series (CUDA)
| GPU Model | Compute Units | Hash Rate (Mkey/s) | Full Search (Mkey/s) |
|-----------|---------------|-------------------|---------------------|
| RTX 3060 | 28 SMs | 120-150 | 90-120 |
| RTX 3060 Ti | 38 SMs | 180-220 | 140-180 |
| RTX 3070 | 46 SMs | 220-270 | 170-220 |
| RTX 3070 Ti | 48 SMs | 240-290 | 190-240 |
| RTX 3080 | 68 SMs | 380-450 | 300-380 |
| RTX 3080 Ti | 80 SMs | 450-530 | 360-450 |
| RTX 3090 | 82 SMs | 480-560 | 380-480 |
| RTX 3090 Ti | 84 SMs | 500-580 | 400-500 |

#### AMD RX 7000 Series (OpenCL, RDNA 3)
| GPU Model | Compute Units | Hash Rate (Mkey/s) | Full Search (Mkey/s) |
|-----------|---------------|-------------------|---------------------|
| RX 7600 | 32 CUs | 120-150 | 90-120 |
| RX 7600 XT | 32 CUs | 130-160 | 100-130 |
| RX 7700 XT | 54 CUs | 200-250 | 160-200 |
| RX 7800 XT | 60 CUs | 230-280 | 180-230 |
| RX 7900 XT | 84 CUs | 280-330 | 220-280 |
| RX 7900 XTX | 96 CUs | 300-360 | 240-300 |

#### AMD RX 6000 Series (OpenCL, RDNA 2)
| GPU Model | Compute Units | Hash Rate (Mkey/s) | Full Search (Mkey/s) |
|-----------|---------------|-------------------|---------------------|
| RX 6600 | 28 CUs | 60-80 | 50-70 |
| RX 6600 XT | 32 CUs | 80-100 | 65-85 |
| RX 6700 XT | 40 CUs | 120-150 | 95-120 |
| RX 6800 | 60 CUs | 180-220 | 145-180 |
| RX 6800 XT | 72 CUs | 220-270 | 175-220 |
| RX 6900 XT | 80 CUs | 250-300 | 200-250 |

**Note**: OpenCL performance on AMD is typically 50-60% of equivalent NVIDIA CUDA performance due to differences in:
- Compiler optimization maturity
- Memory access patterns
- Wave/warp scheduling differences
- Driver overhead

### Mixed-Vendor Performance Expectations

#### Example: RTX 4090 + RX 7900 XTX
- **RTX 4090 (CUDA)**: ~1000-1200 Mkey/s
- **RX 7900 XTX (OpenCL)**: ~300-360 Mkey/s
- **Combined**: ~1300-1560 Mkey/s
- **Efficiency**: 95-98% of sum (small overhead for work distribution)

#### Example: RTX 3080 + RX 6800 XT
- **RTX 3080 (CUDA)**: ~380-450 Mkey/s
- **RX 6800 XT (OpenCL)**: ~220-270 Mkey/s
- **Combined**: ~600-720 Mkey/s
- **Efficiency**: 95-98% of sum

## Troubleshooting

### Issue 1: Only NVIDIA GPU Detected

**Symptoms:**
- `keyhunt -L` only shows NVIDIA devices
- No OpenCL platform visible

**Diagnosis:**
```bash
# Check OpenCL linking
ldd ./keyhunt | grep OpenCL

# Check OpenCL runtime
clinfo -l

# Check ROCm installation
rocminfo | grep "Name:"
```

**Solutions:**
1. Rebuild keyhunt with OpenCL:
   ```bash
   make clean
   make  # Should auto-detect OpenCL
   ```

2. Verify OpenCL runtime:
   ```bash
   sudo apt-get install rocm-opencl rocm-opencl-dev
   ```

3. Check ICD loader:
   ```bash
   ls -la /etc/OpenCL/vendors/
   ```

### Issue 2: Only AMD GPU Detected

**Symptoms:**
- `keyhunt -L` only shows AMD/OpenCL devices
- No CUDA platform visible

**Diagnosis:**
```bash
# Check CUDA linking
ldd ./keyhunt | grep cuda

# Check NVIDIA driver
nvidia-smi

# Check CUDA installation
nvcc --version
```

**Solutions:**
1. Rebuild keyhunt with CUDA:
   ```bash
   make clean
   CUDA_HOME=/usr/local/cuda make
   ```

2. Verify CUDA toolkit:
   ```bash
   export PATH=/usr/local/cuda/bin:$PATH
   export LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH
   ```

### Issue 3: Work Not Distributed Evenly

**Symptoms:**
- One GPU at 100%, other GPU underutilized
- Unbalanced throughput between devices

**Diagnosis:**
```bash
# Check device performance weights
./keyhunt -L

# Monitor both GPUs during operation
# Terminal 1:
watch -n 1 nvidia-smi

# Terminal 2:
watch -n 1 'rocm-smi --showuse'
```

**Possible Causes:**
1. **PCIe bandwidth limitation**: Check PCIe configuration
   ```bash
   lspci -vv | grep -E "VGA|3D|Display" -A 20 | grep LnkSta
   ```

2. **Thermal throttling**: Check temperatures
   ```bash
   nvidia-smi --query-gpu=temperature.gpu --format=csv
   rocm-smi --showtemp
   ```

3. **Power limit**: Check power draw
   ```bash
   nvidia-smi --query-gpu=power.draw,power.limit --format=csv
   rocm-smi --showpower
   ```

**Solutions:**
- Ensure adequate cooling for both GPUs
- Check power supply capacity (sufficient wattage)
- Verify PCIe slots provide full bandwidth (x16 lanes preferred)

### Issue 4: Backend Initialization Fails

**Symptoms:**
- Error message about backend initialization
- Crash on startup with multiple GPUs

**Diagnosis:**
```bash
# Run with debug output (if available)
KEYHUNT_DEBUG=1 ./keyhunt -L

# Check system logs
dmesg | tail -50

# Check CUDA errors
nvidia-smi -q | grep -i error

# Check OpenCL errors
clinfo | grep -i error
```

**Solutions:**
1. Update drivers:
   ```bash
   # NVIDIA
   sudo ubuntu-drivers autoinstall

   # AMD/ROCm
   sudo amdgpu-install --usecase=rocm
   ```

2. Reboot system to clear driver state

3. Test backends individually:
   ```bash
   # Test CUDA only (if option exists)
   ./keyhunt --force-cuda -L

   # Test OpenCL only
   ./keyhunt --force-opencl -L
   ```

### Issue 5: Crashes or Segmentation Faults

**Symptoms:**
- Segfault when using multiple GPUs
- Crashes during search operation

**Diagnosis:**
```bash
# Run under GDB
gdb --args ./keyhunt -m address -f tests/1to32.txt -G full
(gdb) run
# On crash:
(gdb) backtrace
```

**Solutions:**
1. Update to latest drivers
2. Check system RAM availability
3. Reduce batch sizes (if configurable)
4. Test GPUs individually
5. Check for memory leaks with valgrind:
   ```bash
   valgrind --leak-check=full ./keyhunt -L
   ```

### Issue 6: Lower Than Expected Performance

**Symptoms:**
- Combined throughput less than 90% of sum of individual GPUs
- Significant overhead when running mixed-vendor

**Diagnosis:**
1. Test GPUs individually:
   ```bash
   # NVIDIA only (unplug AMD or use device selection if available)
   ./keyhunt -m address -f tests/1to32.txt -G full -s 60

   # AMD only (unplug NVIDIA or use device selection if available)
   ./keyhunt -m address -f tests/1to32.txt -G full -s 60
   ```

2. Check PCIe configuration:
   ```bash
   sudo lspci -vv | grep -E "VGA|3D|Display" -A 20
   ```

3. Monitor CPU utilization (overhead):
   ```bash
   htop  # Check if CPU is bottleneck
   ```

**Solutions:**
- Ensure both GPUs have sufficient PCIe bandwidth (x16 or x8 minimum)
- Check for CPU bottlenecks (upgrade CPU if necessary)
- Verify adequate system RAM
- Disable unnecessary background processes
- Ensure both GPUs are in performance mode (not power-saving)

## Acceptance Criteria Checklist

Before marking the mixed-vendor testing as complete, verify ALL of the following:

### Critical Acceptance Criteria

- [ ] **Both NVIDIA and AMD GPUs detected** by keyhunt -L
- [ ] **Backend type shows "Unified"** or indicates both CUDA and OpenCL
- [ ] **Hash-only mode runs successfully** on all GPUs
- [ ] **Full GPU search mode runs successfully** on all GPUs
- [ ] **Hybrid mode works** with CPU + all GPUs
- [ ] **Work is distributed** to both NVIDIA and AMD devices
- [ ] **Results are correct** (matches known solutions)
- [ ] **No crashes or segfaults** during extended operation
- [ ] **Combined throughput ≥ 90%** of sum of individual GPUs
- [ ] **No device errors** in nvidia-smi or rocm-smi output

### Performance Criteria

- [ ] **NVIDIA GPU utilization ≥ 90%** during full load
- [ ] **AMD GPU utilization ≥ 90%** during full load
- [ ] **Thermal stability**: Temperatures remain within safe limits
- [ ] **Power draw reasonable** for both GPUs under load
- [ ] **No thermal throttling** observed

### Monitoring Criteria

- [ ] **nvidia-smi shows activity** during mixed-vendor operation
- [ ] **rocm-smi shows activity** during mixed-vendor operation
- [ ] **Both GPUs contribute** to overall throughput
- [ ] **Statistics reported** for both devices
- [ ] **Monitoring logs captured** successfully

### Stability Criteria

- [ ] **5-minute stress test completes** without issues
- [ ] **No memory leaks** observed over extended runtime
- [ ] **Consistent performance** throughout test duration
- [ ] **Clean shutdown** after tests complete

### Documentation Criteria

- [ ] **Test results documented** in template
- [ ] **GPU models recorded** (exact SKUs)
- [ ] **Driver versions recorded** (CUDA, ROCm, kernel)
- [ ] **Performance numbers recorded** for both vendors
- [ ] **Any issues noted** with workarounds

## Test Results Documentation

After completing all tests, document your results using the provided template:

```bash
cp tests/mixed_vendor_test_results.template.txt \
   tests/mixed_vendor_results/results_$(date +%Y%m%d).txt
```

Fill in all sections:
1. System Configuration (hardware, drivers)
2. Individual test results (pass/fail, performance data)
3. GPU monitoring observations
4. Performance analysis
5. Issues encountered and resolutions
6. Acceptance criteria verification
7. Conclusions and recommendations

## Additional Resources

- **Unified Backend Source**: `src/gpu/gpu_backend_unified.c`
- **CUDA Backend Source**: `src/gpu/gpu_backend_cuda.cu`
- **OpenCL Backend Source**: `src/gpu/gpu_backend_opencl.c`
- **Automated Test Script**: `tests/test_mixed_vendor.sh`
- **Quick Reference**: `tests/README_MIXED_VENDOR.md`

## Next Steps

After successful mixed-vendor validation:

1. Document results in implementation plan
2. Report any performance anomalies or bugs
3. Consider multi-GPU scaling tests (2+ of each vendor)
4. Test on different hardware generations
5. Profile for optimization opportunities

## Support

For issues or questions:
- Check troubleshooting section above
- Review unified backend source code
- Test backends individually to isolate issues
- Provide full system details when reporting bugs
