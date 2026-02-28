# Technology Stack

**Analysis Date:** 2026-02-28

## Languages

**Primary:**
- C++ (C++17) - Core application and algorithm implementations (`src/keyhunt.cpp`, `src/secp256k1/`, `src/search/`)
- C (C11) - System-level abstractions, platform layer, GPU backends (`src/platform/`, `src/gpu/`, `src/core/`)
- CUDA (NVIDIA PTX) - GPU acceleration for NVIDIA devices (`src/gpu/gpu_backend_cuda.cu`)
- OpenCL (C99) - GPU acceleration for AMD and multi-vendor (`src/gpu/gpu_backend_opencl.c`, `.cl` kernels)

**Support Languages:**
- Bash - Build scripts (`build_cuda.sh`, `build_opencl.sh`, `pgo_train.sh`)
- SQL - Database schema and initialization (`src/database/schema.sql`)

## Runtime

**Environment:**
- Native binaries (Linux x86_64 primary, Windows x64 MinGW/MSVC, macOS)
- No runtime environment required (compiled to machine code)

**Package Manager:**
- N/A (monolithic C/C++ codebase, bundled dependencies)
- SQLite3 included via amalgamation (`src/database/sqlite3.c`)

## Frameworks

**Core:**
- Custom secp256k1 elliptic curve implementation (no external crypto libs) - `src/secp256k1/`
- Custom hash implementations (RIPEMD160, SHA256, SHA512, SHA3-Keccak) - `src/hash/`, `src/sha3/`
- Custom bloom filter - `src/bloom/`
- Baby Step Giant Step (BSGS) algorithm - `src/bsgs/`

**GPU:**
- CUDA Toolkit (NVIDIA) - For GPU acceleration (auto-detected, optional)
- OpenCL 1.2+ - Multi-vendor GPU support via ROCm (AMD) or generic OpenCL
- ROCm 5.0+ (optional) - AMD GPU driver stack

**Testing:**
- Custom test framework - `tests/test_framework.h` (lightweight, no external dependencies)
- Multiple specialized test suites - `tests/test_*.cpp`

**Build/Dev:**
- Makefile (primary) - Multi-target build system with platform detection
- CMake 3.18+ (modern alternative) - Cross-platform build configuration
- NVCC (NVIDIA CUDA Compiler) - CUDA kernel compilation (optional)
- GCC 12+ / Clang 14+ - Primary C/C++ compilers

## Key Dependencies

**Critical (Vendored/Bundled):**
- SQLite3 3.x - Embedded database for performance history (`src/database/sqlite3.c`)
- Base58check - Bitcoin address encoding (`src/base58/`)
- Bech32 - SegWit address encoding (`src/bech32/`)
- RIPEMD160 - Custom implementation (not external library)
- XXHash - Fast hashing utility (`src/xxhash/`)
- Platform abstraction layer - Custom thread/mutex/time API (`src/platform/`)

**Optional (Runtime):**
- OpenSSL (libssl-dev, libcrypto-dev) - TLS support for distributed mode
  - Enabled with: `make ENABLE_TLS=1` or `cmake .. -DENABLE_TLS=ON`
  - Used by: `src/distributed/distributed.h` for encrypted coordinator communication
  - Not required for basic functionality

**System Libraries (Linked):**
- libm (math library)
- libpthread (POSIX threads)
- libdl (dynamic linking) - POSIX only
- Windows crypto API (bcrypt) - Windows only
- Winsock2 (ws2_32) - Windows networking

**Build Tools (Required):**
- GCC or Clang with C17/C++17 support
- GNU Make (for Makefile build) or CMake 3.18+ (for CMake build)
- pkg-config (for OpenCL detection)

**Build Tools (Optional):**
- CUDA Toolkit 11.0+ with compatible GCC - For NVIDIA GPU support
- ROCm 5.0+ - For AMD GPU support (auto-detected)
- lcov + genhtml - For code coverage reports
- Address Sanitizer / Thread Sanitizer - Memory safety testing
- libFuzzer or AFL - Fuzz testing

## Configuration

