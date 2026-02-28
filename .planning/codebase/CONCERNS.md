# Codebase Concerns

**Analysis Date:** 2026-02-28

## Tech Debt

**Global Variables Still Coexist With Structured Config:**
- Issue: The codebase maintains 50+ global variables (e.g., `FLAGMODE`, `NTHREADS`, `FLAGDEBUG`) in parallel with the new structured configuration system in `src/config/config.h`
- Files: `src/keyhunt.cpp`, `src/search/search_address.cpp`, `src/search/search_bsgs_threads.cpp`, `src/search/search_*.cpp`
- Impact: Increases maintenance burden, creates risk of configuration drift where some code reads new config and other code reads legacy globals. Thread safety issues around mutable global state.
- Fix approach: Complete migration to structured `keyhunt_config_t` config parameter. Eliminate all legacy globals by passing config through function signatures to all search modules. This is partially documented in `MIGRATION_GUIDE.md`.

**Incomplete Config Wiring in Search Modules:**
- Issue: `src/config/config.h` defines the new config structure, but search modules (`search_address.cpp`, `search_vanity.cpp`, `search_minikeys.cpp`, `search_bsgs_threads.cpp`) continue using `extern` global variables instead of accepting config as parameter
- Files: `src/search/search_address.cpp:52-88`, `src/search/search_common.h:112`, `src/keyhunt.cpp:745`, `src/keyhunt.cpp:775`
- Impact: Code comments explicitly mark this as "TODO: Accept config parameter instead of using extern globals", preventing unit testing and creating tight coupling to global state
- Fix approach: Add `keyhunt_config_t *config` parameter to all search functions, replace `extern` reads with `config->field` accesses. Start with `search_address.cpp` as highest-traffic code path.

**Monolithic keyhunt.cpp:**
- Issue: Main file is 5,323 lines, combining CLI parsing, thread management, BSGS logic, progress reporting, and search mode dispatching
- Files: `src/keyhunt.cpp`
- Impact: Difficult to navigate, test, and modify. High coupling between unrelated concerns.
- Fix approach: Extract mode-specific entry points to `src/modes/` subdirectory (e.g., `address_mode.cpp`, `bsgs_mode.cpp`). Create thin dispatcher in `keyhunt.cpp`.

**Duplicate/Legacy Implementations:**
- Issue: `src/keyhunt_legacy.cpp` (6,719 lines) and `src/bsgsd.cpp` (2,313 lines) duplicate significant core logic from `keyhunt.cpp`. Legacy version has different SIMD implementations (SSE2 only).
- Files: `src/keyhunt_legacy.cpp`, `src/bsgsd.cpp`
- Impact: Three separate code paths mean bugs in one don't get fixed in others. Maintenance nightmare. Legacy version is out-of-date but still present.
- Fix approach: Mark both files as deprecated in code comments. Create single unified entry point. Legacy code should only be reachable via `--legacy` flag if absolutely needed for compatibility.

---

## Known Bugs

**P2SH and BECH32 Address Modes Not Implemented:**
- Symptoms: The codebase has placeholders but returns zero output hashes for P2SH (Segwit compatibility) and BECH32 (native Segwit) modes
- Files: `src/secp256k1/SECP256K1.cpp` (6 instances of "P2SH mode not implemented"), `src/gmp256k1/GMP256K1.cpp` (2 instances)
- Trigger: Attempting to search for P2SH addresses (Bitcoin Script Hash) or BECH32 addresses results in silently skipped outputs
- Workaround: Use `-l uncompressed` or `-l compressed` which maps to standard P2PKH addresses (legacy format)
- Impact: Users cannot search for Segwit addresses despite the flag being present in the CLI

**Test Failures in Point and IntGroup Tests:**
- Symptoms: Known pre-existing test failures in `test_point.cpp` (8 failures) and `test_intgroup.cpp` (11 failures)
- Files: `tests/test_point.cpp`, `tests/test_intgroup.cpp`
- Trigger: Running `make test` or test executable directly
- Impact: Indicates potential issues in secp256k1 elliptic curve point arithmetic and batch modular inversion. These failures predate current refactor branch and may indicate subtle bugs in ECC operations that are performance-critical.
- Fix approach: Investigate whether these are regression tests for old bugs or actual broken functionality. Tests may be checking for deprecated behavior that changed with recent commits.

