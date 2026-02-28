# Testing Guide

This document covers testing keyhunt for correctness and performance.

## Quick Validation

### Basic Functionality Test

```bash
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -t 4
```

Expected: Finds 32 private keys quickly. All keys from 0x1 to 0x20 should be found.

### Mode-Specific Tests

```bash
# ADDRESS mode
./keyhunt -m address -f tests/66.txt -b 66 -R -q -s 10

# BSGS mode
./keyhunt -m bsgs -f tests/125.txt -b 125 -q -s 10 -R

# RMD160 mode
./keyhunt -m rmd160 -f tests/66.rmd -b 66 -l compress -R -q

# XPOINT mode
./keyhunt -m xpoint -f tests/120.txt -t 4 -b 125 -R -q
```

## Unit Test Framework

Keyhunt includes a lightweight, zero-dependency unit test framework for automated testing of core components. The framework is designed for simplicity, portability, and fast compilation without requiring external libraries like Google Test or Catch2.

### Architecture

The test framework consists of three main components:

#### 1. **test_framework.h** (Header-Only Framework)

Located at `tests/test_framework.h`, this is the core testing infrastructure:

- **Header-only design**: No separate compilation needed
- **Macro-based API**: Simple `TEST()` and `ASSERT_*()` macros
- **Colored output**: Terminal colors for pass/fail visualization
- **Zero dependencies**: Pure C with stdio/stdlib only

**Key Features:**
- Test definition with `TEST(name)` macro
- Test execution with `RUN_TEST(name)` macro
- Comprehensive assertion macros (see below)
- Automatic test counting and results summary
- Section markers for organizing related tests

#### 2. **run_tests.cpp** (Main Test Runner)

Located at `tests/run_tests.cpp`, this is the entry point for running tests:

- **Unified interface**: Single binary runs all or selected test modules
- **Modular execution**: Run specific test suites (e.g., `./run_tests int`)
- **Module integration**: Links together all test modules
- **Help system**: Built-in usage documentation

**Supported test modules:**
```
./run_tests              # Run all tests
./run_tests int          # Int (256-bit integer) tests
./run_tests hash         # Hash and crypto tests
./run_tests bloom        # Bloom filter tests
./run_tests bsgs         # BSGS integration tests
./run_tests bsgs_ops     # BSGS operations tests
./run_tests bsgs_sort    # BSGS sort/search tests
./run_tests gpu          # GPU backend tests
./run_tests multi_gpu    # Multi-GPU integration tests
./run_tests distributed  # Distributed mode tests
./run_tests wizard       # Wizard tests
./run_tests point        # Point operation tests
./run_tests intgroup     # IntGroup batch inversion tests
./run_tests sha512       # SHA512 SIMD tests
./run_tests sha256       # SHA256 SIMD tests
./run_tests search_xpoint # XPOINT search mode tests
./run_tests search_rmd160 # RMD160 search mode tests
./run_tests fused        # Fused hash pipeline tests
./run_tests extended     # Extended range tests
```

#### 3. **Test Modules** (Individual Test Files)

Each component has its own test file (e.g., `test_int.cpp`, `test_bloom.cpp`):

- **Isolated testing**: Each module tests one component
- **Standardized structure**: All follow the same pattern
- **Entry point function**: Each provides `int run_*_tests(void)`
- **Independent execution**: Can be compiled/run standalone

### Writing Tests

#### Basic Test Structure

```cpp
#include "test_framework.h"
#include "../src/component.h"  // Component under test

// Define a test
TEST(test_basic_functionality) {
    int result = my_function(5);
    ASSERT_EQ(10, result);
    ASSERT_TRUE(result > 0);
}

TEST(test_error_handling) {
    void *ptr = allocate_memory(0);
    ASSERT_NULL(ptr);
}

// Test module entry point
int run_component_tests(void) {
    TEST_INIT();

    TEST_SECTION("Basic Functionality");
    RUN_TEST(test_basic_functionality);

    TEST_SECTION("Error Handling");
    RUN_TEST(test_error_handling);

    return TEST_RESULTS();
}
```

#### Available Assertions

