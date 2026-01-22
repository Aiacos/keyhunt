# Source Code Reorganization Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Move all source code into src/ directory structure for cleaner organization.

**Architecture:**
- All code directories move under src/ (hash/, secp256k1/, bloom/, etc.)
- Root-level utility files move to src/core/
- Main programs stay at root (keyhunt.cpp, bsgsd.cpp, keyhunt_legacy.cpp)
- Makefile updated with src/ prefix for all paths
- All #include paths updated to reflect new structure

**Tech Stack:** C/C++, Make

---

## New Directory Structure

```
keyhunt/
├── keyhunt.cpp                    # Main program (stays at root)
├── bsgsd.cpp                      # BSGS daemon (stays at root)
├── keyhunt_legacy.cpp             # Legacy version (stays at root)
├── Makefile                       # Updated paths
│
├── src/
│   ├── core/                      # Core utilities (from root)
│   │   ├── config.c/h
│   │   ├── sysinfo.c/h
│   │   ├── util.c/h
│   │   ├── parameter_validator.c/h
│   │   ├── hashing.c/h
│   │   └── workqueue.h
│   │
│   ├── hash/                      # Hash implementations
│   ├── secp256k1/                 # EC operations
│   ├── gmp256k1/                  # Legacy EC (GMP)
│   ├── bloom/                     # Bloom filters
│   ├── oldbloom/                  # Legacy bloom
│   ├── bsgs/                      # BSGS algorithm
│   ├── gpu/                       # GPU backend
│   ├── hybrid/                    # Hybrid scheduler
│   ├── distributed/               # Distributed computing
│   ├── wizard/                    # Interactive wizard
│   ├── sha3/                      # SHA3/Keccak
│   ├── rmd160/                    # RIPEMD160
│   ├── base58/                    # Base58 encoding
│   ├── xxhash/                    # XXHash
│   ├── util/                      # Mempool utility
│   │
│   ├── output.cpp/h               # Output module (already here)
│   ├── progress.cpp/h             # Progress module (already here)
│   ├── benchmark.cpp/h            # Benchmark module (already here)
│   └── cli.cpp/h                  # CLI module (already here)
```

---

## Task 1: Create src/core/ and Move Root Utilities

**Files:**
- Create: `src/core/` directory
- Move: `config.c`, `config.h` → `src/core/`
- Move: `sysinfo.c`, `sysinfo.h` → `src/core/`
- Move: `util.c`, `util.h` → `src/core/`
- Move: `parameter_validator.c`, `parameter_validator.h` → `src/core/`
- Move: `hashing.c`, `hashing.h` → `src/core/`
- Move: `workqueue.h` → `src/core/`

**Step 1: Create directory and move files**

```bash
mkdir -p src/core
git mv config.c config.h src/core/
git mv sysinfo.c sysinfo.h src/core/
git mv util.c util.h src/core/
git mv parameter_validator.c parameter_validator.h src/core/
git mv hashing.c hashing.h src/core/
git mv workqueue.h src/core/
```

**Step 2: Verify files moved**

```bash
ls -la src/core/
```

Expected: All 11 files present in src/core/

---

## Task 2: Move Code Directories into src/

**Files:**
- Move: `hash/` → `src/hash/`
- Move: `secp256k1/` → `src/secp256k1/`
- Move: `gmp256k1/` → `src/gmp256k1/`
- Move: `bloom/` → `src/bloom/`
- Move: `oldbloom/` → `src/oldbloom/`
- Move: `bsgs/` → `src/bsgs/`
- Move: `gpu/` → `src/gpu/`
- Move: `hybrid/` → `src/hybrid/`
- Move: `distributed/` → `src/distributed/`
- Move: `wizard/` → `src/wizard/`
- Move: `sha3/` → `src/sha3/`
- Move: `rmd160/` → `src/rmd160/`
- Move: `base58/` → `src/base58/`
- Move: `xxhash/` → `src/xxhash/`
- Move: `util/` → `src/util/` (mempool)

**Step 1: Move all code directories**

```bash
git mv hash/ src/
git mv secp256k1/ src/
git mv gmp256k1/ src/
git mv bloom/ src/
git mv oldbloom/ src/
git mv bsgs/ src/
git mv gpu/ src/
git mv hybrid/ src/
git mv distributed/ src/
git mv wizard/ src/
git mv sha3/ src/
git mv rmd160/ src/
git mv base58/ src/
git mv xxhash/ src/
git mv util/ src/
```

**Step 2: Verify structure**

```bash
ls -la src/
```

Expected: All directories present under src/

---

## Task 3: Update Internal Includes in Moved Modules

**Files to modify:** All .c/.cpp/.h files in src/ subdirectories

**Step 1: Update bloom/ includes**

