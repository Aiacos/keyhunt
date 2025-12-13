# GPU Backend Documentation

## Overview

Keyhunt supports optional CUDA GPU acceleration for hash computation. The GPU backend is auto-detected at compile time if `nvcc` is available.

## Build Requirements

### CUDA Toolkit
- CUDA 12.x or later
- Compatible NVIDIA GPU (Compute Capability 5.0+)

### Compiler Compatibility
CUDA has strict host compiler requirements. On systems with newer GCC (15+), use an older GCC as host compiler:

```bash
# Install GCC 13 via Homebrew (Fedora/newer distros)
brew install gcc@13

# Create symlinks for nvcc
mkdir -p /tmp/ccbin-gcc13
ln -sf $(brew --prefix)/bin/gcc-13 /tmp/ccbin-gcc13/gcc
ln -sf $(brew --prefix)/bin/g++-13 /tmp/ccbin-gcc13/g++
```

### Build Commands

**With CUDA (custom path):**
```bash
make clean
make NVCC=/path/to/cuda/bin/nvcc \
     CUDA_HOME=/path/to/cuda \
     NVCCFLAGS='-O3 -std=c++17 -arch=sm_75 -allow-unsupported-compiler --compiler-bindir=/tmp/ccbin-gcc13 -Xcompiler -U_GNU_SOURCE'
```

**Without CUDA:**
```bash
make clean
make
```

The Makefile auto-detects nvcc and builds without GPU support if unavailable.

## GPU Modes

Use the `-G` flag to control GPU usage:

| Flag | Description |
|------|-------------|
| `-G off` | Disable GPU, use CPU only |
| `-G auto` | Auto-detect best mode (default) |
| `-G hash` | GPU computes SHA256+RIPEMD160 only |
| `-G full` | Full GPU search (ECC + hash + matching) |
| `-G hybrid` | GPU+CPU in parallel (static range split) |

### Hash-Only Mode (`-G hash`)
- CPU generates elliptic curve points
- GPU computes HASH160 (SHA256 + RIPEMD160)
- Best for: Systems where CPU ECC is faster than GPU

### Full GPU Mode (`-G full`)
- GPU performs all operations: ECC point generation, hashing, and target matching
- Best for: Large search ranges, many targets
- Supports multi-stream execution and GPU-side bloom/target matching

#### Optional runtime tuning
You can tune kernel launch parameters at runtime (no rebuild needed):
- `KEYHUNT_GPU_BLOCKS_PER_SM=4..64`
- `KEYHUNT_GPU_KEYS_PER_THREAD=64..65536`
- `KEYHUNT_GPU_AUTOTUNE=1` (tries a small safe set and selects the best; disabled if either value above is explicitly set)

## Supported Search Modes

| Mode | GPU Hash | GPU Full |
|------|----------|----------|
| `rmd160` | Yes | Yes |
| `address` | No | Yes |
| `xpoint` | No | Planned |
| `bsgs` | No | No |

## Performance

Benchmark on RTX 2080 SUPER (puzzle 71, compress mode):

| Mode | Speed |
|------|-------|
| CPU (AVX2, 16 threads) | ~50 Mkeys/s |
| GPU Full (`-G full`) | ~570–600 Mkeys/s |
| GPU Hybrid (`-G hybrid`) | CPU+GPU combined |

The hash-only mode can be slower due to CPU↔GPU transfer overhead. Full mode avoids this by keeping ECC+hash+matching on the GPU.

## Troubleshooting

### "CUDA backend not available"
- Check nvcc is in PATH: `which nvcc`
- Rebuild with explicit NVCC path

### Compilation errors with newer GCC
- Use `--compiler-bindir` to specify GCC 13/14
- Add `-Xcompiler -U_GNU_SOURCE` to avoid glibc conflicts

### Runtime errors
- Check CUDA driver: `nvidia-smi`
- Verify library path: `ldd keyhunt | grep cuda`

## Architecture

```
gpu/
├── gpu_backend.h          # C API header
├── gpu_backend_cuda.cu    # CUDA implementation
├── gpu_backend_none.cpp   # Stub for non-CUDA builds
└── gpu_secp256k1.cuh      # GPU ECC operations (for full mode)
```

The backend uses a C interface for compatibility:
- `gpu_backend_init()` - Initialize CUDA, detect GPU
- `gpu_hash160_fromX_batch()` - Batch hash computation
- `gpu_full_search()` - Full GPU search (implemented)
