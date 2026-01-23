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

### Compilation Notes
- Main version uses custom secp256k1 implementation (no external crypto libs)
- Legacy version requires OpenSSL and GMP libraries
- AVX2/AVX-512 optimizations compile with specific flags (`-mavx2`, `-mavx512f`)
- Optimization level: `-O2` (changed from `-Ofast` to fix Ubuntu freeze issues)
- CUDA builds require CUDA 11.0+ and compatible GCC (13 recommended, 14+ works with flags)

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

**Runtime CPU feature detection**: The program automatically selects the best implementation based on detected CPU features. See `g_avx2_available` in keyhunt.cpp:66.

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

## Documentation Files

- **README.md**: User documentation, examples, FAQ
- **WIZARD.md**: **NEW** - Interactive wizard for distributed mode
- **PARAMETER_VALIDATION.md**: Intelligent parameter validation and auto-tuning
- **OPTIMIZATIONS.md**: AVX2/SIMD optimization details
- **AUTO-TUNING.md**: Hardware detection and auto-configuration
- **BSGS_MEMORY_CHECK.md**: Memory validation system
- **PERFORMANCE_ANALYSIS.md**: Benchmark results and optimization phases
- **CHANGELOG.md**: Version history and changes
