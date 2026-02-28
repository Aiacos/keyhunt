# OpenCL Backend Documentation

## Overview

Keyhunt supports optional OpenCL GPU acceleration for AMD GPUs and other OpenCL-compatible devices. The OpenCL backend is auto-detected at compile time and can run alongside CUDA on mixed-vendor systems.

## Build Requirements

### OpenCL Runtime
- **Supported versions**: OpenCL 1.2 - 3.0
- **Required**: AMD GPU with GCN 3.0+ architecture or newer

### Supported GPUs

| Architecture | GPUs | Minimum |
|--------------|------|---------|
| RDNA 3 | RX 7000 series | Recommended |
| RDNA 2 | RX 6000 series | Recommended |
| RDNA 1 | RX 5000 series | Supported |
| GCN 5.0 | Vega 56/64, Radeon VII | Supported |
| GCN 4.0 | RX 400/500 series | Supported |
| GCN 3.0 | R9 Fury, R9 Nano | Minimum |

### ROCm Installation (AMD GPUs)

#### Ubuntu/Debian
```bash
# Add ROCm repository
wget -q -O - https://repo.radeon.com/rocm/rocm.gpg.key | sudo apt-key add -
echo 'deb [arch=amd64] https://repo.radeon.com/rocm/apt/debian/ ubuntu main' | sudo tee /etc/apt/sources.list.d/rocm.list

# Install ROCm
sudo apt update
sudo apt install rocm-opencl-dev clinfo

# Add user to video/render groups
sudo usermod -a -G video,render $USER
```

#### Arch Linux
```bash
# Install ROCm
sudo pacman -S rocm-opencl-runtime rocm-opencl-sdk clinfo

# Add user to video/render groups
sudo usermod -a -G video,render $USER
```

#### Verify Installation
```bash
# List OpenCL platforms
clinfo

# Check AMD GPU detection
rocminfo
```

### Generic OpenCL Installation (Intel, Mali, etc.)

For non-AMD OpenCL devices:
```bash
# Ubuntu/Debian
sudo apt install ocl-icd-opencl-dev clinfo

# Arch Linux
sudo pacman -S ocl-icd opencl-headers clinfo
```

## Quick Build

### Automatic Build (Recommended)

The `build_opencl.sh` script handles everything automatically:

```bash
./build_opencl.sh
```

This script:
1. Detects OpenCL installation location
2. Auto-detects available OpenCL devices
3. Selects optimal compilation flags
4. Builds with OpenCL support

### Manual Build

For manual builds with specific settings:

```bash
# Clean previous build
make clean

# Build with OpenCL
make OPENCL=1 \
     OPENCL_INCLUDE=/opt/rocm/include \
     OPENCL_LIB=/opt/rocm/lib
```

### Build with Both CUDA and OpenCL

Keyhunt can be built with both backends for mixed-vendor systems:

```bash
# Use both build scripts
./build_cuda.sh
./build_opencl.sh --with-cuda

# Or manually
make NVCC=/usr/local/cuda/bin/nvcc \
     OPENCL=1 \
     OPENCL_INCLUDE=/opt/rocm/include \
     OPENCL_LIB=/opt/rocm/lib
```

### Build Without OpenCL

To build CPU-only or CUDA-only version:

```bash
make clean
make
```

The Makefile auto-detects OpenCL libraries and builds without OpenCL support if unavailable.

## Device Selection

OpenCL supports multiple vendors on the same system:

### List Available Devices

```bash
# List all OpenCL platforms and devices
clinfo -l

# Keyhunt device enumeration
./keyhunt --list-opencl-devices
```

### Platform and Device IDs

OpenCL uses platform and device indices:

```bash
# Use specific platform and device
./keyhunt -m address -f targets.txt -b 66 --opencl-platform 0 --opencl-device 0

# Auto-select AMD device (preferred)
./keyhunt -m address -f targets.txt -b 66 --opencl-vendor AMD
```

### Mixed Vendor Setup

Example system with NVIDIA + AMD:

```bash
# Use NVIDIA via CUDA
CUDA_VISIBLE_DEVICES=0 ./keyhunt -m address -f targets.txt -b 66 -G full

# Use AMD via OpenCL (terminal 2)
./keyhunt -m address -f targets.txt -b 66 --opencl-vendor AMD --opencl-device 0
```