**SHA256 Test Input Size Mismatch:**
- Symptoms: SHA256 tests in `test_hash.cpp` have incorrect input size specifications
- Files: `tests/test_hash.cpp:433`, `tests/test_hash.cpp:476`, `tests/test_hash.cpp:522`
- Trigger: Tests are marked with "TODO: Fix this test - input size should be uint32_t[16] not uint32_t[8]"
- Impact: Hash correctness not fully validated for 256-bit inputs. Tests may pass with wrong data structure sizes.
- Fix approach: Correct test inputs to use `uint32_t[16]` (512-bit buffer for SHA256 padding) instead of `uint32_t[8]`.

---

## Security Considerations

**Shell Injection in Distributed Mode:**
- Risk: The distributed coordinator or client modes may construct system commands or shell invocations without proper escaping
- Files: `src/distributed/distributed.c` (4,425 lines) - needs audit
- Current mitigation: Platform abstraction layer prevents most shell access, but JSON parsing from untrusted pool servers needs careful validation
- Recommendations: Add shell escape/quoting utilities for any system() calls. Validate all JSON from remote coordinator. Consider sandboxing subprocess execution.

**Bloom Filter File Integrity Not Cryptographically Verified:**
- Risk: BSGS mode caches bloom filters to disk. Files are checksummed but not signed/authenticated. Corrupted or malicious bloom files could cause silent misses (not detecting valid keys).
- Files: `src/keyhunt.cpp` (BSGS bloom filter I/O), `src/bloom/bloom.cpp`
- Current mitigation: CRC32 checksum validation on load
- Recommendations: Add HMAC-SHA256 signatures for cache files. Include file format version and creation timestamp to detect stale caches across upgrades.

**GPU Memory Not Always Validated Before Allocation:**
- Risk: GPU backend allocates device memory without checking available VRAM. OOM on GPU fails silently in some code paths.
- Files: `src/gpu/gpu_backend_cuda.cu:2053-2055` (cudaMalloc), `src/gpu/gpu_backend_opencl.c`
- Current mitigation: CUDA_CHECK macro will catch malloc failures, but error handling may not gracefully degrade
- Recommendations: Pre-query available GPU memory before allocation. Implement adaptive batch sizing if VRAM is insufficient.

---

## Performance Bottlenecks

**Bloom Filter Lookup Latency in Hybrid (CPU+GPU) Mode:**
- Problem: In hybrid mode, CPU threads perform sequential lookups in main bloom filter while GPU processes batches. Bloom filter has 3-tier hierarchy (bloom1/bloom2/bloom3) requiring up to 3 sequential memory lookups per candidate.
- Files: `src/search/search_address.cpp` (bloom check loop), `src/bloom/bloom_simd.cpp` (batch bloom)
- Cause: CPU doesn't prefetch bloom accesses in tight inner loops, causing L1/L2 cache misses for every 20-50 keys checked
- Improvement path: Add explicit software prefetching via `_mm_prefetch()` before bloom lookups. Batch 64+ bloom checks to amortize latency. Consider SIMD vectorization of bloom check itself (already exists but may not be in hot path).

**GPU-CPU Synchronization Overhead:**
- Problem: Hybrid mode requires frequent synchronization between GPU kernel completion and CPU thread work distribution. Each GPU batch completion triggers atomic updates across CPU threads.
- Files: `src/gpu/gpu_backend_cuda.cu` (g_gpu_keys_checked atomics), `src/keyhunt.cpp:771-785` (aggregation logic)
- Cause: Release/acquire memory ordering on every progress update. No batching of aggregation.
- Improvement path: Implement "lazy aggregation" - allow GPU counter to lag by N ms, then batch-aggregate every 100ms instead of per-batch. Reduces atomic contention.

**BSGS Memory Validation Has No Caching:**
- Problem: BSGS mode validates available RAM on startup but doesn't account for memory fragmentation or other processes' allocation changes during long searches
- Files: `src/keyhunt.cpp:1637-1716` (memory check formula)
- Cause: Single validation at startup. Over multi-week searches, system RAM availability changes.
- Improvement path: Periodic RAM re-check (e.g., every 24 hours). Alert user if available RAM drops below safety threshold. Implement preallocation of full BSGS tables upfront to detect OOM early.

**CPU GCD/Stride Calculations Not SIMD-Optimized:**
- Problem: Range calculation logic for striped/custom stride uses scalar operations. For large N values with big strides, this becomes a bottleneck in initialization.
- Files: `src/secp256k1/Int.cpp` (big integer arithmetic)
- Cause: `Int::Div()` and modular operations use scalar-only implementations for GCD/Euclid algorithm
- Improvement path: Implement AVX2 batch GCD for multiple range calculations in parallel. Not critical unless using exotic stride patterns.

---

## Fragile Areas

