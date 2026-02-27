# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Working Style Guidelines

### MANDATORY: Always Use Available Tools

**CRITICAL**: Before starting ANY task, you MUST check and use available tools in this priority order:

#### 1. MCP Servers (Highest Priority)
MCP (Model Context Protocol) servers provide enhanced capabilities. **Always prefer MCP tools over built-in alternatives**:

- **Check available MCP servers** at the start of each session
- **Use MCP file operations** instead of Bash cat/read when available
- **Use MCP web fetch** instead of built-in WebFetch when available (fewer restrictions)
- **Use MCP database tools** for any database operations
- **Use MCP API tools** for external service integrations

**Common MCP patterns**:
```
# If MCP filesystem server is available:
- Use it for reading/writing files (may have fewer restrictions)

# If MCP fetch server is available:
- Use it for web requests (often bypasses rate limits)

# If MCP git server is available:
- Use it for git operations (may provide richer output)
```

#### 2. Plugins (High Priority)
Plugins extend Claude's capabilities with specialized functionality:

- **pr-review-toolkit** - Code review, type analysis, silent failure detection
- **superpowers** - Brainstorming, planning, debugging, TDD workflows
- **claude-md-management** - CLAUDE.md maintenance and improvements

**Always check plugin availability** and use relevant plugins proactively.

#### 3. Skills (Required for Workflows)
**MANDATORY**: Invoke relevant skills using the Skill tool BEFORE starting work:

| Skill | When to Use |
|-------|-------------|
| **brainstorming** | Creative work, new features, design decisions |
| **writing-plans** | Multi-step implementation tasks |
| **executing-plans** | Implementing written plans |
| **subagent-driven-development** | Parallel task execution with review |
| **systematic-debugging** | ANY bug, test failure, or unexpected behavior |
| **test-driven-development** | Writing tests before implementation |
| **code-review** | Reviewing completed work |
| **verification-before-completion** | Verifying work before claiming completion |
| **finishing-a-development-branch** | Completing development work |
| **receiving-code-review** | When receiving feedback on code |

**If there's even a 1% chance a skill applies, invoke it.**

#### 4. Task Tool and Subagents
Use specialized subagents for parallel and complex work:

- **Explore** - Codebase exploration and understanding
- **code-reviewer** - Code quality review
- **silent-failure-hunter** - Find silent failures in error handling
- **type-design-analyzer** - Analyze type design quality
- **pr-test-analyzer** - Review test coverage

### Work Autonomously with Agents

When facing complex or multi-step tasks:
1. Use the **Task tool** to spawn specialized subagents for parallel work
2. Use **TodoWrite** to track progress on multi-step tasks
3. Run independent tasks in parallel using multiple Task tool calls
4. Let agents complete their work before integrating results

### Proactive Behavior

- Run tests after making code changes
- Use code review agents after implementing features
- Verify builds succeed before committing
- Update documentation when adding new features
- Commit changes with meaningful messages

## Overview

**keyhunt** is a high-performance cryptocurrency private key search tool for secp256k1-based cryptocurrencies (Bitcoin, Ethereum). It implements multiple search algorithms optimized for CPU with SIMD instructions (SSE2/AVX2/AVX-512).

## Build Commands

### Standard Build
```bash
make              # Build main keyhunt executable
make clean        # Remove all build artifacts
```

### Alternative Builds
```bash
make legacy       # Build legacy version (requires libssl-dev, libgmp-dev)
make bsgsd        # Build BSGS daemon variant
```

### CUDA/GPU Build
```bash
./build_cuda.sh              # Auto-detect CUDA, GPU arch, and GCC compatibility
./build_cuda.sh --arch sm_86 # Specify GPU architecture (RTX 3000)
./build_cuda.sh --arch sm_89 # Specify GPU architecture (RTX 4000)
./build_cuda.sh --help       # Show all options
```

The `build_cuda.sh` script:
- Auto-detects CUDA toolkit location
- Auto-detects GPU architecture from nvidia-smi
- Finds compatible GCC version (or uses `-allow-unsupported-compiler` for GCC 14+)
- Handles all nvcc flags automatically

Manual CUDA build:
```bash
make NVCC=/usr/local/cuda/bin/nvcc \
     CUDA_HOME=/usr/local/cuda \
     NVCCFLAGS='-O3 -std=c++17 -arch=sm_75 -allow-unsupported-compiler'
```

