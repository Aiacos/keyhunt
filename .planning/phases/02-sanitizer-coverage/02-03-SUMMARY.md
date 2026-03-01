---
phase: 02-sanitizer-coverage
plan: 03
subsystem: testing
tags: [asan, ubsan, sanitizer, memory-safety, undefined-behavior, bounds-check, alignment]

# Dependency graph
requires:
  - phase: 02-sanitizer-coverage/01
    provides: "Sanitizer build infrastructure (make asan/sanitize targets)"
  - phase: 02-sanitizer-coverage/02
    provides: "Volatile-to-atomic conversion eliminating data races"
provides:
  - "Zero ASan findings on full test suite with detect_leaks=1"
  - "Zero UBSan findings on full test suite"
  - "Bounds-checked Int::getBits/setBits accessors"
  - "UB-free Montgomery multiplication setup (unsigned arithmetic)"
  - "Aligned RIPEMD160 state buffer eliminating misaligned access"
affects: [02-sanitizer-coverage/04, config-migration]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Bounds-checked accessors for Knuth Algorithm D (return 0 for out-of-range digits)"
    - "Unsigned casts for intentional wrapping arithmetic in crypto code"
    - "Aligned intermediate buffers for hash functions writing to unaligned output"
    - "GSn array sizing: hLength+1 elements (function accesses GSn[hLength])"

key-files:
  created: []
  modified:
    - "src/secp256k1/Int.h"
    - "src/secp256k1/IntMod.cpp"
    - "src/hash/ripemd160.cpp"
    - "src/bsgs/bsgs_ops.h"
    - "tests/test_bsgs_ops.cpp"
    - "tests/test_fused_hash.cpp"
    - "tests/test_search_mocks.cpp"

key-decisions:
  - "getBits/setBits bounds check returns 0 for out-of-range (Knuth convention for implicit zero digits)"
  - "Newton iteration uses uint64_t (only low 64 bits matter for Montgomery constant)"
  - "SWAP_ADD/SWAP_SUB use unsigned casts: bit-level result identical but avoids signed overflow UB"
  - "ripemd160_32 uses aligned state[5] buffer then memcpy to digest (avoids misaligned uint32_t)"
  - "sqlite3 did not produce UBSan findings, no exemption needed"
  - "searchbinary mock uses 20-byte comparison matching production cmp_hash20 behavior"

patterns-established:
  - "Crypto wrapping arithmetic: always cast to uint64_t before operations that may overflow"
  - "Hash output alignment: use aligned intermediate buffer + memcpy for potentially unaligned output pointers"
  - "2-block SHA256 inputs: allocate 32 uint32_t per stream (not 16)"

requirements-completed: [MEM-01]

# Metrics
duration: 38min
completed: 2026-03-01
---

# Phase 02 Plan 03: ASan+UBSan Zero Findings Summary

**Fixed 7 source files to achieve zero ASan + zero UBSan findings: bounds-checked Int accessors, unsigned Montgomery arithmetic, aligned RIPEMD160 state, and corrected test buffer sizes**

## Performance

- **Duration:** 38 min
- **Started:** 2026-03-01T08:23:50Z
- **Completed:** 2026-03-01T09:02:37Z
- **Tasks:** 2
- **Files modified:** 7

## Accomplishments
- Achieved zero ASan findings (including detect_leaks=1) across full test suite
- Achieved zero UBSan findings: fixed signed integer overflow, left shift of negative values, and misaligned access
- Fixed 4 production code bugs: Int::getBits bounds underflow, IntMod signed overflow, ripemd160 alignment
- Fixed 3 test files: incorrect buffer sizes for BSGS, SHA256 2-block, and mock comparison width
- MEM-01 requirement satisfied: `make asan` runs clean with zero findings, no suppressions

## Task Commits

Each task was committed atomically:

1. **Task 1: Fix ASan+UBSan findings in production code** - `7d1dff3` (fix)
2. **Task 2: Fix test buffer sizes and mock for ASan-clean suite** - `ef29d40` (fix)

## Files Created/Modified
- `src/secp256k1/Int.h` - Bounds-checked getBits/setBits (return 0 for i < 0 or i >= NB32BLOCK)
- `src/secp256k1/IntMod.cpp` - Newton iteration uint64_t, SWAP_ADD/SWAP_SUB unsigned casts, nb0 unsigned add
- `src/hash/ripemd160.cpp` - Aligned state[5] buffer in ripemd160_32 with memcpy to output
- `src/bsgs/bsgs_ops.h` - Documented GSn requires hLength+1 elements
- `tests/test_bsgs_ops.cpp` - GSn arrays sized hLength+1 (was hLength, overflowing by 1)
- `tests/test_fused_hash.cpp` - 2B SHA256 inputs sized [8][32] (was [8][16], only 1 block)
- `tests/test_search_mocks.cpp` - searchbinary mock uses 20-byte comparison (was 32, reading past RMD160 buffers)

