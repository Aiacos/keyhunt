# Architecture

**Analysis Date:** 2026-02-28

## Pattern Overview

**Overall:** Modular monolithic architecture with layered design

**Key Characteristics:**
- Main orchestrator in `src/keyhunt.cpp` (~4300 lines) dispatches to specialized search modules
- Pluggable search backends: 6 distinct modes (ADDRESS, BSGS, XPOINT, RMD160, VANITY, MINIKEYS)
- Structured configuration system (`src/config/config.h`) replacing legacy global variables
- SIMD-optimized cryptographic pipeline (SHA256, RIPEMD160, secp256k1)
- GPU acceleration backends (CUDA + OpenCL) with multi-device support
- Hybrid CPU+GPU execution with adaptive work scheduling
- Distributed mode for multi-machine coordination

## Layers

**Presentation Layer:**
- Purpose: Command-line parsing, help/wizard, output formatting, progress tracking
- Location: `src/cli.cpp`, `src/cli.h`, `src/output.cpp`, `src/output.h`, `src/progress.cpp`, `src/wizard/`
- Contains: Argument validation, output control (colored/quiet/verbose), progress serialization
- Depends on: Configuration structures (config.h)
- Used by: Main orchestrator (keyhunt.cpp), entry points

**Configuration & Initialization Layer:**
- Purpose: Unified parameter management, hardware detection, auto-tuning, parameter validation
- Location: `src/config/config.h`, `src/config/config.cpp`, `src/core/sysinfo.c`, `src/core/parameter_validator.c`
- Contains: Configuration structures (search, BSGS, GPU, runtime state), CPU/memory detection, intelligent parameter validation
- Depends on: Platform abstraction layer
- Used by: All modules that need configuration, hardware info