## GPU Modes

OpenCL supports the same modes as CUDA with the `--opencl` flag:

| Flag | Description | CPU Load | GPU Load |
|------|-------------|----------|----------|
| `--opencl off` | Disable OpenCL, use CPU only | 100% | 0% |
| `--opencl auto` | Auto-detect best mode (default) | Varies | Varies |
| `--opencl hash` | GPU computes SHA256+RIPEMD160 only | High | Medium |
| `--opencl full` | Full GPU search (ECC + hash + matching) | Low | 100% |
| `--opencl hybrid` | GPU+CPU in parallel | 100% | 100% |

### Hash-Only Mode (`--opencl hash`)
- CPU generates elliptic curve points
- GPU computes HASH160 (SHA256 + RIPEMD160)
- Best for: Systems where CPU ECC is faster than GPU (older GPUs)

### Full GPU Mode (`--opencl full`)
- GPU performs all operations: ECC point generation, hashing, and target matching
- Best for: RDNA 2/3 GPUs (RX 6000/7000), large search ranges
- Supports GPU-side bloom/target matching

### Hybrid Mode (`--opencl hybrid`)
Hybrid runs OpenCL FULL and CPU in parallel.

Optional work-stealing (dynamic load balancing):
- `KEYHUNT_HYBRID_WORK_STEAL=1`
- Requires non-random mode and stride=1
- `KEYHUNT_OPENCL_BLOCK_SIZE=...` (keys per block; accepts decimal or `0x...`)

## Runtime Tuning

You can tune OpenCL kernel parameters at runtime (no rebuild needed):

```bash
# Set work group size (64-1024, must be power of 2)
export KEYHUNT_OPENCL_WORKGROUP_SIZE=256

# Set keys per work item (64-65536)
export KEYHUNT_OPENCL_KEYS_PER_ITEM=2048

# Set global work size multiplier (1-64)
export KEYHUNT_OPENCL_GLOBAL_MULTIPLIER=16

# Enable auto-tuning (finds optimal params automatically)
export KEYHUNT_OPENCL_AUTOTUNE=1

# Force specific AMD optimization path
export KEYHUNT_OPENCL_AMD_OPTIMIZE=1
```

### AMD-Specific Tuning

For RDNA 2/3 GPUs (RX 6000/7000):

```bash
# Optimal settings for RX 6900 XT / RX 7900 XTX
export KEYHUNT_OPENCL_WORKGROUP_SIZE=256
export KEYHUNT_OPENCL_KEYS_PER_ITEM=4096
export KEYHUNT_OPENCL_GLOBAL_MULTIPLIER=32
```

For GCN GPUs (RX 400/500, Vega):

```bash
# Optimal settings for Vega 56/64, RX 580
export KEYHUNT_OPENCL_WORKGROUP_SIZE=256
export KEYHUNT_OPENCL_KEYS_PER_ITEM=2048
export KEYHUNT_OPENCL_GLOBAL_MULTIPLIER=16
```

## Supported Search Modes

| Mode | OpenCL Hash | OpenCL Full |
|------|-------------|-------------|
| `rmd160` | Yes | Yes |
| `address` | No | Yes |
| `xpoint` | No | Planned |
| `bsgs` | No | No |

Notes:
- `OpenCL Full` supports `-l compress`, `-l uncompress`, and `-l both` for BTC.
- `-l both` is slower than `-l compress` because it computes and checks both pubkey encodings.
- `OpenCL Hash` mode supports only compressed pubkeys (`-l compress`).

## Performance

Benchmark comparison (puzzle 71, compress mode):

| GPU | Mode | Speed | vs NVIDIA |
|-----|------|-------|-----------|
| RTX 2080 SUPER | CUDA Full | ~600 Mkeys/s | Baseline |
| RX 7900 XTX | OpenCL Full | ~450-500 Mkeys/s | ~83% |
| RX 6900 XT | OpenCL Full | ~380-420 Mkeys/s | ~70% |
| Vega 64 | OpenCL Full | ~200-250 Mkeys/s | ~40% |
| RX 580 8GB | OpenCL Full | ~120-150 Mkeys/s | ~23% |

