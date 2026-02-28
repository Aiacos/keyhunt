# Testing Patterns

**Analysis Date:** 2026-02-28

## Test Framework

**Runner:**
- Custom lightweight test framework (no external dependencies)
- Framework: `tests/test_framework.h` (179 lines, self-contained)
- Supports C and C++ test code
- Color-coded output (green for pass, red for fail)

**Assertion Library:**
- Macro-based assertions in `test_framework.h` (lines 84-171)
- Core assertions: `ASSERT_TRUE()`, `ASSERT_FALSE()`, `ASSERT_EQ()`, `ASSERT_NEQ()`
- Specialized assertions: `ASSERT_STR_EQ()`, `ASSERT_MEM_EQ()`, `ASSERT_NULL()`, `ASSERT_NOT_NULL()`
- Double comparison: `ASSERT_DOUBLE_EQ(expected, actual, epsilon)`

**Run Commands:**
```bash
make test              # Build and run all tests
./run_tests            # Run test executable directly
make sanitize          # Run with AddressSanitizer (detects memory issues)
make tsan              # Run with ThreadSanitizer (detects race conditions)
make coverage          # Run with coverage instrumentation
```

## Test File Organization

**Location:**
- All tests: `tests/` directory at project root
- Compiled into single executable: `run_tests` (or `run_tests.exe` on Windows)
- Test object files: `obj/tests/` (same structure as main code)

**Naming Convention:**
- Pattern: `test_*.cpp` or `test_*.c`
- Examples:
  - `tests/test_hash.cpp` — RIPEMD160, SHA256, SHA512 implementations
  - `tests/test_int.cpp` — 256-bit integer arithmetic
  - `tests/test_point.cpp` — Elliptic curve point operations
  - `tests/test_bloom.cpp` — Bloom filter functionality
  - `tests/test_bsgs_integration.cpp` — BSGS algorithm integration
  - `tests/test_search_xpoint.cpp` — XPOINT search mode
  - `tests/test_search_rmd160.cpp` — RMD160 search mode

**Test Count:**
- ~23 test files identified (from Makefile lines 220-241)
- Comprehensive coverage of cryptographic primitives, data structures, search modes

## Test Structure

**Test Definition Pattern:**
```c
#include "test_framework.h"

TEST(test_name) {
    // Arrange: Set up test data
    unsigned char input[] = "abc";
    unsigned char expected[20];

    // Act: Call function under test
    ripemd160(input, 3, digest);

    // Assert: Verify results
    ASSERT_MEM_EQ(expected, digest, 20);
}
```

**Main Function Pattern:**
```c
int main(int argc, char *argv[]) {
    TEST_INIT();

    RUN_TEST(test_name_1);
    RUN_TEST(test_name_2);
    RUN_TEST(test_name_3);

    return TEST_RESULTS();
}
```

**Test Execution Flow:**
1. `TEST_INIT()` initializes global test counters (lines 46-51)
2. `RUN_TEST(name)` runs single test with output (lines 58-72)
3. On assertion failure: prints file/line, expected/actual, marks test failed, returns immediately
4. `TEST_RESULTS()` prints summary (pass/fail counts) and returns exit code

## Test Patterns

**Helper Functions:**
- `hex_to_bytes()` - Convert hex strings to byte arrays (common in crypto tests)
- `print_hex()` - Debug output for byte data
- Mock functions prefixed with `mock_` (see `test_search_mocks.cpp`)

**Setup/Teardown:**
- No explicit setup/teardown macros (tests are simple self-contained functions)
- Implicit cleanup: test functions return immediately on assertion failure
- Memory cleanup: Local variables on stack, `new`/`delete` rare

**Async Testing:**
- Single-threaded test framework (no async support)
- Threaded code tested via integration tests (e.g., `test_distributed.cpp`)
- Thread safety verified via ThreadSanitizer build: `make tsan`

**Error Testing:**
- Null pointer tests: `ASSERT_NULL()`, `ASSERT_NOT_NULL()`
- Out-of-bounds verification in integer tests
- Example from `test_int.cpp`: carry-over and overflow handling

**Crypto Test Vectors:**
- ISO/IEC standard test vectors (documented in code)
- Example from `test_hash.cpp` (lines 34-97):
  ```cpp
  // RIPEMD160("") = 9c1185a5c5e9fc54612808977ee8f548b2258d31
  hex_to_bytes("9c1185a5c5e9fc54612808977ee8f548b2258d31", expected, 20);
  ripemd160(input, 0, digest);
  ASSERT_MEM_EQ(expected, digest, 20);
  ```

## Mocking

**Mock Implementation Pattern:**
- Mock functions provided in `tests/test_search_mocks.cpp`
- Example: `searchbinary()` mock (lines 30-43)
  ```c
  // Mock binary search for testing
  int searchbinary(struct address_value *buffer, char *data, int64_t array_length) {
      for (int64_t i = 0; i < array_length; i++) {
          if (memcmp(buffer[i].address, data, 32) == 0) {
              return 1;  // Match found
          }
      }
      return 0;  // No match
  }
  ```

**What to Mock:**
- External file I/O (when not testing file operations directly)
- Binary search operations (simulate with linear search for correctness verification)
- System calls (via sanitizer mode tests)
- GPU operations (can be disabled with `GPU_OBJS=""`)

**What NOT to Mock:**
- Cryptographic primitives (test against known vectors instead)
- Core arithmetic (test actual Int/Point operations)
- Bloom filter operations (test with real data)
- Memory allocation (use AddressSanitizer to catch leaks)

## Fixtures and Factories