The framework provides comprehensive assertion macros:

| Assertion | Purpose | Example |
|-----------|---------|---------|
| `ASSERT_TRUE(cond)` | Condition must be true | `ASSERT_TRUE(x > 0)` |
| `ASSERT_FALSE(cond)` | Condition must be false | `ASSERT_FALSE(ptr == NULL)` |
| `ASSERT_EQ(expected, actual)` | Values must be equal | `ASSERT_EQ(42, result)` |
| `ASSERT_NEQ(not_expected, actual)` | Values must differ | `ASSERT_NEQ(0, count)` |
| `ASSERT_STR_EQ(expected, actual)` | Strings must match | `ASSERT_STR_EQ("hello", str)` |
| `ASSERT_MEM_EQ(expected, actual, len)` | Memory must match | `ASSERT_MEM_EQ(buf1, buf2, 32)` |
| `ASSERT_NULL(ptr)` | Pointer must be NULL | `ASSERT_NULL(error_ptr)` |
| `ASSERT_NOT_NULL(ptr)` | Pointer must not be NULL | `ASSERT_NOT_NULL(result)` |
| `ASSERT_DOUBLE_EQ(expected, actual, epsilon)` | Floats equal within tolerance | `ASSERT_DOUBLE_EQ(3.14, pi, 0.01)` |

**Assertion Behavior:**
- On failure: Prints file/line, expected vs actual values, and marks test as failed
- On success: Silent (no output)
- Early return: Failed assertions immediately exit the current test function

#### Test Organization

Use `TEST_SECTION()` to group related tests:

```cpp
int run_my_tests(void) {
    TEST_INIT();

    TEST_SECTION("Core Operations");
    RUN_TEST(test_add);
    RUN_TEST(test_subtract);

    TEST_SECTION("Edge Cases");
    RUN_TEST(test_overflow);
    RUN_TEST(test_zero_division);

    return TEST_RESULTS();
}
```

### Running Tests

#### Build Tests

```bash
# Build all unit tests
make tests

# Build specific test module
cd tests && make test_int
```

#### Run All Tests

```bash
./run_tests
```

**Output:**
```
╔═══════════════════════════════════════════════════════════════╗
║           KEYHUNT UNIT TEST SUITE                            ║
╚═══════════════════════════════════════════════════════════════╝

>>> Running Int Tests

=== KEYHUNT UNIT TESTS ===

[Arithmetic Operations]
  Running: test_add ... PASSED
  Running: test_subtract ... PASSED
  Running: test_multiply ... PASSED

[Edge Cases]
  Running: test_overflow ... PASSED
  Running: test_underflow ... PASSED

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  Total:  5
  Passed: 5
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

#### Run Specific Module

```bash
# Run only Int tests
./run_tests int

# Run only hash tests
./run_tests hash

# Run only BSGS tests
./run_tests bsgs
```

### Adding New Test Modules

To add a new test module (e.g., for a new component):

#### 1. Create Test File

Create `tests/test_mycomponent.cpp`:

```cpp
#include "test_framework.h"
#include "../src/mycomponent.h"

TEST(test_basic) {
    // Test code here
    ASSERT_TRUE(1 == 1);
}

int run_mycomponent_tests(void) {
    TEST_INIT();
    RUN_TEST(test_basic);
    return TEST_RESULTS();
}
```

#### 2. Update run_tests.cpp

Add forward declaration and call:

```cpp
// Forward declaration
int run_mycomponent_tests(void);

// In main()
if (module == NULL || strcmp(module, "mycomponent") == 0) {
    printf(CLR_BOLD "\n>>> Running MyComponent Tests\n" CLR_RESET);
    total_failures += run_mycomponent_tests();
}
```

#### 3. Update Makefile

Add to `tests/Makefile`:

```makefile
test_mycomponent: test_mycomponent.cpp test_framework.h
	$(CXX) $(CXXFLAGS) -o $@ $< ../src/mycomponent.cpp