**secp256k1 Elliptic Curve Point Arithmetic:**
- Files: `src/secp256k1/Point.cpp`, `src/secp256k1/Int.cpp`, `src/secp256k1/IntGroup.cpp`
- Why fragile: These are the mathematical core of the tool. Small bugs in point addition, doubling, or inversion cause all searches to miss keys. Tests exist but have pre-existing failures (`test_point.cpp:8 failures`, `test_intgroup.cpp:11 failures`).
- Safe modification:
  - Always run full test suite before committing changes
  - Add property-based tests (fuzzing) to catch mathematical errors
  - Compare results against OpenSSL's BIGNUM implementation for validation
  - Document invariants explicitly (e.g., "points must be in affine form after inversion")
- Test coverage: Incomplete - `test_point.cpp` and `test_intgroup.cpp` have known failures. Add constant-time tests to ensure no side-channel timing leaks.

**BSGS Bloom Filter Three-Tier System:**
- Files: `src/keyhunt.cpp:3140-3200` (bloom allocation), `src/bloom/bloom.cpp`, `src/bsgs/bsgs_ops.cpp`
- Why fragile: Three separate bloom filters (bloom1/2/3) with decreasing size must maintain consistency. If one level is corrupted, false negatives cascade. Format version embedded in file headers - format changes break old cache files.
- Safe modification:
  - Changes to bloom filter size calculations must update memory check in `keyhunt.cpp:1637`
  - File format version must increment if structure changes
  - Add migration code if format version changes (read old, convert, write new)
  - Validate all three blooms have consistent counts before using
- Test coverage: `tests/test_bloom.cpp` exists but doesn't test three-tier consistency or file persistence.

**Multi-GPU Scheduler and Work Distribution:**
- Files: `src/gpu/gpu_backend_cuda.cu:50-87` (gpu_context_t), `src/gpu/multi_gpu_scheduler.c`, `src/gpu/gpu_multi_worker.c`
- Why fragile: Complex state machine managing per-GPU contexts, stream scheduling, and work allocation across heterogeneous GPUs. Load balancing weights are hardcoded tuning values.
- Safe modification:
  - Any change to GPU context initialization must update both CUDA and OpenCL backends
  - Stream allocation/deallocation must be paired (create and destroy in matching order)
  - Test on systems with 2-8 GPUs; single-GPU and no-GPU paths must not regress
  - Add verbose debug logging (KEYHUNT_GPU_DEBUG=1) for troubleshooting
- Test coverage: `tests/test_multi_gpu_integration.cpp` exists but only tests specific scenarios. No stress tests for long-duration multi-GPU runs.

**Distributed Coordinator State Machine:**
- Files: `src/distributed/distributed.c` (3,554 lines), `src/wizard/wizard_server.c`, `src/wizard/wizard_client.c`
- Why fragile: Manages worker connections, work unit distribution, progress aggregation, and failover. State transitions must be atomic with respect to protocol messages.
- Safe modification:
  - Protocol messages are versioned. Bumping version breaks old worker compatibility.
  - Worker heartbeats and timeouts are hardcoded (need to be configurable)
  - No transaction log for crash recovery - if coordinator dies mid-allocation, work is lost
  - Test on lossy networks (simulate packet loss, latency, worker crashes)
  - Document all state transitions in comments
- Test coverage: `tests/test_distributed.cpp` exists. Add chaos engineering tests with random worker failures.

---

## Scaling Limits

**Maximum Bloom Filter Size (~1TB):**
- Current capacity: Single BSGS search can use up to 1TB of RAM for bloom filters (with K factor adjustments)
- Limit: Beyond ~1TB, memory allocation fails on most systems. BSGS with N > 2^40 becomes memory-limited rather than time-limited.
- Scaling path: Implement on-disk bloom filters with memory-mapped I/O, or distributed bloom filter across multiple machines.

**Single Machine Coordination Bottleneck:**
- Current capacity: Distributed mode's coordinator can handle ~1000 concurrent worker connections (limited by select()/poll() file descriptor management and socket buffer sizes)
- Limit: With proper error recovery and work scheduling, 1000+ workers saturate network bandwidth on single coordinator
- Scaling path: Implement hierarchical coordinator architecture (coordinator of coordinators). Add work-stealing scheduler to balance load without central coordination.

**GPU Memory Fragmentation Over Time:**
- Current capacity: Long-running GPU searches allocate/deallocate buffers repeatedly, leading to fragmentation. After hours of operation, available contiguous VRAM may drop significantly.
- Limit: Cannot allocate new buffers even if total VRAM is available, causing out-of-memory crashes
- Scaling path: Pre-allocate maximum required buffers upfront. Implement memory pooling allocator instead of raw CUDA malloc/free.