## Findings Catalog

### Round 1: STACK_BUFFER_UNDERFLOW
- **File:** src/secp256k1/Int.h:177 (getBits)
- **Caller:** Int::Div() via Int::MultModN()
- **Root cause:** Knuth Algorithm D accesses digits beyond number size; getBits had no bounds check
- **Fix:** Return 0 for index < 0 or >= NB32BLOCK

### Round 2-3: HEAP/STACK_BUFFER_OVERFLOW
- **File:** tests/test_bsgs_ops.cpp (multiple tests)
- **Root cause:** GSn arrays sized hLength instead of hLength+1; bsgs_batch_compute_points accesses GSn[hLength]
- **Fix:** Increased array sizes to hLength+1

### Round 4: UB_SIGNED_OVERFLOW (15 instances)
- **File:** src/secp256k1/IntMod.cpp:696-700 (Newton iteration)
- **Root cause:** int64_t multiplications overflow computing Montgomery constant
- **Fix:** Changed to uint64_t (only low 64 bits matter)

### Round 4: UB_SHIFT (2 instances)
- **File:** src/secp256k1/IntMod.cpp:427-428 (ModInv)
- **Root cause:** Left shift of negative int64_t values
- **Fix:** Cast to uint64_t before shift, cast back

### Round 4: UB_SIGNED_OVERFLOW (4 instances in ModInv)
- **File:** src/secp256k1/IntMod.cpp:435-443 (SWAP_ADD/SWAP_SUB macros)
- **Root cause:** int64_t addition/subtraction overflow in matrix entries
- **Fix:** Unsigned cast in SWAP macros (bit-identical result, well-defined)

### Round 4: UB_ALIGNMENT (21 instances)
- **File:** src/hash/ripemd160.cpp:32-36, 72, 242-247
- **Root cause:** ripemd160_32 casts digest (unsigned char*) to uint32_t* without alignment guarantee
- **Fix:** Use aligned uint32_t state[5] buffer, memcpy result to digest

### Round 4: STACK_USE_AFTER_RETURN
- **File:** tests/test_search_xpoint.cpp:37 (init_secp256k1_field)
- **Root cause:** InitK1 stores raw pointer to local Int; function returns, pointer dangles
- **Fix:** Already fixed in prior commit (70ff804), vars made static

### Round 5: STACK_BUFFER_OVERFLOW (searchbinary mock)
- **File:** tests/test_search_mocks.cpp:34
- **Root cause:** Mock tried 32-byte memcmp first on 20-byte RMD160 hash buffers
- **Fix:** Changed to 20-byte comparison matching production cmp_hash20

### Round 5: STACK_BUFFER_OVERFLOW (SHA256 2B tests)
- **File:** tests/test_fused_hash.cpp:219
- **Root cause:** 2-block SHA256 needs 32 uint32_t per stream; tests allocated only 16
- **Fix:** Changed input arrays to [8][32]

## Decisions Made
- getBits bounds check returns 0 for out-of-range indices (standard Knuth convention where implicit digits are zero)
- Newton's iteration changed from int64_t to uint64_t (Montgomery constant only needs low 64 bits)
- SWAP macros use unsigned casts for wrapping arithmetic (bit-level identical, no UB)
- ripemd160_32 uses memcpy from aligned buffer (portable, no `no_sanitize` needed)
- sqlite3 did not produce UBSan findings, so no exemption rule was needed
- searchbinary mock simplified to 20-byte only (matches production behavior)

## Deviations from Plan

None -- plan executed as written. All findings were discovered through iterative ASan+UBSan runs and fixed with actual code fixes (no suppressions).

## Issues Encountered
- Required 7 iterative build-fix-rebuild cycles to reach zero findings (findings cascaded: fixing one revealed others in later tests)
- The InitK1 use-after-return finding was already fixed in a prior commit (70ff804), so no additional work needed

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- `make asan` / `make sanitize` runs full test suite with zero ASan and zero UBSan findings
- All fixes are genuine code fixes (no `__attribute__((no_sanitize))` annotations added)
- No `detect_leaks=0` globally (leak detection is active)
- Ready for plan 02-04 (TSan testing)

---
*Phase: 02-sanitizer-coverage*
*Completed: 2026-03-01*