```

### Best Practices

1. **One component per test file**: Keep tests focused and organized
2. **Test both success and failure**: Cover happy path and error cases
3. **Use descriptive test names**: `test_add_overflow_returns_max` not `test1`
4. **Group related tests**: Use `TEST_SECTION()` for readability
5. **Keep tests fast**: Unit tests should complete in milliseconds
6. **Avoid external dependencies**: Tests should be self-contained
7. **Test edge cases**: Zero, negative, maximum values, NULL pointers
8. **Use appropriate assertions**: Choose the most specific assertion available

### Continuous Integration

The unit test suite integrates with CI pipelines:

```bash
# Pre-commit hook
make tests && ./run_tests || exit 1

# CI workflow
make tests
./run_tests
if [ $? -ne 0 ]; then
    echo "Unit tests failed!"
    exit 1
fi
```

### Troubleshooting

**Tests don't compile:**
- Check include paths in Makefile
- Verify component source files are linked
- Ensure C++17 standard is enabled

**Tests crash:**
- Run with Valgrind: `valgrind ./run_tests int`
- Check for uninitialized variables
- Verify pointer validity before dereferencing

**Tests are slow:**
- Profile with `-pg` flag
- Reduce test iteration counts
- Consider moving to integration tests if testing large data

## Test Data Files

Located in `tests/` directory:

| File | Mode | Description |
|------|------|-------------|
| `1to32.txt` | ADDRESS | 32 known addresses (keys 1-32) |
| `66.txt` | ADDRESS | Puzzle 66 address |
| `66.rmd` | RMD160 | Puzzle 66 as RIPEMD160 |
| `125.txt` | BSGS | Puzzle 125 public key |
| `120.txt` | XPOINT | X-coordinate test |

### Creating Test Data

```bash
# Generate address from known private key
echo "1" | ./keyhunt -m vanity -v 1  # Shows address for key 0x1

# Convert address to RMD160
# Use Python or online tool
```

## Performance Benchmark

### Built-in Benchmark

```bash
./keyhunt --benchmark
```

Output:
```
Keyhunt Performance Benchmark
=============================

CPU Information:
  Model: AMD Ryzen 9 5900X
  Cores: 12 physical, 24 logical
  Features: SSE2, AVX2, SHA-NI

Testing ADDRESS mode...
  Threads: 1   Speed: 5.2 Mkeys/s
  Threads: 4   Speed: 20.1 Mkeys/s
  Threads: 8   Speed: 39.8 Mkeys/s
  Threads: 16  Speed: 78.2 Mkeys/s
  Threads: 24  Speed: 92.4 Mkeys/s

Testing GPU modes...
  GPU: NVIDIA RTX 3080
  Mode: hash    Speed: 180 Mkeys/s
  Mode: full    Speed: 320 Mkeys/s
  Mode: hybrid  Speed: 412 Mkeys/s

Recommendations:
  CPU optimal threads: 24
  GPU optimal mode: hybrid
  Combined speed: 412 Mkeys/s
```

### Manual Performance Test

```bash
# 10-second ADDRESS mode test
timeout 10 ./keyhunt -m address -f tests/66.txt -b 66 -R -t 16 -q

# 60-second BSGS test
timeout 60 ./keyhunt -m bsgs -f tests/125.txt -b 125 -q -s 10
```

### Profiling

```bash
# Build with profiling
make CXXFLAGS="-O2 -pg"

# Run
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF

# Analyze
gprof keyhunt gmon.out > profile.txt
```

Key metrics to check:
- `ripemd160_avx2` should dominate (hashing is the bottleneck)
- Bloom filter checks should be fast
- Minimal time in memory allocation

## Memory Testing

### Valgrind Memory Check

```bash
# Build for Valgrind
make CXXFLAGS="-O1 -g"

# Run with Valgrind
valgrind --leak-check=full --show-leak-kinds=all \
    ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF
```

Expected: No memory leaks, no invalid reads/writes.

### AddressSanitizer

```bash
# Build with ASan
make CXXFLAGS="-O1 -g -fsanitize=address -fno-omit-frame-pointer"

# Run (automatically checks for errors)
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF
```

### BSGS Memory Validation

```bash
# Should show memory check
./keyhunt -m bsgs -f tests/125.txt -b 125