Performance factors:
- RDNA 2/3 GPUs perform 70-85% of equivalent NVIDIA performance
- GCN architecture performs 40-50% of equivalent NVIDIA performance
- ROCm 5.4+ provides better optimization than older versions
- Memory bandwidth is critical (use high-end VRAM for best results)

Run your own benchmark:
```bash
./keyhunt --benchmark --opencl
```

## Multi-GPU Support

### List Available OpenCL Devices

```bash
clinfo -l
# or
./keyhunt --list-opencl-devices
```

### Select Specific GPU

```bash
# Use platform 0, device 0
./keyhunt -m address -f targets.txt -b 66 --opencl-platform 0 --opencl-device 0

# Use platform 0, device 1
./keyhunt -m address -f targets.txt -b 66 --opencl-platform 0 --opencl-device 1
```

### Use Multiple AMD GPUs

Run multiple instances with different device IDs:

```bash
# Terminal 1 - AMD GPU 0
./keyhunt -m address -f targets.txt -b 66 --opencl-device 0 --opencl full

# Terminal 2 - AMD GPU 1
./keyhunt -m address -f targets.txt -b 66 --opencl-device 1 --opencl full
```

## Troubleshooting

### "OpenCL backend not available"

1. Check OpenCL runtime is installed:
   ```bash
   clinfo
   ```

2. Check ROCm installation (AMD):
   ```bash
   rocminfo
   ls /opt/rocm
   ```

3. Verify user permissions:
   ```bash
   groups $USER | grep -E 'video|render'
   ```

4. Rebuild with explicit OpenCL path:
   ```bash
   ./build_opencl.sh --opencl-path /opt/rocm
   ```

### "No OpenCL devices found"

1. Check device visibility:
   ```bash
   clinfo -l
   rocm-smi  # AMD only
   ```

2. Verify kernel modules (AMD):
   ```bash
   lsmod | grep amdgpu
   ```

3. Check BIOS settings:
   - Enable IOMMU/Above 4G Decoding
   - Set PCIe to Gen3/Gen4

### Compilation errors with ROCm

Use the build script which handles paths automatically:

```bash
# Automatic (recommended)
./build_opencl.sh

# Manual with specific ROCm version
make OPENCL=1 \
     OPENCL_INCLUDE=/opt/rocm-5.7.0/include \
     OPENCL_LIB=/opt/rocm-5.7.0/lib
```

### Runtime errors

1. Check OpenCL ICD loader:
   ```bash
   ls /etc/OpenCL/vendors/
   ```

2. Verify library path:
   ```bash
   ldd keyhunt | grep -i opencl
   ```

3. Set library path if needed:
   ```bash
   export LD_LIBRARY_PATH=/opt/rocm/lib:$LD_LIBRARY_PATH
   ```

### "Device does not support required features"

Your GPU is too old. Minimum: GCN 3.0 architecture (R9 Fury)

Check your GPU:
```bash
rocminfo | grep "Marketing Name"
clinfo | grep "Device Name"
```

### Slow OpenCL Performance

1. Check GPU utilization:
   ```bash
   rocm-smi --showuse  # AMD
   watch -n 1 rocm-smi  # Monitor in real-time
   ```

2. Try different modes:
   ```bash
   ./keyhunt -m address -f targets.txt -b 66 --opencl full    # Full GPU
   ./keyhunt -m address -f targets.txt -b 66 --opencl hybrid  # CPU+GPU
   ```

3. Enable auto-tuning:
   ```bash
   KEYHUNT_OPENCL_AUTOTUNE=1 ./keyhunt -m address -f targets.txt -b 66 --opencl full
   ```

4. Use AMD-specific optimizations:
   ```bash
   KEYHUNT_OPENCL_AMD_OPTIMIZE=1 ./keyhunt -m address -f targets.txt -b 66 --opencl full
   ```

### Black screen / system hang on AMD

This can happen with aggressive GPU usage:

```bash
# Reduce work group size
export KEYHUNT_OPENCL_WORKGROUP_SIZE=128

# Reduce global multiplier
export KEYHUNT_OPENCL_GLOBAL_MULTIPLIER=8

# Use hybrid mode instead of full
./keyhunt -m address -f targets.txt -b 66 --opencl hybrid
```