### Profile-Guided Optimization (PGO) Build
```bash
make pgo-generate    # Build with profiling instrumentation
make pgo-train       # Run representative workload to collect profile data
make pgo-use         # Build optimized binary using collected profiles
```

The PGO build process:
- **Step 1 (pgo-generate)**: Builds keyhunt with `-fprofile-generate` to collect runtime data
- **Step 2 (pgo-train)**: Runs a representative workload (address search on test file)
- **Step 3 (pgo-use)**: Rebuilds with `-fprofile-use` to optimize hot paths

**Benefits**: 5-15% performance improvement by optimizing branch prediction, function inlining, and code layout based on actual usage patterns. Particularly effective for SIMD-heavy code paths.

**Custom training workload**:
```bash
make pgo-generate
./keyhunt -m address -f your_training_file.txt -r 1:FFFFFFFF -s 30
make pgo-use
```

**Cleanup**:
```bash
make pgo-clean       # Remove profile data (*.gcda files)
```

### Compilation Notes
- Main version uses custom secp256k1 implementation (no external crypto libs)
- Legacy version requires OpenSSL and GMP libraries
- AVX2/AVX-512 optimizations compile with specific flags (`-mavx2`, `-mavx512f`)
- Optimization level: `-O2` (changed from `-Ofast` to fix Ubuntu freeze issues)
- CUDA builds require CUDA 11.0+ and compatible GCC (13 recommended, 14+ works with flags)
- PGO builds provide additional 5-15% performance boost over standard `-O2` builds

## Testing

Test files are in `tests/` directory:
```bash
# Quick functionality test
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF

# BSGS mode test
./keyhunt -m bsgs -f tests/125.txt -b 125 -q -s 10 -R

# Test with different modes
./keyhunt -m rmd160 -f tests/66.rmd -b 66 -l compress -R -q
./keyhunt -m xpoint -f tests/120.txt -t 4 -b 125 -R -q
```

## Architecture

### Configuration System

**NEW**: The codebase has migrated from 50+ global variables to a structured configuration system (`src/config/config.h`) for better maintainability, testability, and thread-safety.

#### Configuration Structure Hierarchy

```
keyhunt_config_t (top-level container)
├── search_config_t       - Search mode, range, flags
├── bsgs_config_t         - BSGS algorithm parameters
├── gpu_config_t          - GPU acceleration settings
├── autotune_config_t     - Auto-detected system info
└── runtime_state_t       - Mutable execution state
```

**Benefits**:
- ✅ Explicit dependency injection (functions receive config as parameter)
- ✅ Thread-safe configuration passing
- ✅ Type-safe enums instead of magic numbers
- ✅ Grouped, logical organization
- ✅ Easier unit testing and code comprehension

**Developer Guide**: See `MIGRATION_GUIDE.md` for complete mapping of legacy globals to config fields (e.g., `FLAGMODE` → `config->search.mode`, `NTHREADS` → `config->runtime.thread_count`).

### Core Search Modes (MODE_*)

The tool operates in 6 distinct modes, each optimized for different search scenarios:

1. **ADDRESS** (`-m address`): Search for Bitcoin addresses using bloom filters
2. **RMD160** (`-m rmd160`): Search for RIPEMD160 hashes directly
3. **XPOINT** (`-m xpoint`): Search for public key X-coordinates (fastest for known pubkeys)
4. **BSGS** (`-m bsgs`): Baby Step Giant Step algorithm for known public keys
5. **VANITY** (`-m vanity`): Generate vanity addresses with specific prefixes
6. **PUB2RMD** (`-m pub2rmd`): Attempt to find public keys from RMD160 hashes

### SIMD-Optimized Hashing Pipeline

**Critical performance path**: The main bottleneck is RIPEMD160 hashing in address generation.

#### Hash Implementations (hash/)
- `ripemd160.cpp`: Reference implementation for 32-byte inputs
- `ripemd160_sse.cpp`: SSE2 4-way parallel (128-bit SIMD)
- `ripemd160_avx2.cpp`: **AVX2 8-way parallel (256-bit SIMD)** - 2× faster than SSE2
- `ripemd160_avx512.cpp`: AVX-512 16-way parallel (512-bit SIMD) - cutting-edge CPUs
- `sha256.cpp`, `sha256_sse.cpp`: SHA256 implementations (SHA-NI available)
- `sha512.cpp`: SHA512 reference implementation
- `sha512_avx2.cpp`: AVX2 4-way parallel (256-bit SIMD, 64-bit operations)
- `sha512_avx512.cpp`: AVX-512 8-way parallel (512-bit SIMD) - for HD wallet key derivation

