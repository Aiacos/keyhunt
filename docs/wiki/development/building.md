# Building Guide

This document covers the keyhunt build system and compilation options.

## Quick Build

```bash
git clone https://github.com/albertobsd/keyhunt.git
cd keyhunt
make clean && make
```

## Build Targets

| Target | Command | Description |
|--------|---------|-------------|
| Standard | `make` | Main build, custom secp256k1 |
| Legacy | `make legacy` | Uses OpenSSL + GMP |
| BSGSD | `make bsgsd` | BSGS daemon variant |
| GPU | `make gpu` | With CUDA support |
| Clean | `make clean` | Remove build artifacts |

## Makefile Overview

```makefile
# Compiler settings
CXX = g++
CXXFLAGS = -O2 -march=native -flto=auto -Wall

# SIMD-specific flags
AVX2_FLAGS = -mavx2
AVX512_FLAGS = -mavx512f -mavx512dq

# Main target
keyhunt: keyhunt.o secp256k1/%.o hash/%.o bloom/%.o
    $(CXX) $(CXXFLAGS) -o $@ $^ -lpthread
```

## Compiler Requirements

### GCC

Minimum: GCC 8.0
Recommended: GCC 11+ (best AVX-512 support)

```bash
# Check version
gcc --version

# Install on Ubuntu
sudo apt install build-essential

# Install newer GCC
sudo apt install gcc-11 g++-11
```

### Clang

Minimum: Clang 10.0
Recommended: Clang 14+

```bash
# Check version
clang --version

# Install on Ubuntu
sudo apt install clang

# Use clang instead of gcc
make CXX=clang++
```

## Optimization Flags

### Default Flags

```makefile
CXXFLAGS = -O2 -march=native -flto=auto
```

| Flag | Purpose |
|------|---------|
| `-O2` | Safe optimization level |
| `-march=native` | Optimize for current CPU |
| `-flto=auto` | Link-time optimization |

### Warning About -Ofast

**DO NOT use `-Ofast`**: Causes system freezes on Ubuntu.

From the codebase:
```
# FREEZE_ISSUES_REPORT.md
# -Ofast causes undefined behavior with floating point
# Changed to -O2 in commit 20c8824
```

### Manual Optimization

For specific CPUs:

```bash
# Intel Skylake
make CXXFLAGS="-O2 -march=skylake -mtune=skylake -flto=auto"

# AMD Zen3
make CXXFLAGS="-O2 -march=znver3 -mtune=znver3 -flto=auto"
```

## SIMD Compilation

### Automatic Detection

The Makefile compiles SIMD-specific files with appropriate flags:

```makefile
hash/ripemd160_avx2.o: hash/ripemd160_avx2.cpp
    $(CXX) $(CXXFLAGS) $(AVX2_FLAGS) -c $< -o $@

hash/ripemd160_avx512.o: hash/ripemd160_avx512.cpp
    $(CXX) $(CXXFLAGS) $(AVX512_FLAGS) -c $< -o $@
```

### Disable SIMD

Force scalar-only build:

```bash
make CXXFLAGS="-O2 -DDISABLE_AVX2 -DDISABLE_AVX512"
```

## Legacy Build

Requires external libraries:

```bash
# Install dependencies
sudo apt install libssl-dev libgmp-dev

# Build
make legacy
```

### Legacy vs Standard

| Aspect | Standard | Legacy |
|--------|----------|--------|
| Dependencies | None | OpenSSL, GMP |
| secp256k1 | Custom | GMP-based |
| Performance | Slightly faster | Slightly slower |
| Portability | High | Requires libs |

## GPU Build (CUDA)

### Prerequisites

1. **NVIDIA GPU** with Compute Capability 5.0+ (Maxwell or newer)
2. **CUDA Toolkit** 11.0 or later (12.x recommended)
3. **NVIDIA Driver** 450.0+ (525+ recommended)

Install CUDA:

```bash
# Ubuntu/Debian
sudo apt install nvidia-cuda-toolkit

# Fedora
sudo dnf install cuda-toolkit

# Or from NVIDIA directly
# https://developer.nvidia.com/cuda-downloads
```

### Automatic Build (Recommended)

Use the `build_cuda.sh` script which handles everything:

```bash
./build_cuda.sh
```

This script:
- Auto-detects CUDA installation location
- Auto-detects your GPU architecture
- Finds a compatible GCC version (or uses flags for newer GCC)
- Builds with optimal settings
- Verifies the build succeeded

Options:
```bash
./build_cuda.sh --help              # Show all options
./build_cuda.sh --arch sm_86        # Specify GPU architecture
./build_cuda.sh --cuda-home /path   # Specify CUDA path
./build_cuda.sh --list-arch         # List GPU architectures
```

### Manual Build with CUDA

```bash
make clean
make NVCC=/usr/local/cuda/bin/nvcc \
     CUDA_HOME=/usr/local/cuda \
     NVCCFLAGS='-O3 -std=c++17 -arch=sm_75 -allow-unsupported-compiler'
```

