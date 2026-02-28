# KeyHunt — Production Hardening

## What This Is

KeyHunt is a high-performance cryptocurrency private key search tool for secp256k1-based blockchains (Bitcoin, Ethereum). It implements 6 search algorithms optimized for CPU with SIMD instructions (SSE2/AVX2/AVX-512), GPU acceleration (CUDA for NVIDIA, OpenCL for AMD), and distributed multi-machine coordination. This milestone focuses on hardening the existing codebase to production-ready quality.

## Core Value

Every search result must be cryptographically correct — a single false negative wastes compute time, a single false positive wastes human time. Correctness and stability under sustained load are non-negotiable.

## Requirements

### Validated

- ✓ 6 search modes (ADDRESS, BSGS, XPOINT, RMD160, VANITY, MINIKEYS) — existing
- ✓ SIMD-optimized hash pipeline (SHA256, RIPEMD160) with SSE2/AVX2/AVX-512 — existing
- ✓ Custom secp256k1 elliptic curve implementation — existing
- ✓ BSGS algorithm with 3-tier bloom filter hierarchy — existing
- ✓ CUDA GPU backend for NVIDIA GPUs — existing
- ✓ OpenCL GPU backend for AMD GPUs — existing
- ✓ Multi-GPU support with performance-weighted work distribution — existing
- ✓ Distributed mode with server/client wizard — existing
- ✓ Hardware auto-detection (CPU cores, cache, RAM, SIMD features) — existing
- ✓ Parameter validation with auto-correction — existing
- ✓ Platform abstraction layer (Linux/Windows/macOS) — existing
- ✓ Structured configuration system (keyhunt_config_t) — existing (partial migration)
- ✓ CLI argument parsing with type-safe enums — existing
- ✓ Progress tracking with auto-save — existing
- ✓ Benchmark mode — existing

### Active

- [ ] All existing tests passing (fix test_point 8 failures, test_intgroup 11 failures, SHA256 test input sizes)
- [ ] Comprehensive test coverage for core crypto operations (secp256k1, hashing, bloom filters)
- [ ] Test coverage for all 6 search modes (end-to-end correctness)
- [ ] Clean memory safety (no leaks, overflows, UB under Valgrind/ASan)
- [ ] Complete config migration (eliminate remaining global variables, wire config into search modules)
- [ ] Monolithic keyhunt.cpp decomposition (extract mode dispatchers)
- [ ] CI pipeline green on Linux x86_64, Windows x64, macOS
- [ ] CUDA backend correctness verification under stress
- [ ] OpenCL backend correctness verification under stress
- [ ] Optimized hot paths with verified SIMD dispatch and no performance regressions
- [ ] P2SH and BECH32 address mode implementation (currently placeholder-only)

### Out of Scope

- New search algorithms — focus is hardening existing ones, not adding new ones
- Mobile or ARM64 platform support — Linux x86_64, Windows x64, macOS only
- GUI or web interface — CLI tool only
- Rewrite of legacy keyhunt_legacy.cpp or bsgsd.cpp — deprecate, don't rewrite
- New distributed protocol features — existing wizard server/client is sufficient

## Context

- Branch: `refactor-pipeline` — all hardening work lands here, merge to main when stable
- The codebase has been through 5+ rounds of code review and remediation (Feb 2026)
- Config system (`keyhunt_config_t`) exists but search modules still use extern globals
- Pre-existing test failures in test_point (8) and test_intgroup (11) predate current refactor
- SHA256 tests have input size mismatches (uint32_t[8] vs uint32_t[16])
- `keyhunt.cpp` is ~5300 lines combining CLI, threads, BSGS, progress, and dispatch
- `-O2` optimization required (not `-Ofast`, which causes Ubuntu freezes)
- Custom test framework in `tests/test_framework.h` (no external test dependencies)
- CMake build system added alongside Makefile during refactor

## Constraints

- **Optimization level**: Must use `-O2`, not `-Ofast` — causes system freezes on Ubuntu
- **No external crypto libs**: Main build uses custom secp256k1 (no OpenSSL/GMP dependency)
- **Backward compatibility**: Existing CLI flags and file formats must remain compatible
- **Memory budget**: BSGS mode must validate RAM before allocation to prevent OOM
- **Cross-platform**: Must compile on GCC 12+, Clang 14+, MinGW-w64, MSVC
- **GPU optional**: CPU-only builds must work without CUDA/OpenCL headers

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Work on refactor-pipeline, merge to main when stable | Isolate hardening from production users | — Pending |
| Fix existing tests before adding new ones | Can't trust new tests if foundation is broken | — Pending |
| Complete config migration before decomposing keyhunt.cpp | Config wiring enables clean extraction | — Pending |
| Target Linux + Windows + macOS (no ARM64) | Primary user base is x86_64 desktop/server | — Pending |
| Both CUDA and OpenCL backends must be verified | Users have both NVIDIA and AMD hardware | — Pending |

---
*Last updated: 2026-02-28 after initialization*