**Runtime CPU feature detection**: The program automatically selects the best implementation based on detected CPU features stored in `config->autotune.avx2_available`.

#### Secp256k1 Elliptic Curve Operations

Two implementations exist:
- **secp256k1/** (custom, used by default): Pure C++ implementation, no external dependencies
- **gmp256k1/** (legacy only): Uses GNU MP library for big integer operations

Key classes:
- `Int`: 256-bit big integer with modular arithmetic
- `Point`: Elliptic curve point (x, y coordinates)
- `SECP256K1`: Main curve operations class with methods like `GetHash160_AVX2()`
- `IntGroup`: Batch modular inversion using Montgomery's trick

### BSGS Algorithm Architecture

The Baby Step Giant Step mode is the most memory-intensive but efficient for known public keys.

#### Memory Structure
BSGS pre-computes a table of "baby steps" and checks "giant steps" against bloom filters:

1. **Bloom Filters** (3 levels for different collision rates):
   - bloom1: Main filter (~100% of data, 3.5 bytes/element)
   - bloom2: Reduced hash (~3% of bloom1, 1/32 size)
   - bloom3: Ultra-reduced (~0.1% of bloom1, 1/1024 size)

2. **bP Table**: Pre-computed baby step points, stored as `(M / 32 * K) * 16` bytes

#### Memory Formula
```
N = search range (-n parameter)
K = multiplier factor (-k parameter)
M = sqrt(N)

Total RAM = (M * K * 3.5) + (M * K * 3.5 / 32) + (M * K * 3.5 / 1024) + (M / 32 * K * 16)
```

**Memory validation** (keyhunt.cpp:1637-1716): Automatically checks available RAM before allocation and suggests appropriate N/K values if insufficient.

#### BSGS Optimizations
- **Batched bloom checks** (bsgs_optimized.h): Process 64 points at a time with prefetching
- **Batched loop** (bsgs_batched_loop.cpp): Vectorized inner loops
- **File caching** (`-S` flag): Save/load bloom filters and bP tables to avoid recomputation

### Auto-Tuning and Validation System

### Hardware Detection (sysinfo.c/h)

**Hardware detection** automatically configures optimal parameters:

1. **CPU Detection**:
   - Physical cores (not hyperthreads): Parse `/sys/devices/system/cpu/*/topology/`
   - Cache sizes (L1/L2/L3): Read `/sys/devices/system/cpu/cpu*/cache/`
   - CPU features (AVX2/AVX-512/SHA-NI): Parse `/proc/cpuinfo`

2. **Memory Detection**:
   - Total/Available RAM: Parse `/proc/meminfo` or `sysinfo()` syscall
   - 75% of available RAM used as safe limit

3. **Auto-tuned Parameters**:
   - **Threads**: All logical cores (hyperthreading enabled)
   - **Batch size**: 1024 (proven optimal, aligned to AVX2)
   - **N value**: Largest N that fits in 60% of available RAM
   - **K factor**: Balanced K based on N and RAM

**Override**: Use `-t N` to manually specify thread count. Auto-tuning can be bypassed with `KEYHUNT_SKIP_SYSINFO=1` environment variable.

### Parameter Validation (parameter_validator.c/h)

**NEW**: Intelligent parameter validation system that:

1. **Validates User Parameters**:
   - Checks threads against available CPU cores
   - Validates N and K factor against available RAM (BSGS mode)
   - Ensures batch size is properly aligned for AVX2

2. **Auto-Correction**:
   - Corrects dangerous values that would cause OOM or crashes
   - Adjusts excessive thread counts to prevent overhead
   - Scales down N/K combinations that exceed available RAM

3. **User Feedback**:
   - ✓ (green): Parameter is optimal
   - i (blue): Parameter works but isn't optimal
   - ! (yellow): Parameter was auto-corrected for safety
   - ⚠ (red): Parameter may cause performance issues

4. **Safety Guarantees**:
   - Prevents out-of-memory crashes
   - Avoids excessive context switching
   - Ensures cache-aligned operations

See [PARAMETER_VALIDATION.md](PARAMETER_VALIDATION.md) for detailed documentation and examples.

### Bloom Filter Implementation

Two bloom filter versions exist:
- **bloom/**: New implementation with better memory characteristics
- **oldbloom/**: Original implementation (kept for compatibility)

## Critical Code Locations

### Main Search Loop
- `keyhunt.cpp:640-720`: Main key generation and checking loop
  - Calls `secp->GetHash160_AVX2()` or `GetHash160()` based on CPU features
  - Uses bloom filters for fast negative lookups
  - Binary search in sorted target array on bloom hits

### BSGS Core
- `keyhunt.cpp:1800-2100`: BSGS mode initialization and main loop
- `bsgs_batched_loop.cpp`: Vectorized BSGS inner loop
- File I/O for bloom filter caching: `keyhunt_bsgs_*.blm`, `keyhunt_bsgs_*.tbl`

### Endomorphism Optimization
- `keyhunt.cpp`: `-e` flag enables checking 6 related keys per computation
- Uses lambda/beta constants for secp256k1 endomorphism
- Only beneficial when searching full curve (not puzzles with specific ranges)

## Performance Considerations

### Optimization Levels
- **DO NOT use `-Ofast`**: Causes system freezes on Ubuntu (see FREEZE_ISSUES_REPORT.md)
- **Use `-O2`**: Current stable optimization level
- **LTO enabled**: Link-time optimization with `-flto=auto`

### CPU Tuning (cpu_tuning.cpp/h)
- Batch sizes aligned to cache line (64 bytes)
- Prefetching for bloom filter accesses
- Thread affinity (future enhancement)

### Memory Patterns
- Sequential access patterns preferred for cache efficiency
- Bloom filter checks batched to amortize memory latency
- Point batches aligned to 64-byte boundaries for SIMD

### Known Issues
- **Ubuntu freeze**: Fixed by switching to `-O2` (commit: 20c8824)
- **getrandom() failures**: Non-fatal fallback to time-based seed implemented
- **BSGS OOM**: Memory validation prevents out-of-memory crashes

## Common Development Scenarios

### Adding a New Hash Function Optimization
1. Create `hash/newhash_avx2.cpp` with SIMD implementation
2. Add detection function (e.g., `newhash_avx2_available()`)
3. Update `Makefile` with specific compiler flags
4. Integrate into `SECP256K1.cpp` with runtime dispatch
5. Test fallback to non-SIMD version

### Modifying BSGS Parameters
- **Increasing N or K**: Update memory check formula in keyhunt.cpp:1637-1716
- **File format changes**: Update version/magic numbers in bloom filter headers
- **New bloom filter level**: Add to 3-tier hierarchy (bloom1/2/3)

### Performance Profiling
```bash
# Build with profiling
make clean
CXXFLAGS="-pg -O2" make

