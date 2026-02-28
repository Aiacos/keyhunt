# Codebase Structure

**Analysis Date:** 2026-02-28

## Directory Layout

```
keyhunt/
├── src/                        # Main source code
│   ├── keyhunt.cpp             # Main orchestrator and entry point (~4300 lines)
│   ├── keyhunt_legacy.cpp       # Legacy version using GMP library (~200KB)
│   ├── bsgsd.cpp               # BSGS daemon variant standalone
│   ├── cli.h/cpp               # Command-line argument parsing
│   ├── output.h/cpp            # Colored output and logging
│   ├── progress.h/cpp          # Progress tracking and checkpointing
│   ├── benchmark.h/cpp         # Performance benchmarking module
│   ├── secure_file.h            # Secure file operations
│   │
│   ├── config/                 # Unified configuration system (NEW)
│   │   ├── config.h             # keyhunt_config_t structure
│   │   └── config.cpp           # Configuration initialization
│   │
│   ├── secp256k1/              # Elliptic curve cryptography
│   │   ├── SECP256k1.h/cpp      # Main curve class and operations
│   │   ├── Point.h/cpp          # Elliptic curve point (Jacobian)
│   │   ├── Int.h/cpp            # 256-bit big integer arithmetic
│   │   ├── IntMod.cpp           # Modular arithmetic and inversion
│   │   ├── IntMod_avx2.h         # AVX2-optimized modular operations
│   │   ├── IntGroup.h/cpp        # Batch modular inversion
│   │   └── Random.h/cpp          # Random number generation
│   │
│   ├── hash/                   # Cryptographic hash functions
│   │   ├── sha256.h/cpp         # SHA256 reference implementation
│   │   ├── sha256_sse.cpp        # SSE2 4-way SHA256
│   │   ├── sha256_avx2.cpp       # AVX2 8-way SHA256
│   │   ├── sha256_avx512.cpp     # AVX-512 16-way SHA256
│   │   ├── sha256_shani.cpp      # SHA-NI instruction variant
│   │   ├── sha512.h/cpp          # SHA512 reference
│   │   ├── sha512_avx2.cpp       # AVX2 4-way SHA512 (64-bit)
│   │   ├── sha512_avx512.cpp     # AVX-512 8-way SHA512
│   │   ├── ripemd160.h/cpp       # RIPEMD160 reference
│   │   ├── ripemd160_sse.cpp     # SSE2 4-way RIPEMD160
│   │   ├── ripemd160_avx2.cpp    # AVX2 8-way RIPEMD160 (main bottleneck)
│   │   └── ripemd160_avx512.cpp  # AVX-512 16-way RIPEMD160
│   │
│   ├── crypto/                 # Cryptographic utilities
│   │   ├── address_util.h/cpp   # Bitcoin/Ethereum address generation
│   │   └── bloom_init.h/cpp     # Bloom filter initialization
│   │
│   ├── bloom/                  # Bloom filter implementations
│   │   ├── bloom.h/cpp          # Core bloom filter
│   │   ├── bloom_wrapper.h       # C/C++ compatible wrapper
│   │   ├── bloom_simd.h/cpp      # SIMD-optimized checks
│   │   ├── bloom_fast.h          # Fast lookup variants
│   │   └── LICENSE               # Bloom filter license
│   │
│   ├── search/                 # Search algorithm implementations (6 modes)
│   │   ├── search_common.h      # Shared declarations and constants
│   │   ├── search_context.h     # Extern globals used by search modules
│   │   ├── search_utils.h       # Helper functions and profiling
│   │   ├── search_address.cpp    # ADDRESS mode: bloom filter lookup
│   │   ├── search_xpoint.h/cpp   # XPOINT mode: raw X-coordinate
│   │   ├── search_rmd160.h/cpp   # RMD160 mode: hash comparison
│   │   ├── search_bsgs.cpp       # BSGS mode: algorithm setup
│   │   ├── search_bsgs_threads.cpp  # BSGS threading
│   │   ├── search_vanity.cpp     # VANITY mode: prefix matching
│   │   └── search_minikeys.cpp   # MINIKEYS mode: minikey format
│   │
│   ├── bsgs/                   # BSGS algorithm support
│   │   ├── bsgs_ops.h/cpp       # Point operations for BSGS
│   │   ├── bsgs_sort.h/cpp       # Sorting and binary search
│   │   ├── bsgs_fast.h/cpp       # Optimized BSGS variants
│   │   └── bsgs_batched_loop.cpp # Vectorized inner loops (removed in current)
│   │
│   ├── gpu/                    # GPU acceleration (CUDA + OpenCL)
│   │   ├── gpu_backend.h        # Unified GPU API
│   │   ├── gpu_backend_cuda.cu   # NVIDIA CUDA kernels
│   │   ├── gpu_backend_opencl.c  # AMD ROCm/OpenCL backend
│   │   ├── gpu_backend_unified.c # Multi-vendor orchestration
│   │   ├── gpu_backend_none.cpp  # Stub if no GPU support
│   │   ├── gpu_multi_worker.h/c  # GPU worker thread management
│   │   ├── gpu_secp256k1.cuh     # CUDA secp256k1 kernels
│   │   ├── gpu_secp256k1_opencl.cl  # OpenCL secp256k1 kernels
│   │   ├── gpu_hash_optimized.cuh   # CUDA hash kernels
│   │   ├── gpu_hash_opencl.cl    # OpenCL hash kernels
│   │   ├── multi_gpu_scheduler.h/c  # Load balancing across GPUs
│   │   ├── cuda_check.h          # CUDA error checking macros
│   │   ├── opencl_check.h        # OpenCL error checking macros
│   │   └── async_pipeline.h/c    # Pipeline operations
│   │
│   ├── hybrid/                 # CPU+GPU hybrid execution
│   │   ├── adaptive_scheduler.h/c  # Dynamic work distribution
│   │   └── (Work distribution logic for balancing CPU/GPU load)
│   │
│   ├── distributed/            # Multi-machine coordination
│   │   ├── distributed.h/c      # Server/client protocol and implementation
│   │   └── (Work distribution across network, progress sync)
│   │
│   ├── platform/               # Cross-platform abstraction (Windows/POSIX)
│   │   ├── platform.h           # Main entry point
│   │   ├── platform_types.h     # Type definitions and platform detection
│   │   ├── platform_thread.h/c   # Thread create/join/detach
│   │   ├── platform_mutex.h/c    # Mutex operations
│   │   └── platform_time.h/c     # High-resolution monotonic time
│   │
│   ├── io/                     # Input/output operations
│   │   ├── io.h/cpp             # Target file loading, result output
│   │   └── (Address parsing, BSGS file caching)
│   │
│   ├── core/                   # System-level utilities
│   │   ├── config.h/c           # INI file configuration (legacy)
│   │   ├── util.h/c             # General utilities
│   │   ├── hashing.h/c          # Hash function dispatch
│   │   ├── sysinfo.h/c          # Hardware detection
│   │   ├── parameter_validator.h/c  # Parameter validation and auto-tuning
│   │   ├── workpool.h            # Work distribution pool
│   │   └── workqueue.h           # Thread-safe work queue
│   │
│   ├── error/                  # Error handling
│   │   ├── enhanced_error.h/c   # Error stack traces and reporting
│   │   └── (Memory leak detection, error context)
│   │
│   ├── diagnostics/            # System diagnostics
│   │   ├── diagnostics.h/c      # System health checks
│   │   └── gpu_diagnostics.h/c  # GPU-specific diagnostics
│   │
│   ├── base58/                 # Bitcoin address encoding
│   │   ├── base58.c             # Base58Check implementation
│   │   └── libbase58.h           # Base58 API
│   │
│   ├── bech32/                 # Segwit address encoding
│   │   └── (Bech32 encoding/decoding)
│   │
│   ├── sha3/                   # SHA3 and Keccak
│   │   └── sha3.h               # Keccak-256 for Ethereum
│   │
│   ├── xxhash/                 # Fast non-cryptographic hashing
│   │   └── (XXHash implementation)
│   │
│   ├── sort/                   # Sorting utilities
│   │   └── sort.h               # BSGS table sorting
│   │
│   ├── rmd160/                 # RIPEMD160 reference (legacy)
│   │   └── (Backup reference implementation)
│   │
│   ├── gmp256k1/               # GMP-based secp256k1 (legacy)
│   │   └── (Used by keyhunt_legacy.cpp only)
│   │
│   ├── database/               # Community stale cache integration
│   │   └── (Interaction with community databases)
│   │
│   ├── benchmarks/             # Benchmark utilities
│   │   └── benchmark_*.cpp      # Individual benchmark implementations
│   │
│   ├── wizard/                 # Interactive setup (NEW)
│   │   └── wizard.h/c           # 5-step distributed mode wizard
│   │
│   └── tools/                  # Development tools
│       └── (Utility scripts and test helpers)
│
├── tests/                      # Test suite
│   ├── run_tests.cpp            # Test runner
│   ├── test_*.cpp/c             # Unit and integration tests (~30 test files)
│   ├── fuzz_*.cpp               # Fuzzing tests
│   ├── data/                    # Test data files
│   ├── 1to32.txt                # Test target addresses
│   └── benchmark_results/       # Performance benchmark data
│
├── docs/                       # Documentation
│   ├── ENV_VARIABLES.md         # All KEYHUNT_* env var registry
│   ├── PARAMETER_VALIDATION.md  # Parameter validation details
│   ├── OPTIMIZATIONS.md         # SIMD optimization details
│   ├── PERFORMANCE_ANALYSIS.md  # Benchmark results
│   ├── CHANGELOG.md             # Version history
│   ├── wiki/                    # Detailed documentation
│   │   ├── getting-started/
│   │   ├── modes/
│   │   ├── optimization/
│   │   └── distributed/
│   └── plans/                   # Development planning documents
│
├── Makefile                    # Main build system
├── CMakeLists.txt              # CMake build system (alternative)
├── build_cuda.sh               # CUDA build script with auto-detection
├── build_opencl.sh             # OpenCL build script (AMD ROCm)
├── keyhunt (executable)        # Compiled binary (main)
├── keyhunt_legacy (executable) # Legacy GMP variant
├── bsgsd (executable)          # BSGS daemon variant
│
└── CLAUDE.md                   # Instructions for Claude Code
```

