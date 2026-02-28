# Phase 1: Test Baseline - Context

**Gathered:** 2026-02-28
**Status:** Ready for planning

<domain>
## Phase Boundary

Fix all 19 pre-existing test failures (test_point ×8, test_intgroup ×11) and SHA256 input size mismatches. Add NIST/IETF crypto test vectors. Create end-to-end correctness tests for all 6 search modes. Establish 80% coverage gate on crypto paths. No new search modes or features.

</domain>

<decisions>
## Implementation Decisions

### Failure Strategy
- Investigate each of the 19 failures individually to determine root cause before deciding fix approach
- Per-case decision: fix the test (if testing deprecated behavior) or fix the code (if test is the spec)
- Very cautious with ECC math changes — validate against bitcoin-core/secp256k1 reference implementation before modifying any point arithmetic or modular inversion code
- Never "fix" a test by just changing expected values without understanding why they differ

### Reference Vectors
- Use NIST FIPS 180-4 test vectors for SHA256 (authoritative standard)
- Use IETF RFC for RIPEMD160 test vectors
- Use bitcoin-core/secp256k1 test vectors for EC point operations (addition, doubling, scalar multiplication, batch inversion)
- Use real Bitcoin puzzle addresses as integration test vectors for E2E mode tests
- SHA256 uint32_t[8] vs uint32_t[16] mismatch: investigate both implementation and test to determine which side has the wrong assumption, then fix accordingly

### E2E Test Design
- Run ./keyhunt as subprocess with known inputs for each of the 6 modes
- Check stdout for expected found key output
- Known test cases: use addresses/keys from tests/1to32.txt and Bitcoin puzzle known solutions
- Each mode test: ADDRESS, BSGS, XPOINT, RMD160, VANITY, MINIKEYS

### Coverage Approach
- Use gcovr with --filter for src/secp256k1/ and src/hash/ directories
- 80% line coverage threshold on crypto paths
- Add `make coverage` target if not already functional

### Claude's Discretion
- Exact gcovr configuration and reporting format
- Whether to add new test files or extend existing ones
- Test organization within the custom framework
- How to handle MINIKEYS E2E test (may need synthetic test data)

</decisions>

<specifics>
## Specific Ideas

- Bitcoin puzzle addresses provide real-world integration test vectors
- test_point.cpp and test_intgroup.cpp failures predate the refactor branch — may be long-standing bugs in ECC operations
- SHA256 tests have TODO comments marking the input size issue (test_hash.cpp lines 433, 476, 522)

</specifics>

<code_context>
## Existing Code Insights

### Reusable Assets
- `tests/test_framework.h`: Custom test framework with ASSERT_TRUE, ASSERT_MEM_EQ, ASSERT_EQ macros (179 lines)
- `tests/1to32.txt`: Known key-address pairs for ADDRESS mode testing
- `tests/125.txt`, `tests/66.rmd`, `tests/120.txt`: Mode-specific test input files
- 22 existing test files covering hash, int, point, bloom, BSGS, search modes

### Established Patterns
- TEST() macro defines test functions, RUN_TEST() registers them in main()
- Color-coded output (green pass, red fail)
- Makefile targets: `make test`, `make sanitize`, `make tsan`, `make coverage`
- Test objects built to `obj/tests/` directory

### Integration Points
- `run_tests` single executable compiles all test files
- Makefile `TEST_OBJS` list (lines 220-241) controls which tests are included
- SIMD tests conditionally compiled based on CPU feature detection

</code_context>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 01-test-baseline*
*Context gathered: 2026-02-28*
