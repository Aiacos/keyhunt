---
phase: 01-test-baseline
plan: 03
subsystem: testing
tags: [e2e-tests, shell-script, address-mode, bsgs-mode, xpoint-mode, subprocess-testing]

# Dependency graph
requires:
  - phase: 01-01
    provides: "Green unit test baseline and working keyhunt binary"
provides:
  - "E2E subprocess tests for ADDRESS, BSGS, and XPOINT search modes"
  - "make test-e2e target for running E2E tests independently"
  - "make test-all target combining unit tests and E2E tests"
affects: [01-04-PLAN, phase-2-sanitizer]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "E2E testing via subprocess execution with KEYFOUNDKEYFOUND.txt output verification"
    - "BSGS test requires N with exact sqrt divisible by 1024, K >= 128, range >= N"
    - "Bitcoin puzzle known solutions as E2E test vectors (puzzle #21 for BSGS)"

key-files:
  created:
    - "tests/test_e2e_modes.sh"
  modified:
    - "Makefile"

key-decisions:
  - "Used Bitcoin puzzle #21 (privkey 0x1BA534) for BSGS test instead of plan's suggested privkey 5, because BSGS requires minimum N/K values that make small ranges unusable"
  - "Used puzzle key 7's x-coordinate for XPOINT test as originally planned, verified against secp256k1 generator"
  - "Kept test-all target dependent on make test even though unit test build has pre-existing linker issue from Plan 01-02"

patterns-established:
  - "E2E tests run keyhunt as subprocess, check KEYFOUNDKEYFOUND.txt for expected output"
  - "BSGS tests need -b (bit range), -n (exact-sqrt N), -k (min 128) parameters carefully aligned"
  - "tests/1to32.txt contains Bitcoin puzzle addresses, NOT sequential private keys 1-32"

requirements-completed: [TEST-07, TEST-08, TEST-09]

# Metrics
duration: 13min
completed: 2026-03-01
---

# Phase 1 Plan 03: E2E Mode Tests Summary

**Shell-based E2E subprocess tests for ADDRESS, BSGS, and XPOINT modes using known Bitcoin puzzle solutions as test vectors**

## Performance

- **Duration:** 13 min
- **Started:** 2026-02-28T23:42:23Z
- **Completed:** 2026-02-28T23:56:18Z
- **Tasks:** 2
- **Files modified:** 2

## Accomplishments

- Created comprehensive E2E test script (165 lines) testing all 3 major search modes
- ADDRESS mode test verifies private key 1 discovery via address matching in KEYFOUNDKEYFOUND.txt
- BSGS mode test finds puzzle #21 (privkey 0x1BA534) from its compressed public key in 21-bit range
- XPOINT mode test finds private key 7 from its x-coordinate in range 1:F
- Added `make test-e2e` and `make test-all` Makefile targets

## Task Commits

Each task was committed atomically:

1. **Task 1: Create E2E shell script tests** - `a4a102d` (test)
2. **Task 2: Add test-e2e Makefile target** - `9bc1c4d` (chore)

## Files Created/Modified

- `tests/test_e2e_modes.sh` - E2E test script for ADDRESS, BSGS, XPOINT modes (165 lines)
- `Makefile` - Added `test-e2e`, `test-all` targets and `.PHONY` declarations

## Decisions Made

1. **Used puzzle #21 for BSGS test instead of privkey 5**: The plan suggested using private key 5's public key for the BSGS test, but BSGS mode has minimum parameter requirements (N must have exact square root divisible by 1024, K factor auto-corrects to min 128, range must be >= N). These constraints make it impossible to search a tiny range containing key 5 (which is at position 0x5). Instead, used Bitcoin puzzle #21 (private key 0x1BA534 = 1811764) with -b 21 flag, which provides a properly-sized 21-bit search range (2^20 to 2^21) compatible with N=0x100000, K=128.

2. **Discovered tests/1to32.txt contains puzzle addresses, not sequential keys**: The file does NOT contain addresses for private keys 1-32 in order. It contains addresses corresponding to Bitcoin puzzle solutions (key 1 at 0x1, key 2 at 0x3, key 3 at 0x7, key 4 at 0x8, etc.). This affected BSGS test vector selection.

3. **BSGS output format differs from ADDRESS/XPOINT**: BSGS writes "Key found privkey [hex]" while ADDRESS/XPOINT write "Private Key: [decimal/hex]". Tests use appropriate grep patterns for each mode.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Adjusted BSGS test vectors for algorithm constraints**
- **Found during:** Task 1
- **Issue:** Plan specified using private key 5's public key with range 1:F for BSGS test. BSGS mode requires N >= (1024)^2, K >= 128, and range_diff >= N. A range of 1:F (14 values) is vastly smaller than the minimum N, causing "the given range is small" error.
- **Fix:** Used Bitcoin puzzle #21 (known private key 0x1BA534) with -b 21 flag (21-bit range = 2^20 to 2^21) and N=0x100000, K=128. This satisfies all BSGS parameter constraints.
- **Files modified:** tests/test_e2e_modes.sh
- **Verification:** BSGS test finds "Key found privkey 1ba534" in KEYFOUNDKEYFOUND.txt
- **Committed in:** a4a102d

---

**Total deviations:** 1 auto-fixed (Rule 3 - blocking)
**Impact on plan:** Essential fix -- BSGS mode cannot operate with the plan's suggested parameters. Test still validates the full BSGS pipeline (public key input, bloom filter precomputation, baby-step/giant-step search, key output).

## Issues Encountered

- **Pre-existing test build linker error**: `make test` fails on clean build due to undefined references to `rmd160_check_single` and related functions. This is from Plan 01-02 adding `test_search_rmd160.cpp` without linking the required objects. Logged in `deferred-items.md`. Does not affect `make test-e2e` which works independently.
- **keyhunt binary disappearing between commands**: The binary was deleted between separate shell invocations, likely due to sandbox cleanup. Resolved by using compound commands or rebuilding as needed.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- E2E test infrastructure established with `make test-e2e`
- ADDRESS, BSGS, and XPOINT modes verified end-to-end with known solutions
- Ready for Plan 01-04 (if any) or Phase 2 work
- Pre-existing test build linker issue needs resolution before `make test-all` can work

## Self-Check: PASSED

- tests/test_e2e_modes.sh: FOUND (165 lines, min 80)
- 01-03-SUMMARY.md: FOUND
- Commit a4a102d: FOUND
- Commit 9bc1c4d: FOUND
- test-e2e target in Makefile: FOUND
- test-all target in Makefile: FOUND
- `make test-e2e` exit code: 0 (3 tests passed)

---
*Phase: 01-test-baseline*
*Completed: 2026-03-01*