# Run and generate profile
./keyhunt -m address -f tests/66.txt -b 66 -R -q -s 10
gprof keyhunt gmon.out > analysis.txt
```

### Testing Memory Safety
```bash
# Valgrind (slow but thorough)
valgrind --leak-check=full ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF

# AddressSanitizer (faster)
make clean
CXXFLAGS="-fsanitize=address -O2" make
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF
```

## Cryptographic Notes

This tool is designed for **educational purposes and authorized security testing** (puzzles, CTF challenges, research).

- Searches occur on the secp256k1 elliptic curve (y² = x³ + 7)
- Private key space: 2^256 (practically: 2^160 for RIPEMD160 collision)
- Puzzle solving: Bitcoin puzzles have known public keys in specific bit ranges

## Interactive Wizard (NEW)

### Usage
```bash
./keyhunt --wizard    # or ./keyhunt -W
```

### Features
The wizard provides an interactive 5-step setup for distributed puzzle solving:

1. **Puzzle Selection**: Downloads puzzle database from BTCPuzzle.info (with built-in fallback)
2. **Mode Selection**: Server (coordinator + local worker) or Client (worker only)
3. **Server Configuration**: Port, work unit size, checkpoint interval
4. **Search Configuration**: Auto-detects hardware and recommends optimal parameters
5. **Community Integration**: Fetches already-scanned ranges to avoid duplicate work

### Intelligent Configuration
The wizard calculates optimal parameters based on puzzle characteristics:

| Puzzle Type | Recommended Mode | Strategy | GPU | Random |
|-------------|------------------|----------|-----|--------|
| Has public key | BSGS | O(√N) complexity | Disabled | No |
| No public key | Address | Brute-force | Enabled | Yes (>66 bits) |

For BSGS mode, it automatically calculates optimal N and K values based on available RAM:
- Uses 60% of available RAM for safety
- Calculates max entries: `safe_ram / 20 bytes per entry`
- Selects N as largest power of 2 that fits

### Configuration Persistence
- Saves to `keyhunt_wizard.json` (human-readable JSON)
- Auto-resumes if previous configuration exists
- Puzzle cache stored in `puzzles_cache.txt`

### Architecture
```
wizard/
├── wizard.h              # Main header with data structures
├── wizard.c              # Entry point (5-step flow)
├── wizard_config.c       # JSON config + puzzle database
├── wizard_ui.c           # Interactive terminal UI
├── wizard_community.c    # BTCPuzzle.info integration
├── wizard_server.c       # Server mode (coordinator + worker)
└── wizard_client.c       # Client mode (auto-config worker)
```

### Server Mode
The server runs as both **coordinator** (distributing work) and **local worker** (processing ranges):
- Starts coordinator on specified port
- Spawns local worker thread if `server_also_worker` is enabled
- Tracks progress and saves checkpoints
- Handles community exclusions

### Client Mode
- Auto-detects local hardware (CPU, RAM, GPU, SIMD features)
- Connects to coordinator and requests work units
- Reports progress via heartbeat messages
- Saves found keys locally and reports to server

## Modular Components (src/)

### Output Module (src/output.h, src/output.cpp)
Provides colored output with verbosity levels:
- `OUTPUT_SILENT` - No output except errors and key found
- `OUTPUT_MINIMAL` - Clean progress output
- `OUTPUT_NORMAL` - Standard output (default)
- `OUTPUT_VERBOSE` - Debug output

Functions: `output_error()`, `output_warning()`, `output_info()`, `output_success()`

### Progress Module (src/progress.h, src/progress.cpp)
Persistent progress tracking with auto-save:
- Saves progress to `~/.keyhunt/progress/` every 60 seconds
- JSON format for human-readable inspection
- Functions: `progress_init()`, `progress_update()`, `progress_complete()`

### CLI Module (src/cli.h, src/cli.cpp)
Command-line argument parsing with type-safe enums:
- `search_mode_t`: MODE_ADDRESS, MODE_BSGS, MODE_XPOINT, etc.
- `key_type_t`: KEYTYPE_COMPRESSED, KEYTYPE_UNCOMPRESSED, KEYTYPE_BOTH
- `gpu_mode_t`: GPU_OFF, GPU_ON, GPU_AUTO, GPU_HYBRID
- `bsgs_mode_t`: BSGS_SEQUENTIAL, BSGS_BACKWARD, BSGS_BOTH, BSGS_RANDOM, BSGS_DANCE

### Benchmark Module (src/benchmark.h, src/benchmark.cpp)
Integrated performance benchmark:
- Usage: `./keyhunt --benchmark`
- Tests CPU, GPU, and hybrid performance
- Provides recommendations for optimal settings

## Platform Abstraction Layer (src/platform/)

The platform abstraction layer provides a unified, cross-platform API for system-level operations, eliminating the need for scattered `#ifdef` blocks throughout the codebase. It enables seamless compilation on Windows (64-bit) and POSIX-compliant systems (Linux, macOS).