In `src/bloom/bloom.cpp`:
```cpp
// Change from:
#include "../xxhash/xxhash.h"
#include "../util/mempool.h"
// To:
#include "../xxhash/xxhash.h"    // Same (relative within src/)
#include "../util/mempool.h"     // Same
```

In `src/bloom/bloom_simd.cpp`:
```cpp
// Change from:
#include "../xxhash/xxhash.h"
// To: (same, relative path still works)
#include "../xxhash/xxhash.h"
```

In `src/bloom/bloom_fast.h`:
```cpp
// Change from:
#include "../xxhash/xxhash.h"
// To: (same)
#include "../xxhash/xxhash.h"
```

**Step 2: Update oldbloom/ includes**

In `src/oldbloom/bloom.cpp`:
```cpp
// Change from:
#include "../xxhash/xxhash.h"
// To: (same, relative within src/)
#include "../xxhash/xxhash.h"
```

**Step 3: Update gmp256k1/ includes**

In `src/gmp256k1/GMP256K1.cpp`:
```cpp
// Change from:
#include "../util.h"
#include "../hashing.h"
// To:
#include "../core/util.h"
#include "../core/hashing.h"
```

**Step 4: Update bsgs/ includes**

In `src/bsgs/bsgs_ops.h`:
```cpp
// Change from:
#include "../secp256k1/Point.h"
#include "../secp256k1/IntGroup.h"
// To: (same, both now under src/)
#include "../secp256k1/Point.h"
#include "../secp256k1/IntGroup.h"
```

**Step 5: Update wizard/ includes**

In `src/wizard/wizard.c`:
```cpp
// Change from:
#include "../sysinfo.h"
// To:
#include "../core/sysinfo.h"
```

In `src/wizard/wizard_server.c`:
```cpp
// Change from:
#include "../distributed/distributed.h"
#include "../sysinfo.h"
// To:
#include "../distributed/distributed.h"  // Same
#include "../core/sysinfo.h"
```

In `src/wizard/wizard_client.c`:
```cpp
// Change from:
#include "../distributed/distributed.h"
#include "../sysinfo.h"
// To:
#include "../distributed/distributed.h"  // Same
#include "../core/sysinfo.h"
```

**Step 6: Update parameter_validator.h**

In `src/core/parameter_validator.h`:
```cpp
// Change from:
#include "sysinfo.h"
// To: (same, both in core/)
#include "sysinfo.h"
```

---

## Task 4: Update Main Program Includes

**Files:**
- Modify: `keyhunt.cpp`
- Modify: `bsgsd.cpp`
- Modify: `keyhunt_legacy.cpp`

**Step 1: Update keyhunt.cpp includes**

```cpp
// Change from:
#include "base58/libbase58.h"
#include "oldbloom/oldbloom.h"
#include "bloom/bloom.h"
#include "bloom/bloom_wrapper.h"
#include "sha3/sha3.h"
#include "util.h"
#include "workqueue.h"
#include "sysinfo.h"
#include "parameter_validator.h"
#include "gpu/gpu_backend.h"
#include "config.h"
#include "hybrid/adaptive_scheduler.h"
#include "wizard/wizard.h"
#include "secp256k1/SECP256k1.h"
#include "secp256k1/Point.h"
#include "secp256k1/Int.h"
#include "secp256k1/IntGroup.h"
#include "secp256k1/Random.h"
#include "hash/sha256.h"
#include "hash/ripemd160.h"

// To:
#include "src/base58/libbase58.h"
#include "src/oldbloom/oldbloom.h"
#include "src/bloom/bloom.h"
#include "src/bloom/bloom_wrapper.h"
#include "src/sha3/sha3.h"
#include "src/core/util.h"
#include "src/core/workqueue.h"
#include "src/core/sysinfo.h"
#include "src/core/parameter_validator.h"
#include "src/gpu/gpu_backend.h"
#include "src/core/config.h"
#include "src/hybrid/adaptive_scheduler.h"
#include "src/wizard/wizard.h"
#include "src/secp256k1/SECP256k1.h"
#include "src/secp256k1/Point.h"
#include "src/secp256k1/Int.h"
#include "src/secp256k1/IntGroup.h"
#include "src/secp256k1/Random.h"
#include "src/hash/sha256.h"
#include "src/hash/ripemd160.h"
```

**Step 2: Update bsgsd.cpp includes**

```cpp
// Change from:
#include "base58/libbase58.h"
#include "oldbloom/oldbloom.h"
#include "bloom/bloom.h"
#include "sha3/sha3.h"
#include "util.h"
#include "secp256k1/SECP256k1.h"
// etc.

// To:
#include "src/base58/libbase58.h"
#include "src/oldbloom/oldbloom.h"
#include "src/bloom/bloom.h"
#include "src/sha3/sha3.h"
#include "src/core/util.h"
#include "src/secp256k1/SECP256k1.h"
// etc.
```

