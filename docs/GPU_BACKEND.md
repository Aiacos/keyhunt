# GPU Backend Documentation

## Overview

Keyhunt supports optional CUDA GPU acceleration for cryptographic operations. The GPU backend is auto-detected at compile time if `nvcc` is available.

## Build Requirements

### CUDA Toolkit
- **Supported versions**: CUDA 11.0 - 12.x
- **Required**: NVIDIA GPU with Compute Capability 5.0+ (Maxwell or newer)

### GCC Compatibility

CUDA has strict host compiler requirements:

| CUDA Version | Max Supported GCC |
|--------------|-------------------|
| CUDA 12.6    | GCC 13 (14 with flag) |
| CUDA 12.4    | GCC 13 (14 with flag) |
| CUDA 12.0    | GCC 12 |
| CUDA 11.8    | GCC 11 |

For newer GCC versions (14+), use the `-allow-unsupported-compiler` flag (added automatically by `build_cuda.sh`).

## Quick Build

### Automatic Build (Recommended)

The `build_cuda.sh` script handles everything automatically:

```bash
./build_cuda.sh
```

This script:
1. Detects CUDA installation location
2. Auto-detects your GPU architecture
3. Finds a compatible GCC version (or uses `-allow-unsupported-compiler`)
4. Builds with optimal settings

### Manual Build

For manual builds with specific settings:

```bash
# Clean previous build
make clean

# Build with CUDA
make NVCC=/usr/local/cuda/bin/nvcc \
     CUDA_HOME=/usr/local/cuda \
     NVCCFLAGS='-O3 -std=c++17 -arch=sm_75 -allow-unsupported-compiler'
```

### Build with Older GCC (GCC 15+ systems)

On systems with GCC 15+, install an older GCC:

```bash
# Install GCC 13 via Homebrew (works on most Linux distros)
brew install gcc@13

# Create symlinks for nvcc
mkdir -p /tmp/ccbin-gcc13
ln -sf $(brew --prefix)/bin/gcc-13 /tmp/ccbin-gcc13/gcc
ln -sf $(brew --prefix)/bin/g++-13 /tmp/ccbin-gcc13/g++

# Build with CUDA
make clean
make NVCC=/usr/local/cuda/bin/nvcc \
     CUDA_HOME=/usr/local/cuda \
     NVCCFLAGS='-O3 -std=c++17 -arch=sm_75 -allow-unsupported-compiler --compiler-bindir=/tmp/ccbin-gcc13 -Xcompiler -U_GNU_SOURCE'
```

Or simply use the build script which handles this automatically:

```bash
./build_cuda.sh
```

### Build Without CUDA

To build CPU-only version:

```bash
make clean
make
```

The Makefile auto-detects nvcc and builds without GPU support if unavailable.

## GPU Architecture Selection

Choose the architecture matching your GPU:

| Architecture | GPUs | Flag |
|--------------|------|------|
| Maxwell | GTX 900 series | `sm_50` |
| Pascal | GTX 1000 series, P100 | `sm_60` / `sm_61` |
| Volta | V100, Titan V | `sm_70` |
| Turing | RTX 2000 series, GTX 1660 | `sm_75` |
| Ampere | RTX 3000 series | `sm_86` |
| Ampere | A100 | `sm_80` |
| Ada Lovelace | RTX 4000 series | `sm_89` |
| Hopper | H100 | `sm_90` |

Use `--arch` flag or auto-detect:

```bash
# Auto-detect (recommended)
./build_cuda.sh

# Specify architecture
./build_cuda.sh --arch sm_86    # RTX 3000
./build_cuda.sh --arch sm_89    # RTX 4000
```

## GPU Modes

Use the `-G` flag to control GPU usage:

| Flag | Description | CPU Load | GPU Load |
|------|-------------|----------|----------|
| `-G off` | Disable GPU, use CPU only | 100% | 0% |
| `-G auto` | Auto-detect best mode (default) | Varies | Varies |
| `-G hash` | GPU computes SHA256+RIPEMD160 only | High | Medium |
| `-G full` | Full GPU search (ECC + hash + matching) | Low | 100% |
| `-G hybrid` | GPU+CPU in parallel | 100% | 100% |

### Hash-Only Mode (`-G hash`)
- CPU generates elliptic curve points
- GPU computes HASH160 (SHA256 + RIPEMD160)
- Best for: Systems where CPU ECC is faster than GPU

### Full GPU Mode (`-G full`)
- GPU performs all operations: ECC point generation, hashing, and target matching
- Best for: Large search ranges, many targets
- Supports multi-stream execution and GPU-side bloom/target matching

### Hybrid Mode (`-G hybrid`)
Hybrid runs GPU FULL and CPU in parallel.

Optional work-stealing (dynamic load balancing):
- `KEYHUNT_HYBRID_WORK_STEAL=1`
- Requires non-random mode and stride=1
- `KEYHUNT_HYBRID_BLOCK_SIZE=...` (keys per block; accepts decimal or `0x...`)

## Runtime Tuning