## Directory Purposes

**src/secp256k1/**
- Purpose: Core elliptic curve operations (Bitcoin/Ethereum compatible)
- Contains: Point arithmetic, 256-bit integer operations, curve initialization
- Key files: `SECP256k1.cpp` (main curve), `Int.cpp` (big integer), `Point.cpp` (Jacobian coordinates)

**src/hash/**
- Purpose: Cryptographic hashing with SIMD optimizations
- Contains: SHA256/SHA512/RIPEMD160 in multiple implementations (C, SSE2, AVX2, AVX-512)
- Key files: `ripemd160_avx2.cpp` (bottleneck optimization), `sha256_avx2.cpp`
- Important: RIPEMD160 is the main performance bottleneck in ADDRESS mode

**src/search/**
- Purpose: Mode-specific search implementations
- Contains: 6 separate thread entry points (one per search mode)
- Key files: `search_address.cpp` (main mode), `search_bsgs_threads.cpp` (largest file ~47KB)
- Pattern: Each file includes `search_context.h` for extern globals

**src/bloom/**
- Purpose: Probabilistic lookup for fast negative filtering
- Contains: Generic bloom filter, SIMD-accelerated checks, wrapper for C/C++
- Key files: `bloom.h` (interface), `bloom_simd.cpp` (SSE2 4-way checks)
- Used by: ADDRESS/XPOINT/RMD160 modes, BSGS amplification filters

**src/gpu/**
- Purpose: Multi-vendor GPU acceleration
- Contains: CUDA kernels, OpenCL kernels, multi-device orchestration
- Key files: `gpu_backend_cuda.cu` (~98KB), `gpu_backend_opencl.c` (~60KB), `gpu_multi_worker.c`
- Pattern: Pluggable backends with unified `gpu_backend.h` API
- Supported: NVIDIA (CUDA), AMD (ROCm), generic OpenCL

**src/platform/**
- Purpose: Cross-platform abstraction for thread/mutex/timing
- Contains: Platform-specific implementations hidden behind unified API
- Key files: `platform_thread.c`, `platform_mutex.c`, `platform_time.c`
- Pattern: Single include (`platform/platform.h`) provides all platform functionality

**src/core/**
- Purpose: System-level utilities
- Contains: Hardware detection, parameter validation, INI configuration
- Key files: `sysinfo.c` (CPU/memory detection), `parameter_validator.c` (intelligent checks)

**src/config/**
- Purpose: New unified configuration system (replaces legacy globals)
- Contains: `keyhunt_config_t` structure with nested components
- Key files: `config.h` (struct definitions), `config.cpp` (initialization)
- Status: Gradually replacing legacy globals in `search_context.h`

**src/distributed/**
- Purpose: Multi-machine coordination for puzzle solving
- Contains: Server (coordinator + local worker), client (network worker)
- Key files: `distributed.c` (~169KB, large coordinate implementation)

## Key File Locations

**Entry Points:**
- `src/keyhunt.cpp:1571`: Main entry point
- `src/keyhunt_legacy.cpp`: Legacy variant using GMP
- `src/bsgsd.cpp`: BSGS standalone daemon
- `src/benchmark.cpp`: Benchmark executable

**Configuration:**
- `src/config/config.h`: Unified configuration structure
- `src/cli.h/cpp`: Command-line parsing
- `src/core/config.c`: INI file loading (legacy)
- `docs/ENV_VARIABLES.md`: Environment variable registry

**Core Logic:**
- `src/secp256k1/SECP256k1.cpp`: Elliptic curve operations
- `src/hash/ripemd160_avx2.cpp`: Performance-critical hashing
- `src/search/search_address.cpp`: Main search mode (~44KB)
- `src/search/search_bsgs_threads.cpp`: BSGS implementation (~47KB)
- `src/bloom/bloom.cpp`: Bloom filter core

**Testing:**
- `tests/run_tests.cpp`: Test runner
- `tests/test_hash.cpp`: Hash function tests
- `tests/test_point.cpp`: Elliptic curve tests (8 failures - known issue)
- `tests/test_intgroup.cpp`: Batch inversion tests (11 failures - known issue)

## Naming Conventions

**Files:**
- Source files: `lowercase_with_underscores.cpp` or `.c`
- Headers: `lowercase_with_underscores.h`
- SIMD variants: `name_avx2.cpp`, `name_sse.cpp`, `name_avx512.cpp`
- Kernels: `gpu_*.cuh` (CUDA), `*.cl` (OpenCL)

**Directories:**
- Functionality-based: `src/hash/`, `src/search/`, `src/gpu/`
- Modules with single implementation: `src/secp256k1/`, `src/bloom/`
- Cross-cutting: `src/platform/`, `src/core/`, `src/error/`

**Functions:**
- Thread entry points: `thread_process_*()` (ADDRESS, BSGS, VANITY, etc.)
- Module initialization: `*_init()`
- Cryptographic: Verbs like `GeneratePublicKey()`, `PointMultiply()`, `GetHash160_AVX2()`
- Utilities: `acquire_base_key()`, `output_success()`, `platform_thread_create()`

**Variables:**
- Global: UPPERCASE (legacy: `FLAGMODE`, `NTHREADS`) — being migrated to config struct
- Thread-local: `g_thread_*` or `thread_local` keyword
- Constants: UPPERCASE (`CPU_GRP_SIZE`, `MODE_ADDRESS`)
- Configuration: camelCase in structs (`n_value`, `k_factor`, `enabled`)

**Types:**
- Structs: `snake_case_t` (e.g., `keyhunt_config_t`, `thread_counter`)
- Classes: PascalCase (e.g., `Secp256K1`, `Int`, `Point`)
- Enums: snake_case_t (e.g., `search_mode_t`, `gpu_mode_t`)

## Where to Add New Code

**New Search Mode:**
1. Create `src/search/search_newmode.cpp`
2. Implement `thread_process_newmode()` function
3. Add declaration to `src/search/search_common.h`
4. Add enum to `src/cli.h` (if new mode type needed)
5. Add dispatch in `keyhunt.cpp` main loop
6. Create corresponding tests in `tests/test_search_newmode.cpp`

**New Hash Function Optimization:**
1. Create `src/hash/newhash_avx2.cpp` with SIMD implementation
2. Add detection function (e.g., `newhash_avx2_available()`)
3. Update `src/core/hashing.c` with dispatch logic
4. Add Makefile rule with `-mavx2` flag
5. Test fallback to non-SIMD version
6. Benchmark: `src/benchmarks/benchmark_newhash.cpp`

**GPU Kernel Enhancement:**
1. For CUDA: Modify `src/gpu/gpu_backend_cuda.cu`
2. For OpenCL: Modify `src/gpu/gpu_secp256k1_opencl.cl` or `gpu_hash_opencl.cl`
3. Update unified backend in `src/gpu/gpu_backend_unified.c`
4. Test on both CUDA and OpenCL devices
5. Benchmark: Compare throughput before/after

**New Utility Module:**
1. Create `src/util/newutil.h/cpp`
2. Add to `src/core/util.h` includes if system-wide
3. Or add local includes in modules that need it
4. Avoid circular dependencies (use forward declarations)

**Configuration Enhancement:**
1. Add field to appropriate struct in `src/config/config.h`
2. Update `cli_args_t` in `src/cli.h` if user-configurable
3. Add parsing in `src/cli.cpp` if CLI flag needed
4. Update `cli_populate_config()` to map CLI → config struct
5. Use in modules via config parameter (dependency injection)

## Special Directories

**obj/, obj_asan/:**
- Purpose: Build output directory (created by Makefile)
- Generated: Yes
- Committed: No (in .gitignore)
- Contains: Object files, one subdir per src/ module

**tests/fuzz_corpus/, tests/benchmark_results/:**
- Purpose: Test data and benchmark results
- Generated: Yes (during test runs)
- Committed: Selectively (corpus for regression, results for documentation)

**_cuda_tmp/:**
- Purpose: CUDA intermediate files (Makefile scratch space)
- Generated: Yes
- Committed: No

**docs/plans/:**
- Purpose: Development phase planning documents
- Generated: By GSD orchestrator
- Committed: Yes (documentation of work done)

---

*Structure analysis: 2026-02-28*