**Environment:**
- `KEYHUNT_*` environment variables - 13 tuning parameters documented in `docs/ENV_VARIABLES.md`
  - `KEYHUNT_PROFILE` - Enable profiling
  - `KEYHUNT_SKIP_SYSINFO` - Skip hardware auto-detection
  - `KEYHUNT_GPU_SELFTEST` - Test GPU on startup
  - `KEYHUNT_HYBRID_GPU_PERCENT` - GPU/CPU split ratio
  - `KEYHUNT_HYBRID_WORK_STEAL` - Enable work-stealing mode

**Build:**
- Makefile targets:
  - `make` - Build main keyhunt executable
  - `make legacy` - Build with OpenSSL/GMP (requires `-libssl-dev`, `-libgmp-dev`)
  - `make ENABLE_TLS=1` - Enable TLS for distributed mode
  - `make sanitize` - Build with AddressSanitizer
  - `make tsan` - Build with ThreadSanitizer
  - `make coverage` - Build with code coverage instrumentation
  - `make pgo-generate && make pgo-train && make pgo-use` - Profile-Guided Optimization
  - `make test` - Build and run unit tests
- CMakeLists.txt targets:
  - `-DENABLE_TLS=ON` - Enable OpenSSL TLS
  - `-DENABLE_LTO=OFF` - Disable Link-Time Optimization
  - `-DCMAKE_BUILD_TYPE=Debug` - Debug build
  - `-DCMAKE_CUDA_ARCHITECTURES=75` - Override GPU architecture

**Runtime Configuration:**
- `~/.keyhunt/` - User state directory
- `~/.keyhunt/progress/` - Progress tracking (JSON)
- `~/.keyhunt/performance.db` - SQLite3 performance history
- `keyhunt_wizard.json` - Wizard mode configuration (persistent)
- Distributed coordinator state: `coordinator_state_*.json` - Work distribution metadata

## Compiler Flags

**Optimization:**
- `-O2` - Primary optimization (changed from `-Ofast` to prevent Ubuntu freezes)
- `-ftree-vectorize -funroll-loops -pipe` - Vectorization and pipelining
- `-march=native -mtune=native -mssse3` - CPU-specific tuning (when not cross-compiling)
- `-flto=auto` - Link-Time Optimization enabled by default

**SIMD Optimizations (Per-File):**
- `-mavx2` - AVX2 128-bit operations (`src/hash/*_avx2.cpp`, `src/bloom/bloom_simd.cpp`)
- `-mavx512f -mavx512dq` - AVX-512 for advanced hashing (`src/hash/*_avx512.cpp`)
- `-msha -msse4.1` - Intel SHA extensions (`src/hash/sha256_shani.cpp`)

**C++ Standard:**
- `-std=gnu++17 -fno-exceptions` - C++17 with GNU extensions, no exceptions
- No RTTI or exception handling enabled

## Compilation Targets

**Main Executables:**
- `keyhunt` - Primary executable (CPU + optional GPU support)
- `keyhunt_legacy` - Legacy version using OpenSSL/GMP (requires external crypto libs)
- `bsgsd` - BSGS daemon variant
- `run_tests` - Unit test suite

**Testing & Profiling:**
- `keyhunt_pgo_gen` - PGO profiling instrumented binary
- `keyhunt_pgo` - PGO optimized binary
- `run_tests_asan` - AddressSanitizer version
- `run_tests_tsan` - ThreadSanitizer version
- `run_tests_cov` - Coverage instrumented version

## Platform Support

**Development:**
- Linux (x86_64) - Primary development target
- macOS (x86_64, Apple Silicon via Rosetta) - Supported
- Windows (x64) - MinGW-w64 or MSVC via build scripts
- Container/CI - SafeOS defaults available via `KEYHUNT_SKIP_SYSINFO=1`

**Production:**
- GPU acceleration: NVIDIA CUDA (RTX 3000/4000 series recommended) or AMD RDNA/Vega (ROCm)
- CPU: x86_64 with AVX2 preferred, SSE2 minimum
- RAM: 1GB minimum, 8GB+ recommended for BSGS
- Disk: SQLite database storage (~100MB per year of benchmarks)

**GPU Architectures Supported:**
- NVIDIA: sm_50+ (Maxwell, Pascal, Volta, Ampere, Ada, Hopper)
- AMD: gfx900/906 (Vega), gfx1010 (RDNA), gfx1030/1100 (RDNA 2/3)
- Multi-GPU: Up to 8 simultaneous devices (CUDA + OpenCL mixed)

---

*Stack analysis: 2026-02-28*
