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

## GPU Build

### Prerequisites

```bash
# Install CUDA Toolkit
sudo apt install nvidia-cuda-toolkit

# Or from NVIDIA
wget https://developer.download.nvidia.com/compute/cuda/repos/...
```

### Build with CUDA

```bash
make gpu CUDA_PATH=/usr/local/cuda
```

### GPU Build Variables

```makefile
CUDA_PATH = /usr/local/cuda
NVCC = $(CUDA_PATH)/bin/nvcc
CUDA_FLAGS = -arch=sm_50 -O2
```

Adjust `sm_50` for your GPU architecture:
- sm_50: Maxwell (GTX 900)
- sm_60: Pascal (GTX 1000)
- sm_70: Volta (V100)
- sm_75: Turing (RTX 2000)
- sm_80: Ampere (RTX 3000)
- sm_90: Hopper (H100)

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