**File I/O Performance for BSGS Cache:**
- Current capacity: Loading 500GB bloom filter cache files from disk is done sequentially. Modern NVMe SSDs can do 7GB/s but code does single-threaded I/O.
- Limit: On systems with mechanical HDD, BSGS initialization can take 30+ minutes
- Scaling path: Parallelize bloom filter file I/O across 4-8 threads. Implement memory-mapped file access for instant "loading".

---

## Dependencies at Risk

**Legacy SHA512 and SHA512-AVX2 Not Used by Default:**
- Risk: `src/hash/sha512.cpp` and `src/hash/sha512_avx2.cpp` exist but are not integrated into main search code path. If HD wallet key derivation or extended key formats are added, code may be incomplete.
- Files: `src/hash/sha512_avx2.cpp:444`, `src/hash/sha512_avx512.cpp:374` (marked "TODO: Variable-length API")
- Impact: Any feature depending on SHA512 will have untested, incomplete code
- Migration plan: Complete SHA512 API (variable length, HMAC). Add integration tests that verify against OpenSSL. Consider deprecating in favor of single SHA256-based KDF.

**OpenCL Backend May Fall Behind CUDA Optimizations:**
- Risk: GPU acceleration has parallel CUDA and OpenCL backends. CUDA receives more testing and features. OpenCL code may diverge.
- Files: `src/gpu/gpu_backend_opencl.c`, `src/gpu/gpu_backend_cuda.cu`
- Impact: OpenCL devices (AMD GPUs) may have stale algorithms or missing optimizations
- Migration plan: Unify kernel implementations where possible. Use unified C++ template GPU backend that compiles to both CUDA and OpenCL PTX. Currently they are separate implementations.

**Wizard Distributed Mode Not Fully Integrated:**
- Risk: Wizard mode (`src/wizard/wizard.c` and related files) provides interactive distributed puzzle solving but may not interoperate correctly with CLI-based mode
- Files: `src/wizard/wizard.c`, `src/wizard/wizard_server.c`, `src/wizard/wizard_config.c`
- Impact: Users may run wizard in one session and CLI in another, resulting in duplicate work or missed ranges
- Migration plan: Unify work persistence format between wizard and CLI modes. Store in common `~/.keyhunt/` directory with locking.

---

## Missing Critical Features

**No Automatic Checkpoint/Resume for Long Searches:**
- Problem: If keyhunt crashes or is interrupted (after 72+ hour searches), progress is lost. Only BSGS with file caching has persistence.
- Blocks: Users cannot safely run unattended long-duration searches
- Recommendation: Implement progress snapshots (every 1 hour) to `~/.keyhunt/progress/`. Resume seamlessly on restart with `--resume` flag.

**No Real-Time Progress Visualization:**
- Problem: Progress output is sampled every N seconds and printed to terminal. No graphical dashboard despite wizard mode having interactive UI.
- Blocks: Users cannot monitor large distributed clusters
- Recommendation: Add prometheus metrics export (`--metrics-port 9090`) so Grafana can visualize real-time progress, throughput, and GPU utilization.

**BSGS Cannot Use Multiple Files for Point Table:**
- Problem: BSGS baby step point table is a single monolithic file. If table is 500GB and disk is 1TB, cannot create second table for different K factor even on different machine.
- Blocks: Distributed BSGS across multiple machines
- Recommendation: Split point table across multiple numbered files (bP.0, bP.1, ...). Coordinator can assign different table ranges to different workers.

**No Input Validation for Address Files:**
- Problem: If target address file contains duplicates, corrupted data, or invalid formats, no explicit error reporting. Program may silently skip malformed entries.
- Blocks: Debugging why expected addresses aren't found
- Recommendation: Add `--validate-targets` flag that parses entire file, reports duplicates and format errors with line numbers.

---

## Test Coverage Gaps

**BSGS Distributed Coordinator Has No Failover Tests:**
- What's not tested: Coordinator crashes mid-work-allocation. Multiple coordinators with overlapping ranges. Worker reconnection after network partition.
- Files: `src/distributed/distributed.c`, `tests/test_distributed.cpp`
- Risk: Distributed puzzles may have silent duplicate or missing work
- Priority: High - distributed mode is advertised feature but untested for fault tolerance

**GPU Backend Has No Stress Tests:**
- What's not tested: Long-running (24+ hours) GPU kernels. Memory allocation/deallocation cycles. Multi-GPU load balancing under unequal device speeds.
- Files: `src/gpu/gpu_backend_cuda.cu`, `tests/test_multi_gpu_integration.cpp`
- Risk: GPU backend may have memory leaks or incorrect work distribution that only manifests over days
- Priority: High - GPU is primary performance path