### GCC Compatibility

CUDA has host compiler version requirements:

| CUDA Version | Max Supported GCC |
|--------------|-------------------|
| CUDA 12.6    | GCC 13 (14 with flag) |
| CUDA 12.4    | GCC 13 |
| CUDA 11.8    | GCC 11 |

For newer GCC (14+), the `-allow-unsupported-compiler` flag is added automatically.

If you have GCC 15+ and want better compatibility:

```bash
# Install GCC 13
brew install gcc@13          # Homebrew (any Linux)
sudo apt install gcc-13      # Ubuntu/Debian
sudo dnf install gcc13       # Fedora

# build_cuda.sh will use it automatically
./build_cuda.sh
```

### GPU Build Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `NVCC` | Path to nvcc compiler | Auto-detect |
| `CUDA_HOME` | CUDA toolkit directory | `/usr/local/cuda` |
| `CUDA_ARCH` | GPU architecture | Auto-detect from GPU |
| `NVCCFLAGS` | nvcc compiler flags | `-O3 -std=c++17 -arch=$(CUDA_ARCH)` |
| `CUDA_CC_BINDIR` | Host compiler directory | None |

### GPU Architecture Reference

| Architecture | GPUs | Code |
|--------------|------|------|
| Maxwell | GTX 900 series | `sm_50` |
| Pascal | GTX 1000 series | `sm_60`/`sm_61` |
| Volta | V100, Titan V | `sm_70` |
| Turing | RTX 2000 series, GTX 1660 | `sm_75` |
| Ampere | RTX 3000 series | `sm_86` |
| Ampere | A100 | `sm_80` |
| Ada | RTX 4000 series | `sm_89` |
| Hopper | H100 | `sm_90` |

### Verify CUDA Build

```bash
# Check CUDA is linked
ldd keyhunt | grep cuda
# Should show: libcudart.so.12 => /usr/local/cuda/lib64/libcudart.so.12

# Test GPU detection
./keyhunt -m address -f tests/1to32.txt -r 1:FF -G auto
# Should show GPU info in output
```

### Build Without CUDA

If you don't have CUDA or want CPU-only:

```bash
make clean
make
```

The Makefile auto-detects nvcc and builds without GPU support if unavailable.

## Debug Build

For debugging with symbols:

```bash
make CXXFLAGS="-O0 -g -DDEBUG"
```

### With AddressSanitizer

```bash
make CXXFLAGS="-O1 -g -fsanitize=address -fno-omit-frame-pointer"
```

### With Valgrind

```bash
make CXXFLAGS="-O1 -g"
valgrind --leak-check=full ./keyhunt ...
```

## Profile Build

For profiling with gprof:

```bash
make CXXFLAGS="-O2 -pg"
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF
gprof keyhunt gmon.out > analysis.txt
```

For perf profiling:

```bash
make CXXFLAGS="-O2 -g -fno-omit-frame-pointer"
perf record ./keyhunt ...
perf report
```

## Cross-Compilation

### For older CPUs

```bash
# Without AVX2 (pre-2013 CPUs)
make CXXFLAGS="-O2 -march=x86-64 -msse2"
```

### Static Build

```bash
make LDFLAGS="-static -lpthread"
```

Note: Static builds are larger but more portable.

## Build Verification

### Check Build

```bash
# Verify executable
file keyhunt
# Expected: ELF 64-bit LSB executable, x86-64, dynamically linked

# Check dependencies
ldd keyhunt
# Should show libpthread only (for standard build)

# Check symbols
nm keyhunt | grep -i avx
# Should show AVX-related symbols if compiled with AVX support
```

### Quick Test

```bash
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF
# Should find 32 keys quickly
```

## Parallel Compilation

Speed up builds on multi-core systems:

```bash
make -j$(nproc)
```

## Incremental Builds

Only recompile changed files:

```bash
# First build
make

# After changing a file
make  # Only recompiles affected files

# Full rebuild
make clean && make
```

## Build Output

Successful build produces:

```
keyhunt              # Main executable
*.o                  # Object files (can be cleaned)
```

## Troubleshooting

### "command not found: g++"

Install build tools:
```bash
sudo apt install build-essential
```

### "cannot find -lpthread"

Install pthread development files:
```bash
sudo apt install libc6-dev
```

### "undefined reference" errors

Usually missing `-l` flag. Check library dependencies.

### AVX2/AVX-512 errors

CPU doesn't support the instruction set. Build without:
```bash
make CXXFLAGS="-O2 -msse2"
```

### Link-time optimization failures

Disable LTO:
```bash
make CXXFLAGS="-O2 -march=native"
```

## See Also

- [Architecture](architecture.md) - Codebase structure
- [Testing](testing.md) - Running tests
- [Installation](../getting-started/installation.md) - User installation guide