You can tune kernel launch parameters at runtime (no rebuild needed):

```bash
# Set blocks per SM (4-64)
export KEYHUNT_GPU_BLOCKS_PER_SM=32

# Set keys per thread (64-65536)
export KEYHUNT_GPU_KEYS_PER_THREAD=2048

# Enable auto-tuning (finds optimal params automatically)
export KEYHUNT_GPU_AUTOTUNE=1
```

## Supported Search Modes

| Mode | GPU Hash | GPU Full |
|------|----------|----------|
| `rmd160` | Yes | Yes |
| `address` | No | Yes |
| `xpoint` | No | Planned |
| `bsgs` | No | No |

Notes:
- `GPU Full` supports `-l compress`, `-l uncompress`, and `-l both` for BTC.
- `-l both` is slower than `-l compress` because it computes and checks both pubkey encodings.
- `GPU Hash` mode supports only compressed pubkeys (`-l compress`).

## Performance

Benchmark on RTX 2080 SUPER (puzzle 71, compress mode):

| Mode | Speed |
|------|-------|
| CPU (AVX2, 16 threads) | ~50 Mkeys/s |
| GPU Full (`-G full`) | ~570-600 Mkeys/s |
| GPU Hybrid (`-G hybrid`) | ~650+ Mkeys/s |

Run your own benchmark:
```bash
./keyhunt --benchmark
```

## Multi-GPU Support

### List Available GPUs

```bash
nvidia-smi -L
```

### Select Specific GPU

```bash
CUDA_VISIBLE_DEVICES=0 ./keyhunt -m address -f targets.txt -b 66 -G full
CUDA_VISIBLE_DEVICES=1 ./keyhunt -m address -f targets.txt -b 66 -G full
```

### Use Multiple GPUs

Run multiple instances or use distributed mode:

```bash
# Terminal 1 - GPU 0
CUDA_VISIBLE_DEVICES=0 ./keyhunt -m address -f targets.txt -b 66 -G full

# Terminal 2 - GPU 1
CUDA_VISIBLE_DEVICES=1 ./keyhunt -m address -f targets.txt -b 66 -G full
```

## Troubleshooting

### "CUDA backend not available"

1. Check nvcc is installed:
   ```bash
   which nvcc
   nvcc --version
   ```

2. Check CUDA path:
   ```bash
   ls /usr/local/cuda
   ```

3. Rebuild with explicit NVCC path:
   ```bash
   ./build_cuda.sh --cuda-home /usr/local/cuda-12.6
   ```

### Compilation errors with newer GCC

Use `--compiler-bindir` or let `build_cuda.sh` handle it:

```bash
# Automatic (recommended)
./build_cuda.sh

# Manual
make NVCCFLAGS='-O3 -std=c++17 -arch=sm_75 -allow-unsupported-compiler --compiler-bindir=/tmp/ccbin-gcc13'
```

### Runtime errors

1. Check CUDA driver:
   ```bash
   nvidia-smi
   ```

2. Verify library path:
   ```bash
   ldd keyhunt | grep cuda
   ```

3. Set library path if needed:
   ```bash
   export LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH
   ```

### "GPU compute capability too low"

Your GPU is too old. Minimum: Compute Capability 5.0 (Maxwell)

Check your GPU:
```bash
nvidia-smi --query-gpu=compute_cap --format=csv
```

### Slow GPU Performance

1. Check GPU utilization:
   ```bash
   nvidia-smi dmon -s u
   ```

2. Try different modes:
   ```bash
   ./keyhunt -m address -f targets.txt -b 66 -G full    # Full GPU
   ./keyhunt -m address -f targets.txt -b 66 -G hybrid  # CPU+GPU
   ```

3. Enable auto-tuning:
   ```bash
   KEYHUNT_GPU_AUTOTUNE=1 ./keyhunt -m address -f targets.txt -b 66 -G full
   ```

## Architecture

```
src/gpu/
├── gpu_backend.h          # C API header
├── gpu_backend_cuda.cu    # CUDA implementation (ECC + hashing + matching)
├── gpu_backend_none.cpp   # Stub for non-CUDA builds
├── gpu_hash_optimized.cuh # Optimized SHA256/RIPEMD160 kernels
├── gpu_autotune.c         # Auto-tuning logic
├── multi_gpu_scheduler.c  # Multi-GPU scheduling
└── async_pipeline.c       # Async execution pipeline
```

The backend uses a C interface for compatibility:
- `gpu_backend_init()` - Initialize CUDA, detect GPU
- `gpu_upload_gtable()` - Upload precomputed G table
- `gpu_upload_targets()` - Upload target hashes
- `gpu_full_search()` - Full GPU search (ECC + hash + matching)
- `gpu_benchmark()` - Benchmark GPU performance

## See Also

- [GPU Setup Guide](wiki/optimization/gpu-setup.md) - Detailed GPU configuration
- [Building Guide](wiki/development/building.md) - General build instructions
- [Hybrid Mode](wiki/optimization/hybrid-mode.md) - CPU+GPU parallel mode