**Step 3: Update keyhunt_legacy.cpp includes**

```cpp
// Change from:
#include "base58/libbase58.h"
#include "oldbloom/oldbloom.h"
#include "bloom/bloom.h"
#include "util.h"
#include "hashing.h"
#include "workqueue.h"
#include "gmp256k1/GMP256K1.h"
// etc.

// To:
#include "src/base58/libbase58.h"
#include "src/oldbloom/oldbloom.h"
#include "src/bloom/bloom.h"
#include "src/core/util.h"
#include "src/core/hashing.h"
#include "src/core/workqueue.h"
#include "src/gmp256k1/GMP256K1.h"
// etc.
```

---

## Task 5: Update Makefile Paths

**File:** `Makefile`

**Step 1: Update object file paths**

```makefile
# Change all paths from:
BLOOM_OBJS := oldbloom/bloom.o bloom/bloom.o bloom/bloom_simd.o
HASH_OBJS := hash/ripemd160.o hash/ripemd160_sse.o ...
# etc.

# To:
BLOOM_OBJS := src/oldbloom/bloom.o src/bloom/bloom.o src/bloom/bloom_simd.o
HASH_OBJS := src/hash/ripemd160.o src/hash/ripemd160_sse.o src/hash/ripemd160_avx2.o src/hash/ripemd160_avx512.o src/hash/sha256.o src/hash/sha256_sse.o src/hash/sha256_avx2.o src/hash/sha256_shani.o
SHA3_OBJS := src/sha3/sha3.o src/sha3/keccak.o
SECP256K1_OBJS := src/secp256k1/Int.o src/secp256k1/Point.o src/secp256k1/SECP256K1.o src/secp256k1/IntMod.o src/secp256k1/Random.o src/secp256k1/IntGroup.o
GMP256K1_OBJS := src/gmp256k1/Int.o src/gmp256k1/Point.o src/gmp256k1/GMP256K1.o src/gmp256k1/IntMod.o src/gmp256k1/Random.o src/gmp256k1/IntGroup.o
BSGS_OBJS := src/bsgs/bsgs_ops.o src/bsgs/bsgs_fast.o
HYBRID_OBJS := src/hybrid/adaptive_scheduler.o
UTIL_OBJS := src/util/mempool.o
DIST_OBJS := src/distributed/distributed.o
WIZARD_OBJS := src/wizard/wizard.o src/wizard/wizard_config.o src/wizard/wizard_ui.o src/wizard/wizard_community.o src/wizard/wizard_server.o src/wizard/wizard_client.o
GPU_OBJS := src/gpu/gpu_backend_none.o src/gpu/gpu_autotune.o src/gpu/multi_gpu_scheduler.o src/gpu/async_pipeline.o

COMMON_OBJS := src/base58/base58.o src/rmd160/rmd160.o src/xxhash/xxhash.o src/core/util.o src/core/sysinfo.o src/core/parameter_validator.o src/core/config.o $(GPU_OBJS) $(BLOOM_OBJS) $(HASH_OBJS) $(SHA3_OBJS) $(BSGS_OBJS) $(HYBRID_OBJS) $(UTIL_OBJS) $(DIST_OBJS) $(OUTPUT_OBJS) $(PROGRESS_OBJS) $(BENCHMARK_OBJS) $(CLI_OBJS)
```

**Step 2: Update special build rules**

```makefile
# Update util.o and hashing.o rules
src/core/util.o: src/core/util.c
	$(CXX) $(CXXFLAGS) -c $< -o $@

src/core/hashing.o: src/core/hashing.c
	$(CXX) $(CXXFLAGS) -c $< -o $@

src/sha3/sha3.o: src/sha3/sha3.c
	$(CXX) $(CXXFLAGS) -c $< -o $@

src/sha3/keccak.o: src/sha3/keccak.c
	$(CXX) $(CXXFLAGS) -c $< -o $@

# AVX2 optimized builds
src/hash/ripemd160_avx2.o: src/hash/ripemd160_avx2.cpp
	$(CXX) $(CXXFLAGS) -mavx2 -c $< -o $@

src/hash/sha256_avx2.o: src/hash/sha256_avx2.cpp
	$(CXX) $(CXXFLAGS) -mavx2 -c $< -o $@

# AVX-512 optimized builds
src/hash/ripemd160_avx512.o: src/hash/ripemd160_avx512.cpp
	$(CXX) $(CXXFLAGS) -mavx512f -mavx512dq -c $< -o $@

# SHA-NI optimized builds
src/hash/sha256_shani.o: src/hash/sha256_shani.cpp
	$(CXX) $(CXXFLAGS) -msha -msse4.1 -c $< -o $@

# SIMD bloom filter
src/bloom/bloom_simd.o: src/bloom/bloom_simd.cpp
	$(CXX) $(CXXFLAGS) -mavx2 -c $< -o $@

# BSGS optimized modules
src/bsgs/bsgs_ops.o: src/bsgs/bsgs_ops.cpp
	$(CXX) $(CXXFLAGS) -mavx2 -c $< -o $@

src/bsgs/bsgs_fast.o: src/bsgs/bsgs_fast.cpp
	$(CXX) $(CXXFLAGS) -mavx2 -c $< -o $@
```