### Design Principles

1. **Single Include Point**: `#include "platform/platform.h"` provides all platform functionality
2. **Opaque Types**: Platform-specific handles (threads, mutexes) are wrapped in unified types
3. **Zero External Dependencies**: Uses only native OS APIs (Windows API, pthread, clock_gettime)
4. **Runtime Detection**: Platform is detected at compile-time via preprocessor macros
5. **Clean API**: Return value convention: 0 = success, non-zero = error code

### Architecture

```
src/platform/
├── platform.h              # Main entry point (includes all sub-headers)
├── platform_types.h        # Opaque type definitions and platform detection
├── platform_thread.h/c     # Thread operations (create, join, detach)
├── platform_mutex.h/c      # Mutex operations (init, lock, unlock, destroy)
└── platform_time.h/c       # High-resolution monotonic time
```

### Platform Detection (platform_types.h)

**Compile-time platform detection**:
```c
#if defined(_WIN64) && !defined(__CYGWIN__)
    #define PLATFORM_WINDOWS 1
    #define PLATFORM_POSIX 0
#else
    #define PLATFORM_WINDOWS 0
    #define PLATFORM_POSIX 1
#endif
```

**Opaque type mappings**:
- `platform_thread_t`: Windows `HANDLE` or POSIX `pthread_t`
- `platform_mutex_t`: Windows `HANDLE` or POSIX `pthread_mutex_t`
- `platform_thread_func_t`: Unified thread function signature across platforms
- `platform_thread_return_t`: Windows `DWORD` or POSIX `void*`