# Should warn about insufficient memory
./keyhunt -m bsgs -f tests/125.txt -b 125 -n 0x10000000000000000
```

## Correctness Tests

### Known Answer Test

Verify keys produce correct addresses:

```python
#!/usr/bin/env python3
# test_correctness.py
import subprocess
import re

known_keys = {
    "1": "1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH",
    "2": "1CUNEBjYrCn2y1SdiUMohaKUi4wpP326Lb",
    "3": "19ZewH8Kk1PDbSNdJ97FP4EiCjTRaZMZQA",
}

# Run keyhunt and verify
for key, expected_addr in known_keys.items():
    result = subprocess.run(
        ["./keyhunt", "-m", "address", "-f", "-", "-r", f"{key}:{key}"],
        input=expected_addr.encode(),
        capture_output=True
    )
    assert expected_addr in result.stdout.decode(), f"Failed for key {key}"
    print(f"OK: Key {key} -> {expected_addr}")

print("All correctness tests passed!")
```

### BSGS Correctness

```bash
# Known public key with known private key
# Should find the key
./keyhunt -m bsgs -f tests/known_pubkey.txt -r 12345:12345
```

### Endomorphism Test

```bash
# With endomorphism (-e flag)
./keyhunt -m address -f tests/66.txt -b 66 -e -R -q -s 10
```

## Stress Testing

### Long-Running Test

```bash
# Run for 1 hour
timeout 3600 ./keyhunt -m address -f tests/66.txt -b 66 -R -t 16 -q -s 60
```

Monitor:
- Memory usage stable
- Speed consistent
- No crashes

### Multi-Instance Test

```bash
# Run multiple instances
for i in {1..4}; do
    ./keyhunt -m address -f tests/66.txt -b 66 -R -t 4 -q &
done
wait
```

### Resource Exhaustion Test

```bash
# Maximum threads
./keyhunt -m address -f tests/66.txt -b 66 -t 1000

# Should auto-correct to available cores
```

## Distributed Mode Testing

### Server-Client Test

Terminal 1 (Server):
```bash
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF \
    --server --port 2222 --work-unit-size 0x10000
```

Terminal 2 (Client):
```bash
./keyhunt --client --server-ip 127.0.0.1 --port 2222
```

Verify:
- Client connects successfully
- Work units distributed
- Keys found reported to server

### Wizard Test

```bash
# Interactive test
./keyhunt --wizard
# Follow prompts, verify each step works
```

## Regression Testing

### Before Commits

```bash
#!/bin/bash
# pre-commit-test.sh

echo "Running regression tests..."

# Quick functionality
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -t 4 || exit 1

# Mode tests
./keyhunt -m bsgs -f tests/125.txt -b 125 -n 0x1000000 -q -s 1 || exit 1
./keyhunt -m rmd160 -f tests/66.rmd -b 66 -R -q -s 1 -t 4 || exit 1

# Benchmark (should complete without error)
timeout 30 ./keyhunt --benchmark || exit 1

echo "All tests passed!"
```

### CI Integration

Example GitHub Actions workflow:

```yaml
# .github/workflows/test.yml
name: Tests
on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3

      - name: Build
        run: make

      - name: Quick Test
        run: ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF

      - name: Benchmark
        run: timeout 30 ./keyhunt --benchmark
```

## Test Coverage

### Code Coverage

```bash
# Build with coverage
make CXXFLAGS="-O0 -g --coverage"

# Run tests
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF
./keyhunt -m bsgs -f tests/125.txt -b 125 -n 0x1000000

# Generate report
gcov keyhunt.cpp
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_report
```

## Troubleshooting Tests

### Test Fails to Find Keys

1. Check target file format
2. Verify range contains target
3. Check key type (compressed/uncompressed)

### Performance Below Expected

1. Check SIMD detection at startup
2. Verify CPU frequency scaling
3. Check for thermal throttling

### Memory Errors

1. Check available RAM
2. Reduce N value for BSGS
3. Check for other memory-hungry processes

## See Also

- [Building](building.md) - Build options
- [Architecture](architecture.md) - Code structure
- [Quick Start](../getting-started/quick-start.md) - Basic usage