**No Negative Tests for Invalid Ranges/Parameters:**
- What's not tested: Invalid bit ranges (e.g., `--bits 300` on 256-bit curve). Negative stride values. Zero K factor. Empty address files.
- Files: `src/keyhunt.cpp` (parameter parsing), `tests/` (test suite)
- Risk: Users may set invalid configs that fail silently or hang
- Priority: Medium - affects user experience but not correctness

**Hash Pipeline Integration Not Tested End-to-End:**
- What's not tested: Full path: random key → EC multiply → SHA256 → RIPEMD160 → bloom check. Only individual hash functions tested separately.
- Files: `src/search/search_address.cpp` (inner loop), `tests/test_hash.cpp`
- Risk: Integration bugs between hash stages (e.g., buffer size mismatches) only discovered in production
- Priority: High - core functionality

**Wizard Configuration Persistence Not Tested:**
- What's not tested: Save config → exit → restart → resume searches. Config file corruption. Version migration.
- Files: `src/wizard/wizard_config.c`, `tests/test_wizard.cpp`
- Risk: Wizard data loss on restart
- Priority: Medium - impacts user convenience

---

## Architectural Issues

**C and C++ Boundary Lacks Clear Ownership:**
- Issue: Core search logic is C++ (`src/keyhunt.cpp`, `src/search/*.cpp`) while GPU backend, distributed coordinator, and platform abstraction are C (`src/gpu/gpu_backend_opencl.c`, `src/distributed/distributed.c`, `src/platform/platform_*.c`)
- Files: Mixed C/C++ throughout
- Impact: ABI incompatibilities, unnecessary `extern "C"` blocks, difficulty linking C code with C++ STL
- Fix approach: Standardize on C++ for all new code. Gradually convert C modules to C++ as they undergo maintenance. Provide thin C wrappers only where absolutely necessary (e.g., OpenCL kernel launches).

**Config Structure Not Thread-Safe During Mutation:**
- Issue: `src/config/config.h` defines `keyhunt_config_t` as plain C structs with `volatile` fields. Volatile is not atomic. Multiple threads reading while one thread writes = race condition.
- Files: `src/config/config.h` (gpu_config_t has volatile fields), `src/keyhunt.cpp` (updates during execution)
- Impact: GPU progress counters may be read as torn/inconsistent values on weak-memory platforms
- Fix approach: Replace all `volatile` with `std::atomic<T>`. Ensure config initialization completes before thread spawn. Mark config as const after init.

**No Central Error/Exception Handling Strategy:**
- Issue: Different modules use different error reporting: some return error codes, some `fprintf(stderr)`, some exit(), some use exceptions
- Files: Throughout codebase
- Impact: Hard to distinguish fatal errors from warnings. No way to programmatically handle errors in library mode.
- Fix approach: Define standard error codes in `src/error/error_codes.h`. Create `keyhunt_error_t` union type with code + message. All functions return this type.

---

## Summary Table

| Concern | Severity | Files | Impact | Effort to Fix |
|---------|----------|-------|--------|---------------|
| Global Variables Coexist with Config | High | keyhunt.cpp, search/*.cpp | Maintainability, thread safety | 2 weeks |
| Incomplete Config Wiring | High | search_address.cpp, search_*.cpp | Testability, coupling | 3 weeks |
| Monolithic keyhunt.cpp | Medium | keyhunt.cpp | Navigation, testing | 2 weeks |
| P2SH/BECH32 Not Implemented | High | SECP256K1.cpp | Missing features | 1 week |
| Test Failures (Point/IntGroup) | Medium | test_point.cpp, test_intgroup.cpp | Correctness unclear | 1-2 weeks |
| Bloom Filter File Integrity | Medium | keyhunt.cpp, bloom.cpp | Data integrity risk | 3 days |
| Distributed Coordinator State | High | distributed.c, wizard_server.c | Reliability | 2 weeks |
| GPU Memory Fragmentation | Medium | gpu_backend_cuda.cu | Long-running stability | 1 week |
| Multi-GPU Load Imbalance | Medium | gpu_backend_cuda.cu, multi_gpu_scheduler.c | Performance under load | 3 days |
| No Checkpoint/Resume | Medium | keyhunt.cpp | Usability | 1 week |
| Bloom Filter Lookup Latency | Low | search_address.cpp | Performance optimization | 3 days |

---

*Concerns audit: 2026-02-28*