### Thread Operations (platform_thread.h/c)

**API Functions**:
- `platform_thread_create()`: Create and start a new thread
- `platform_thread_join()`: Wait for thread termination, retrieve exit value
- `platform_thread_detach()`: Detach thread for independent execution

**Windows Implementation**:
- Uses `CreateThread` with default stack size and immediate start
- `WaitForSingleObject(INFINITE)` for blocking join
- `GetExitCodeThread` to retrieve exit code
- `CloseHandle` for detaching

**POSIX Implementation**:
- Uses `pthread_create` with default attributes
- `pthread_join` for blocking wait and return value retrieval
- `pthread_detach` for independent thread execution

### Mutex Operations (platform_mutex.h/c)

**API Functions**:
- `platform_mutex_init()`: Initialize a mutex
- `platform_mutex_lock()`: Acquire lock (blocking)
- `platform_mutex_unlock()`: Release lock
- `platform_mutex_destroy()`: Destroy mutex and free resources

**Windows Implementation**:
- Uses `CreateMutex` with NULL security, unnamed, not initially owned
- `WaitForSingleObject(INFINITE)` for blocking lock acquisition
- `ReleaseMutex` to release ownership
- `CloseHandle` to destroy

**POSIX Implementation**:
- Uses `pthread_mutex_init` with default attributes
- `pthread_mutex_lock` for blocking lock acquisition
- `pthread_mutex_unlock` to release lock
- `pthread_mutex_destroy` to free resources

### Time Operations (platform_time.h/c)

**API Function**:
- `platform_time_now_ns()`: Get monotonic timestamp in nanoseconds

**Windows Implementation**:
- Uses `QueryPerformanceCounter` for high-resolution timestamps
- Uses `QueryPerformanceFrequency` to get timer frequency (cached on first call)
- Converts ticks to nanoseconds: `(ticks * 1,000,000,000) / frequency`
- Two-step conversion to avoid overflow: ticks → microseconds → nanoseconds

**POSIX Implementation**:
- Prefers `CLOCK_MONOTONIC_RAW` (not affected by NTP adjustments)
- Falls back to `CLOCK_MONOTONIC` if `CLOCK_MONOTONIC_RAW` unavailable
- Converts `timespec` (seconds + nanoseconds) to total nanoseconds
- Provides true nanosecond resolution (1e-9 seconds)

**Use Cases**:
- Performance measurements and benchmarking
- Elapsed time calculations
- Not suitable for wall-clock time or absolute timestamps

### Usage Example

```c
#include "platform/platform.h"

/* Thread example */
platform_thread_t thread;
platform_thread_create(&thread, my_thread_func, user_data);
platform_thread_join(thread, NULL);

/* Mutex example */
platform_mutex_t mutex;
platform_mutex_init(&mutex);
platform_mutex_lock(&mutex);
/* Critical section */
platform_mutex_unlock(&mutex);
platform_mutex_destroy(&mutex);

/* Timing example */
uint64_t start = platform_time_now_ns();
/* ... do work ... */
uint64_t elapsed = platform_time_now_ns() - start;
double elapsed_seconds = elapsed / 1e9;
```

### Integration Notes

- **Replaces direct platform APIs**: No need for `pthread.h`, `windows.h`, or `time.h` includes in application code
- **Thread-safe**: All operations are thread-safe and can be called concurrently
- **Error handling**: All functions return 0 on success, non-zero error codes on failure
- **No cleanup overhead**: Uses native OS primitives directly without wrappers or allocations

### Testing Platform Abstraction

```bash
# Linux/macOS build
make clean && make
./keyhunt -m address -f tests/1to32.txt -t 4

# Windows build (requires MinGW-w64 or MSVC)
make clean && make CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++
./keyhunt.exe -m address -f tests/1to32.txt -t 4
```

## Documentation Files

- **README.md**: User documentation, examples, FAQ
- **WIZARD.md**: **NEW** - Interactive wizard for distributed mode
- **PARAMETER_VALIDATION.md**: Intelligent parameter validation and auto-tuning
- **OPTIMIZATIONS.md**: AVX2/SIMD optimization details
- **AUTO-TUNING.md**: Hardware detection and auto-configuration
- **BSGS_MEMORY_CHECK.md**: Memory validation system
- **PERFORMANCE_ANALYSIS.md**: Benchmark results and optimization phases
- **CHANGELOG.md**: Version history and changes
- **docs/ENV_VARIABLES.md**: All KEYHUNT_* environment variable overrides