**Test Data:**
- No external fixture files (data embedded in test code)
- Helper function to generate test data:
  ```cpp
  static void hex_to_bytes(const char *hex, unsigned char *bytes, size_t len) {
      for (size_t i = 0; i < len; i++) {
          sscanf(hex + 2*i, "%2hhx", &bytes[i]);
      }
  }
  ```

**Common Test Vectors:**
- Bitcoin test data: `tests/` directory contains sample address files
  - `tests/1to32.txt` — Simple test addresses
  - `tests/125.txt` — Bitcoin puzzle 125 test
  - `tests/66.txt` — Bitcoin puzzle 66 test

**Location:**
- Test data files: `tests/` (kept outside versioned test code)
- Generated test objects: `obj/tests/`

## Coverage

**Requirements:** No explicit coverage target enforced
- Coverage instrumentation available via `make coverage`
- Builds with `-fprofile-arcs -fprofile-use`

**View Coverage:**
```bash
make coverage              # Build with coverage instrumentation
./run_tests               # Run tests to generate coverage data
lcov -c -d obj -o coverage.info  # Collect coverage
lcov -l coverage.info     # View coverage report
```

## Test Types

**Unit Tests:**
- Scope: Individual functions and classes
- Approach: Test via public API with known test vectors
- Examples:
  - `test_int.cpp`: `Int::Add()`, `Int::Mult()`, `Int::IsZero()`
  - `test_hash.cpp`: `ripemd160()`, `sha256()` against ISO vectors
  - `test_point.cpp`: `Point::Set()`, `Point::Clear()`

**Integration Tests:**
- Scope: Multi-component interactions
- Approach: Test end-to-end workflows
- Examples:
  - `test_bsgs_integration.cpp`: BSGS algorithm with real bloom filters
  - `test_search_xpoint.cpp`: XPOINT mode with search matching
  - `test_distributed.cpp`: Coordinator + worker communication
  - `test_gpu_backend.cpp`: GPU initialization and kernel execution

**E2E Tests:**
- Framework: Shell scripts (not C/C++ test framework)
- Location: `tests/` directory (*.sh, *.md files)
- Approach: Run compiled keyhunt binary with real test data
- Example commands:
  ```bash
  ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF
  ./keyhunt -m bsgs -f tests/125.txt -b 125 -q -s 10 -R
  ./keyhunt -m xpoint -f tests/120.txt -t 4 -b 125 -R -q
  ```

## Sanitizer Builds

**AddressSanitizer (ASAN):**
```bash
make sanitize          # Build and run with AddressSanitizer
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 ./run_tests_asan
```
- Detects: Memory leaks, use-after-free, buffer overflows
- Creates separate build in `obj_asan/`
- Output: All memory errors reported with backtrace

**ThreadSanitizer (TSAN):**
```bash
make tsan              # Build and run with ThreadSanitizer
./run_tests_tsan
```
- Detects: Data races, deadlocks, synchronization issues
- Creates separate build in `obj_tsan/`
- Reports on all thread safety violations

**Flags Used:**
```makefile
ASAN_FLAGS = -fsanitize=address -fno-omit-frame-pointer -g
ASAN_LDFLAGS = -fsanitize=address
TSAN_FLAGS = -fsanitize=thread -g
TSAN_LDFLAGS = -fsanitize=thread
```

## Known Test Failures

**Pre-existing issues (documented in MEMORY.md):**
- `test_point`: 8 failures (elliptic curve point operations)
- `test_intgroup`: 11 failures (modular inverse batch operations)

**Root Causes:**
- Complex projective coordinate transformations
- Montgomery inverse algorithm edge cases
- Not critical to current functionality (workarounds exist)

**Running tests despite failures:**
```bash
make test 2>&1 | tee test_output.log  # Capture output for analysis
```

## Test Execution Targets

**Makefile Targets (from lines 294-350):**
- `test` — Default target, runs all tests
- `run_tests` — Explicit test runner build target
- `sanitize` — AddressSanitizer with leak detection
- `tsan` — ThreadSanitizer for data races
- `coverage` — Code coverage instrumentation

**Test Objects Built:**
```makefile
TEST_INT_OBJ := $(TEST_OBJDIR)/test_int.o
TEST_HASH_OBJ := $(TEST_OBJDIR)/test_hash.o
TEST_POINT_OBJ := $(TEST_OBJDIR)/test_point.o
TEST_INTGROUP_OBJ := $(TEST_OBJDIR)/test_intgroup.o
TEST_BSGS_OBJ := $(TEST_OBJDIR)/test_bsgs_integration.o
TEST_BLOOM_OBJ := $(TEST_OBJDIR)/test_bloom.o
# ... + 17 more
```

**Test Shared Objects:**
- All tests link against: `SECP256K1_OBJS`, `BLOOM_OBJS`, `HASH_OBJS`, etc.
- Total of ~10 shared object groups linked into test executable
- GPU tests link `GPU_BACKEND_OBJS` for GPU backend testing

## Quick Test Reference

**Run all tests with color output:**
```bash
make test
```

**Run single test category (by grep):**
```bash
./run_tests 2>&1 | grep "int_"       # Run Int class tests
./run_tests 2>&1 | grep "hash_"      # Run hash tests
```

**Run with memory checking:**
```bash
make sanitize          # Memory + leak detection
make tsan              # Thread safety checks
make coverage          # Coverage analysis
```

**Debug test failure:**
```bash
# Build test executable with debug symbols
make clean && make test CXXFLAGS="-g -O0"

# Run under gdb
gdb ./run_tests
(gdb) run
(gdb) bt    # Print backtrace on crash
```

**Test specific functionality:**
```bash
# Build and run only crypto tests
make clean
make obj/tests/test_hash.o obj/tests/test_int.o # Build specific tests
# Then relink run_tests target
```

---

*Testing analysis: 2026-02-28*