## Architecture

```
src/gpu/
├── gpu_backend.h              # C API header (shared with CUDA)
├── gpu_backend_opencl.cpp     # OpenCL implementation (ECC + hashing + matching)
├── kernels/
│   ├── secp256k1_opencl.cl    # OpenCL secp256k1 ECC kernels
│   ├── sha256_opencl.cl       # OpenCL SHA256 kernel
│   └── ripemd160_opencl.cl    # OpenCL RIPEMD160 kernel
├── opencl_platform.c          # Platform/device detection
├── opencl_autotune.c          # Auto-tuning logic
└── opencl_utils.c             # Helper functions
```

The backend uses a C interface for compatibility with the CUDA backend:
- `gpu_backend_init()` - Initialize OpenCL, detect devices
- `gpu_upload_gtable()` - Upload precomputed G table
- `gpu_upload_targets()` - Upload target hashes
- `gpu_full_search()` - Full GPU search (ECC + hash + matching)
- `gpu_benchmark()` - Benchmark OpenCL performance

OpenCL-specific functions:
- `opencl_list_devices()` - Enumerate platforms and devices
- `opencl_select_device()` - Select device by platform/vendor/index
- `opencl_compile_kernels()` - JIT compile kernels with optimizations

## Platform-Specific Notes

### AMD RDNA 2/3 (RX 6000/7000)

**Best performance tier** - Recommended for OpenCL acceleration:
- Excellent compute performance
- High memory bandwidth
- Good ROCm support (ROCm 5.4+)
- Competitive with NVIDIA mid-range GPUs

### AMD GCN (RX 400/500, Vega)

**Legacy support** - Works but slower:
- Older architecture with lower performance
- Limited to ROCm 5.x (Vega) or AMDGPU-PRO (Polaris)
- Best used in hybrid mode with CPU

### Intel GPUs

**Experimental support**:
- Requires Intel Compute Runtime
- Best for integrated GPUs (testing/development)
- Not recommended for production use

### Apple Silicon (M1/M2/M3)

**Not supported**:
- Apple uses Metal, not OpenCL
- No plans for Metal backend

## ROCm Version Compatibility

| ROCm Version | GPU Support | Status |
|--------------|-------------|--------|
| ROCm 6.x | RDNA 3, RDNA 2 | Recommended |
| ROCm 5.4-5.7 | RDNA 2, RDNA 1, Vega | Supported |
| ROCm 4.x | Vega, GCN 4/5 | Legacy |
| AMDGPU-PRO | GCN 3/4 | Minimal |

## Environment Variables

All OpenCL-specific environment variables:

```bash
# Performance tuning
KEYHUNT_OPENCL_WORKGROUP_SIZE=256         # Work group size
KEYHUNT_OPENCL_KEYS_PER_ITEM=2048         # Keys per work item
KEYHUNT_OPENCL_GLOBAL_MULTIPLIER=16       # Global work size multiplier
KEYHUNT_OPENCL_AUTOTUNE=1                 # Enable auto-tuning

# Device selection
KEYHUNT_OPENCL_PLATFORM=0                 # Platform index
KEYHUNT_OPENCL_DEVICE=0                   # Device index
KEYHUNT_OPENCL_VENDOR=AMD                 # Vendor filter

# Optimization flags
KEYHUNT_OPENCL_AMD_OPTIMIZE=1             # AMD-specific optimizations
KEYHUNT_OPENCL_DEBUG=1                    # Debug output
KEYHUNT_OPENCL_PROFILE=1                  # Profiling information

# Hybrid mode
KEYHUNT_HYBRID_WORK_STEAL=1               # Work stealing
KEYHUNT_OPENCL_BLOCK_SIZE=0x100000        # Block size
```

## See Also

- [GPU Backend](GPU_BACKEND.md) - CUDA backend documentation
- [Building Guide](wiki/development/building.md) - General build instructions
- [Hybrid Mode](wiki/optimization/hybrid-mode.md) - CPU+GPU parallel mode
- [ROCm Documentation](https://rocmdocs.amd.com/) - Official AMD ROCm docs