**Step 3: Update clean target**

```makefile
clean:
	$(RM) keyhunt keyhunt_legacy bsgsd
	$(RM) $(KEYHUNT_OBJS) $(BSGSD_OBJS) $(LEGACY_OBJS)
	$(RM) src/core/*.o src/hash/*.o src/secp256k1/*.o src/gmp256k1/*.o
	$(RM) src/bloom/*.o src/oldbloom/*.o src/bsgs/*.o src/gpu/*.o
	$(RM) src/hybrid/*.o src/util/*.o src/distributed/*.o src/wizard/*.o
	$(RM) src/sha3/*.o src/rmd160/*.o src/base58/*.o src/xxhash/*.o
	$(RM) src/*.o
```

---

## Task 6: Update Benchmark and Tool Includes

**Files:**
- Modify: `benchmarks/benchmark_bloom.cpp`
- Modify: `benchmarks/benchmark_bloom_integrated.cpp`
- Modify: `benchmarks/benchmark_modmul.cpp`
- Modify: `tools/puzzle71_coordinator.c`

**Step 1: Update benchmark includes**

In `benchmarks/benchmark_bloom.cpp`:
```cpp
// Change from:
#include "bloom/bloom.h"
#include "bloom/bloom_fast.h"
// To:
#include "../src/bloom/bloom.h"
#include "../src/bloom/bloom_fast.h"
```

In `benchmarks/benchmark_bloom_integrated.cpp`:
```cpp
// Change from:
#include "bloom/bloom_wrapper.h"
// To:
#include "../src/bloom/bloom_wrapper.h"
```

In `benchmarks/benchmark_modmul.cpp`:
```cpp
// Change from:
#include "secp256k1/Int.h"
#include "secp256k1/IntMod_asm.h"
// To:
#include "../src/secp256k1/Int.h"
#include "../src/secp256k1/IntMod_asm.h"
```

**Step 2: Update tools includes**

In `tools/puzzle71_coordinator.c`:
```cpp
// Change from:
#include "distributed/distributed.h"
// To:
#include "../src/distributed/distributed.h"
```

---

## Task 7: Build and Test

**Step 1: Clean old build artifacts**

```bash
make clean
rm -f src/**/*.o
```

**Step 2: Build**

```bash
make 2>&1 | head -50
```

Expected: Successful compilation with no errors

**Step 3: Quick functionality test**

```bash
timeout 5 ./keyhunt -m address -f tests/1to32.txt -r 1:FFFF -q 2>&1 | head -20
```

Expected: Program runs correctly, finds keys

**Step 4: Test all modes**

```bash
timeout 3 ./keyhunt -m rmd160 -f tests/66.rmd -b 66 -R -q 2>&1 | head -10
timeout 3 ./keyhunt -m xpoint -f tests/120.txt -b 120 -R -q 2>&1 | head -10
timeout 3 ./keyhunt --benchmark 2>&1 | head -20
```

Expected: All modes work correctly

---

## Task 8: Commit Reorganization

**Step 1: Stage all changes**

```bash
git add -A
git status
```

**Step 2: Commit**

```bash
git commit -m "refactor: move all source code into src/ directory

- Create src/core/ for root-level utilities (config, sysinfo, util, etc.)
- Move all code directories under src/ (hash, secp256k1, bloom, etc.)
- Update all #include paths to reflect new structure
- Update Makefile with src/ prefixes for all paths
- Main programs remain at root (keyhunt.cpp, bsgsd.cpp, keyhunt_legacy.cpp)

New structure:
  src/core/     - Core utilities
  src/hash/     - SIMD hash implementations
  src/secp256k1/- EC operations
  src/bloom/    - Bloom filters
  src/gpu/      - GPU backend
  src/wizard/   - Interactive wizard
  src/...       - Other modules

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"
```

---

## Verification Checklist

After completing all tasks:

- [ ] `make clean && make` succeeds without errors
- [ ] `./keyhunt --help` works
- [ ] `./keyhunt -m address -f tests/1to32.txt -r 1:FF` finds keys
- [ ] `./keyhunt --benchmark` runs
- [ ] No source files remain at root (except main programs)
- [ ] All .o files compile in src/ subdirectories