**Cryptographic Layer:**
- Purpose: Elliptic curve operations, hash functions (SHA256, RIPEMD160, SHA512), address generation
- Location: `src/secp256k1/`, `src/hash/`, `src/crypto/`, `src/sha3/`
- Contains:
  - **secp256k1/**: Point, Int (256-bit), IntGroup, SECP256K1 class (curve operations)
  - **hash/**: Reference and SIMD implementations (SSE2/AVX2/AVX-512) for SHA256/RIPEMD160/SHA512
  - **crypto/**: Address encoding (pubkey→Bitcoin/Ethereum address), bloom initialization
- Depends on: Nothing
- Used by: All search modules, CPU key generation threads

**Search Algorithms Layer:**
- Purpose: Implement 6 distinct search modes with thread dispatchers
- Location: `src/search/`
  - `search_address.cpp`: ADDRESS mode (bloom filter lookup)
  - `search_bsgs.cpp`, `search_bsgs_threads.cpp`: BSGS mode (baby step giant step with memory optimization)
  - `search_xpoint.cpp`: XPOINT mode (raw X-coordinate comparison)
  - `search_rmd160.cpp`: RMD160 mode (direct RIPEMD160 hash comparison)
  - `search_vanity.cpp`: VANITY mode (prefix matching)
  - `search_minikeys.cpp`: MINIKEYS mode (minikey format search)
- Contains: Thread entry points, search loop logic, target matching
- Depends on: Cryptographic layer, bloom filters, configuration structures
- Used by: Main orchestrator for CPU threading

**Bloom Filter Layer:**
- Purpose: Fast negative lookup for ADDRESS/XPOINT/RMD160 modes, BSGS amplification bloom filters
- Location: `src/bloom/`
- Contains: Generic bloom filter (bloom.h), SIMD-optimized checks (bloom_simd.cpp), wrapper for both C and C++ (bloom_wrapper.h)
- Depends on: Nothing
- Used by: Search address, search bsgs, crypto bloom initialization

**GPU Acceleration Layer:**
- Purpose: Multi-vendor GPU support (NVIDIA CUDA + AMD OpenCL) with multi-device load balancing
- Location: `src/gpu/`
- Contains:
  - `gpu_backend.h`: Unified GPU backend API (init, enqueue, fetch results)
  - `gpu_backend_cuda.cu`: NVIDIA CUDA kernels and host code
  - `gpu_backend_opencl.c`: AMD ROCm and generic OpenCL support
  - `gpu_backend_unified.c`: Multi-device scheduler and platform abstraction
  - `gpu_multi_worker.c`: Worker threads for GPU batch processing
  - `gpu_secp256k1_opencl.cl`, `gpu_hash_opencl.cl`: OpenCL kernels
- Depends on: CUDA toolkit (optional), OpenCL runtime (optional), configuration
- Used by: Main orchestrator for GPU search (ADDRESS/XPOINT modes only)

**Hybrid Execution Layer:**
- Purpose: Adaptive CPU+GPU work distribution based on relative performance
- Location: `src/hybrid/adaptive_scheduler.h`, `src/hybrid/adaptive_scheduler.c`
- Contains: Work distribution logic, range splitting, dynamic load balancing
- Depends on: GPU backend, configuration
- Used by: Main orchestrator for hybrid mode (-G hybrid)

**Distributed Mode Layer:**
- Purpose: Multi-machine coordination via network protocol
- Location: `src/distributed/distributed.c`, `src/distributed/distributed.h`
- Contains: Server (coordinator), client (worker), work distribution, progress synchronization, webhook integration
- Depends on: Configuration, I/O (file writing for keys found)
- Used by: Main orchestrator for distributed/wizard modes

**I/O & Data Management Layer:**
- Purpose: Target file loading, results output, progress persistence, precalculated file caching
- Location: `src/io/io.cpp`, `src/io/io.h`
- Contains: Target parsing (addresses/hashes/public keys), result writing, BSGS file caching (bloom/bP tables)
- Depends on: Cryptographic functions, base58/bech32 encoding
- Used by: Main orchestrator, search modules

**Platform Abstraction Layer:**
- Purpose: Cross-platform (Windows 64-bit + POSIX) thread and synchronization primitives
- Location: `src/platform/`
- Contains: Thread creation/join, mutexes, high-resolution timing
- Depends on: Native OS APIs only (Windows API, pthreads, clock_gettime)
- Used by: All modules needing threads or synchronization

**Error Handling & Diagnostics:**
- Purpose: Error reporting, system diagnostics, GPU health checks
- Location: `src/error/enhanced_error.c`, `src/diagnostics/diagnostics.c`
- Contains: Error stack traces, memory leak detection, GPU diagnostics
- Depends on: Platform layer
- Used by: All modules for error reporting

**Utilities:**
- Purpose: Helper functions, data structures, constants
- Location: `src/core/util.c`, `src/base58/`, `src/bech32/`, `src/xxhash/`, `src/sort/`
- Contains: Base58/Bech32 encoding, hashing utilities, sorting (bsgs_sort.h), xxHash
- Depends on: Nothing
- Used by: Cryptographic and I/O layers

## Data Flow

**ADDRESS Mode (Main Search Path):**

1. **Initialization** (main → keyhunt.cpp:1571):
   - Parse CLI arguments (`cli_parse()`)
   - Initialize configuration structure
   - Detect hardware and auto-tune parameters
   - Load target file → parse addresses → build bloom filter (`crypto/bloom_init.cpp`)
   - Initialize secp256K1 curve and generator points

2. **Key Generation & Checking** (thread_process):
   - Acquire base key (Int) from work queue
   - Generate sequential keys (base → base+stride)
   - For each key:
     - Compute public key (secp→Point)
     - Hash to RIPEMD160 (GetHash160_AVX2 or GetHash160)
     - Check bloom filter (address_utils → bloom_wrapper)
     - On positive match: check full address against sorted target array (binary search)
     - On match found: write result and exit

3. **GPU Path** (if -G enabled):
   - Load target bloom filter to GPU memory
   - Dispatch key ranges to GPU(s)
   - GPU kernels compute hashes, check locally cached bloom
   - Transfer results back to CPU for verification

4. **Results & Output**:
   - Write found keys to stdout/file
   - Update progress checkpoint every 60 seconds
   - Display statistics (keys/sec, elapsed time)

**BSGS Mode (Baby Step Giant Step):**

1. **Pre-computation Phase**:
   - Parse public key target(s)
   - Calculate N, K, M (baby/giant step sizes)
   - Validate memory requirements (keyhunt.cpp:1637-1716)
   - Generate and store "baby step" points (bP table)
   - Build bloom filters (3-tier hierarchy for collision reduction)
   - Optional: Save to disk for reuse (-S flag)

2. **Giant Step Phase**:
   - Batched giant step computation (BSGS_MP, BSGS_MP2, BSGS_MP3)
   - Check each giant step point against bloom filters
   - On bloom positive: perform secondary/tertiary checks (Integer arithmetic)
   - On match: compute private key = base + (k * M) + (i * BSGS_P)

3. **Parallelization**:
   - Threads divided into two groups:
     - bP table generation threads (pre-computation phase)
     - Giant step check threads (search phase)

**Vanity Mode:**

1. Load vanity patterns → build pattern matching tables (RMD160 prefixes)
2. Generate keys and hash to RIPEMD160
3. Match first N bytes against pattern table
4. On match: verify full address prefix

**Minikey Mode:**

1. Parse base minikey template
2. Increment through minikey space
3. Validate minikey checksum (SHA256)
4. Convert to private key and search

## State Management

**Runtime State:**
- `keyhunt_config_t` in `src/config/config.h` contains mutable state during execution
  - `runtime_state_t`: Thread counts, finished item counters, mutable flags
  - Search globals still in `search_context.h` (legacy; under migration)

**Thread-Safe Counters:**
- `steps[i]`: Per-thread key counter (cache-line padded)
- `ends[i]`: Per-thread finished flag (cache-line padded)
- Atomic `FINISHED_ITEMS` for aggregated progress

**Global Synchronization:**
- `write_keys`: Mutex for coordinating found key output
- `write_random`: Mutex for thread-safe random state (legacy; thread_local now used)
- `bsgs_thread`: Mutex for BSGS phase coordination
- `bloom_bP_mutex[]`: Per-device mutexes for bloom filter access (BSGS)

## Key Abstractions

**Secp256K1 Elliptic Curve:**
- Purpose: All elliptic curve operations on Bitcoin/Ethereum curves
- Examples: `src/secp256k1/SECP256k1.cpp`, `SECP256k1.h`
- Pattern: Jacobian coordinates for point operations, Montgomery multiplication for efficiency
- Key methods: `GetHash160_AVX2()`, `GeneratePublicKey()`, `PointMultiply()`

**Int (256-bit Big Integer):**
- Purpose: Arbitrary precision arithmetic modulo curve order
- Examples: `src/secp256k1/Int.cpp`, `Int.h`
- Pattern: Binary representation (8x 32-bit or 4x 64-bit), modular reduction using Barrett's algorithm
- Key methods: `SetBase16()`, `Add()`, `Multiply()`, `ModInv()`

**IntGroup (Batch Modular Inversion):**
- Purpose: Optimize multiple ModInv operations using Montgomery's trick (N-1 multiplies + 1 invert)
- Examples: `src/secp256k1/IntGroup.cpp`
- Pattern: Reduces algorithm complexity from O(N*log M) to O(N + log M)

**Point (Elliptic Curve Point):**
- Purpose: Jacobian representation of curve points (X, Y, Z)
- Examples: `src/secp256k1/Point.cpp`
- Pattern: Stores as (X, Y, Z) rather than (x, y) for efficient doubling/addition
- Key methods: `Double()`, `Add()`

**Bloom Filter (Extended Version):**
- Purpose: Fast probabilistic negative lookup
- Examples: `src/bloom/bloom.h`, `bloom_simd.cpp`
- Pattern: Hash multiple independent functions, check bits in parallel (SIMD in bloom_simd.cpp)
- Three versions: `bloom_extended_t` (main), secondary (5%), tertiary (0.25%) for BSGS

**Configuration Structure:**
- Purpose: Unified parameter container replacing 50+ global variables
- Examples: `src/config/config.h` (`keyhunt_config_t`)
- Pattern: Nested structs (search_config_t, bsgs_config_t, gpu_config_t, runtime_state_t)
- Used by: All modules via dependency injection (passed as parameter)

## Entry Points

**keyhunt.cpp main():**
- Location: `src/keyhunt.cpp:1571`
- Triggers: Command-line invocation
- Responsibilities:
  1. Parse command-line arguments
  2. Initialize configuration and detect hardware
  3. Load targets and initialize search structures
  4. Dispatch to appropriate search mode
  5. Manage thread lifecycle and progress tracking
  6. Handle signals (SIGINT for graceful shutdown)
  7. Report results and cleanup

**Search Mode Dispatchers:**
- `thread_process()`: ADDRESS/XPOINT/RMD160 modes
- `thread_process_bsgs()`: BSGS sequential
- `thread_process_bsgs_backward()`: BSGS backward (from end of range)
- `thread_process_bsgs_both()`: BSGS both directions
- `thread_process_bsgs_random()`: BSGS random order
- `thread_process_bsgs_dance()`: BSGS choreographed pattern
- `thread_process_vanity()`: VANITY mode
- `thread_process_minikeys()`: MINIKEYS mode

**GPU Execution:**
- `gpu_backend_init()`: Initialize CUDA/OpenCL runtime
- `gpu_worker_enqueue()`: Submit work batches to GPU
- `gpu_worker_fetch_results()`: Retrieve completed results

**Distributed/Wizard:**
- `wizard_run()`: Interactive setup for distributed mode
- `distributed_server_start()`: Coordinator for multi-client work distribution
- `distributed_client_connect()`: Worker node connecting to coordinator

## Error Handling

**Strategy:** Layered error detection with graceful degradation

**Patterns:**
- **Memory allocation errors**: Check NULL returns, output_error(), exit(EXIT_FAILURE)
- **File I/O errors**: Validate fopen/fread/fwrite returns, verify checksums for critical files
- **CUDA errors**: Enhanced error checking in `gpu/cuda_check.h` (cudaGetLastError, kernel synchronization)
- **OpenCL errors**: Macro-based error code translation in `gpu/opencl_check.h`
- **Parameter validation**: `core/parameter_validator.c` with auto-correction (scales N/K if OOM risk)
- **Configuration errors**: Early validation in `cli_validate()` before main loop

## Cross-Cutting Concerns

**Logging:**
- Module: `src/output.h/cpp`
- Approach: Verbosity levels (SILENT, MINIMAL, NORMAL, VERBOSE) with color codes
- Functions: `output_success()` (green), `output_info()` (blue), `output_warning()` (yellow), `output_error()` (red)

**Validation:**
- Module: `src/core/parameter_validator.c`
- Approach: Intelligent validation with auto-correction (avoids OOM, prevents excessive threading)
- Checks: Thread count vs CPU cores, N/K vs available RAM, batch size alignment

**Authentication:**
- Module: `src/wizard/wizard.c`
- Approach: Token-based for distributed mode, secure file storage
- Used for: Coordinator-client authentication in distributed setup

**Performance Profiling:**
- Module: Built-in profiler in `keyhunt.cpp` (KEYHUNT_PROFILE=1 env var)
- Approach: Lightweight instrumentation (no external tools required)
- Tracks: Hash computation, bloom checks, thread coordination overhead

---

*Architecture analysis: 2026-02-28*
