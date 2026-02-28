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

## Running Tests

Keyhunt includes a comprehensive unit test suite to verify correctness of core components. Tests can be run with a single command or selectively by module.

### Building and Running Tests

#### Quick Start

Build and run all tests with a single command:

```bash
make test
```

This command:
1. Compiles the test executable (`run_tests`)
2. Runs all test modules automatically
3. Reports pass/fail status with colored output

#### Manual Build and Run

If you need to build tests separately:

```bash
# Build the test executable
make run_tests

# Run all tests
./run_tests
```

**Output:** You'll see a colored banner followed by each test module running. Green ✓ indicates passed tests, red ✗ indicates failures.

### Running Specific Test Modules

To run only specific test modules, pass the module name as an argument:

```bash
# Run only Int (256-bit integer) tests
./run_tests int

# Run only hash/crypto tests
./run_tests hash

# Run only Bloom filter tests
./run_tests bloom

# Run only BSGS tests
./run_tests bsgs

# Run only GPU backend tests
./run_tests gpu
```

**Available modules:**
- `int` - 256-bit integer arithmetic tests
- `hash` - Hash functions (RIPEMD160, SHA256, SHA512) and SIMD variants
- `bloom` - Bloom filter operations
- `bsgs` - BSGS integration tests
- `bsgs_ops` - BSGS operations tests
- `bsgs_sort` - BSGS sort and search tests
- `gpu` - GPU backend tests
- `multi_gpu` - Multi-GPU integration tests
- `distributed` - Distributed mode tests
- `wizard` - Interactive wizard tests
- `point` - Elliptic curve point operations
- `intgroup` - Batch modular inversion tests
- `sha512` - SHA512 SIMD tests
- `sha256` - SHA256 SIMD tests
- `search_xpoint` - XPOINT search mode tests
- `search_rmd160` - RMD160 search mode tests
- `fused` - Fused hash pipeline tests
- `extended` - Extended range (256-bit) tests

**Help:** Run `./run_tests help` to see the full list of available modules.

### Advanced Testing Options

#### AddressSanitizer (Memory Safety)

Detect memory errors (buffer overflows, use-after-free, memory leaks):

```bash
make sanitize
./run_tests_asan
```

**Use when:** Debugging memory-related crashes or validating new memory management code.

#### ThreadSanitizer (Race Conditions)

Detect data races and thread safety issues:

```bash
make tsan
./run_tests_tsan
```

**Use when:** Debugging multi-threaded code or validating thread safety.

#### Coverage Analysis

Generate code coverage reports:

```bash
# Build with coverage instrumentation
make coverage

# Run tests to collect coverage data
./run_tests_cov

# Generate coverage report (requires lcov)
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_report
```

**Use when:** Identifying untested code paths or measuring test completeness.

## Common Test Patterns

This section demonstrates how to write effective unit tests for keyhunt, based on real examples from the test suite.

### 1. Basic Test Structure

Tests use the lightweight `TEST()` macro from `test_framework.h`. Here's the anatomy of a simple test:

```cpp
#include "test_framework.h"
#include "secp256k1/Int.h"

TEST(int_add_simple) {
    Int a(100);
    Int b(200);
    a.Add(&b);
    ASSERT_EQ(300, a.GetInt64());
}
```

**Key elements:**
- `TEST(name)`: Defines a test function (automatically prefixed with `test_`)
- `ASSERT_EQ(expected, actual)`: Verifies equality
- Clear, descriptive test names indicating what is being tested

**Available assertion macros:**
- `ASSERT_TRUE(condition)` - Verify condition is true
- `ASSERT_FALSE(condition)` - Verify condition is false
- `ASSERT_EQ(expected, actual)` - Verify numeric equality
- `ASSERT_NEQ(not_expected, actual)` - Verify numeric inequality
- `ASSERT_STR_EQ(expected, actual)` - Verify string equality
- `ASSERT_MEM_EQ(expected, actual, len)` - Verify memory block equality
- `ASSERT_NULL(ptr)` - Verify pointer is NULL
- `ASSERT_NOT_NULL(ptr)` - Verify pointer is not NULL
- `ASSERT_DOUBLE_EQ(expected, actual, epsilon)` - Verify floating-point equality

### 2. Grouping Tests with TEST_SECTION

Use `TEST_SECTION()` to organize related tests visually in the output:

```cpp
int main(int argc, char *argv[]) {
    TEST_INIT();

    TEST_SECTION("Addition Tests");
    RUN_TEST(int_add_simple);
    RUN_TEST(int_add_uint64);
    RUN_TEST(int_add_zero);
    RUN_TEST(int_add_carry);

    TEST_SECTION("Subtraction Tests");
    RUN_TEST(int_sub_simple);
    RUN_TEST(int_sub_uint64);
    RUN_TEST(int_sub_to_zero);

    TEST_SECTION("Multiplication Tests");
    RUN_TEST(int_mult_simple);
    RUN_TEST(int_mult_large);

    return TEST_RESULTS();
}
```

**Output:**
```
=== KEYHUNT UNIT TESTS ===

[Addition Tests]
  Running: int_add_simple ... PASSED
  Running: int_add_uint64 ... PASSED
  Running: int_add_zero ... PASSED
  Running: int_add_carry ... PASSED

[Subtraction Tests]
  Running: int_sub_simple ... PASSED
  ...
```

### 3. Testing with Data Files

Many tests require test data files from the `tests/` directory. Here's how to structure such tests:

**Example: Testing hash functions with official test vectors**

```cpp
#include "test_framework.h"
#include "hash/ripemd160.h"
#include <cstring>
#include <cstdio>

/* Helper function to convert hex string to bytes */
static void hex_to_bytes(const char *hex, unsigned char *bytes, size_t len) {
    for (size_t i = 0; i < len; i++) {
        sscanf(hex + 2*i, "%2hhx", &bytes[i]);
    }
}

TEST(ripemd160_abc) {
    unsigned char input[] = "abc";
    unsigned char digest[20];
    unsigned char expected[20];

    /* RIPEMD160("abc") = 8eb208f7e05d987a9b044a8e98c6b087f15a0bfc */
    hex_to_bytes("8eb208f7e05d987a9b044a8e98c6b087f15a0bfc", expected, 20);

    ripemd160(input, 3, digest);
    ASSERT_MEM_EQ(expected, digest, 20);
}

TEST(ripemd160_million_a) {
    /* Test with 1 million 'a' characters */
    const size_t len = 1000000;
    unsigned char *input = (unsigned char *)malloc(len);
    ASSERT_NOT_NULL(input);

    memset(input, 'a', len);

    unsigned char digest[20];
    unsigned char expected[20];

    /* Expected hash from official test vectors */
    hex_to_bytes("52783243c1697bdbe16d37f97f68f08325dc1528", expected, 20);

    ripemd160(input, len, digest);
    ASSERT_MEM_EQ(expected, digest, 20);

    free(input);
}
```

**Key patterns:**
- Use helper functions (`hex_to_bytes`) to prepare test data
- Clean up dynamically allocated memory with `free()`
- Use `ASSERT_NOT_NULL()` to verify allocations succeeded
- Test with both small and large inputs
- Use official test vectors when available

**Example: Testing with files from tests/ directory**

```cpp
TEST(address_search_with_testfile) {
    /* This would be a functional test that uses test data files */
    FILE *fp = fopen("tests/1to32.txt", "r");
    ASSERT_NOT_NULL(fp);

    /* Read and validate test data */
    char line[256];
    int count = 0;
    while (fgets(line, sizeof(line), fp)) {
        count++;
        /* Validate address format, etc. */
    }

    fclose(fp);
    ASSERT_EQ(32, count);  /* Verify file has expected number of entries */
}
```

### 4. CI Integration (GitHub Actions)

Tests are automatically run in GitHub Actions CI on every push and pull request. Here's how the CI workflow integrates with the test suite:

**From `.github/workflows/ci.yml`:**

```yaml
jobs:
  build-and-test:
    name: Build and Test
    runs-on: ubuntu-latest
    steps:
      - name: Checkout code
        uses: actions/checkout@v4

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y build-essential

      - name: Build test runner
        run: |
          make run_tests COMMON_FLAGS="-m64 -march=x86-64 -mtune=generic -mssse3"

      - name: Run tests
        timeout-minutes: 5
        run: |
          timeout 240 ./run_tests 2>&1 | tee test_output.txt

      - name: Upload test results
        uses: actions/upload-artifact@v4
        if: always()
        with:
          name: test-results
          path: test_output.txt
```

**Key CI features:**
- **Timeouts**: Tests have a 5-minute timeout to prevent CI hangs
- **Multiple platforms**: Tests run on `ubuntu-latest` and `ubuntu-22.04`
- **Artifact upload**: Test output is saved and can be downloaded from failed runs
- **Generic builds**: CI uses `-march=x86-64` instead of `-march=native` for compatibility

**Testing CI locally:**

To verify tests will pass in CI before pushing:

```bash
# Clean build with CI-compatible flags
make clean
make run_tests COMMON_FLAGS="-m64 -march=x86-64 -mtune=generic -mssse3"

# Run tests with timeout (same as CI)
timeout 240 ./run_tests
```

### 5. Setup and Teardown Patterns

For tests that require initialization or cleanup, use the test function itself:

```cpp
TEST(complex_test_with_setup) {
    /* Setup */
    Int *values = (Int *)malloc(100 * sizeof(Int));
    ASSERT_NOT_NULL(values);

    /* Initialize test data */
    for (int i = 0; i < 100; i++) {
        values[i].SetInt64(i);
    }

    /* Test operations */
    Int sum;
    for (int i = 0; i < 100; i++) {
        sum.Add(&values[i]);
    }

    ASSERT_EQ(4950, sum.GetInt64());  /* Sum of 0..99 */

    /* Teardown */
    free(values);
}
```

**Pattern:**
1. Allocate/initialize resources
2. Verify allocation with `ASSERT_NOT_NULL`
3. Perform test operations
4. Clean up resources before test exits

### 6. Testing Edge Cases

Always test boundary conditions and edge cases:

```cpp
TEST(int_add_overflow) {
    Int a((uint64_t)0xFFFFFFFFFFFFFFFFULL);
    a.Add((uint64_t)1);
    /* Should carry over to next 64-bit block */
    ASSERT_FALSE(a.IsZero());
    ASSERT_EQ(0, a.GetInt64());  /* Lower 64 bits wrap to 0 */
}

TEST(ripemd160_empty_input) {
    unsigned char input[] = "";
    unsigned char digest[20];
    ripemd160(input, 0, digest);
    /* Verify digest is not all zeros */
    int all_zero = 1;
    for (int i = 0; i < 20; i++) {
        if (digest[i] != 0) {
            all_zero = 0;
            break;
        }
    }
    ASSERT_FALSE(all_zero);
}
```

**Common edge cases to test:**
- Zero values
- Maximum/minimum values
- Empty inputs
- Overflow/underflow
- NULL pointers
- Boundary values (powers of 2, etc.)

### 7. Deterministic Testing

Ensure tests produce consistent results across runs:

```cpp
TEST(ripemd160_deterministic) {
    /* Same input should always produce same output */
    unsigned char input[] = "deterministic test";
    unsigned char digest1[20];
    unsigned char digest2[20];

    ripemd160(input, strlen((char*)input), digest1);
    ripemd160(input, strlen((char*)input), digest2);

    ASSERT_MEM_EQ(digest1, digest2, 20);
}

TEST(ripemd160_different_inputs) {
    /* Different inputs should produce different outputs */
    unsigned char input1[] = "test1";
    unsigned char input2[] = "test2";
    unsigned char digest1[20];
    unsigned char digest2[20];

    ripemd160(input1, 5, digest1);
    ripemd160(input2, 5, digest2);

    /* Digests should be different */
    int same = (memcmp(digest1, digest2, 20) == 0);
    ASSERT_FALSE(same);
}
```

### Test Output Format

Tests produce colored, structured output:

```
╔═══════════════════════════════════════════════════════════════╗
║           KEYHUNT UNIT TEST SUITE                            ║
╚═══════════════════════════════════════════════════════════════╝

>>> Running Int Tests

  ✓ test_int_basic_operations
  ✓ test_int_modular_arithmetic
  ✗ test_int_edge_cases (expected 0, got 1)

Int Tests: 2 passed, 1 failed out of 3 tests
```

- **Green ✓**: Test passed
- **Red ✗**: Test failed (with details)
- **Summary**: Pass/fail count per module

### Troubleshooting

#### Test Binary Not Found

If `./run_tests` doesn't exist:

```bash
# Build it first
make run_tests
```

#### Tests Fail After Code Changes

1. **Rebuild from clean state:**
   ```bash
   make clean
   make test
   ```

2. **Run specific failing module:**
   ```bash
   ./run_tests <module_name>
   ```

3. **Use sanitizers for debugging:**
   ```bash
   make sanitize
   ./run_tests_asan <module_name>
   ```

#### Permission Denied

Make sure the test executable has execute permissions:

```bash
chmod +x run_tests
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

### API Reference

This section provides detailed documentation for all test framework macros.

#### TEST_INIT()

**Description:** Initializes the test framework and resets all counters.

**Syntax:**
```c
TEST_INIT()
```

**Usage:** Call once at the beginning of your test module's entry point function, before running any tests.

**Example:**
```c
int run_my_tests(void) {
    TEST_INIT();  // Initialize framework

    RUN_TEST(test_feature_1);
    RUN_TEST(test_feature_2);

    return TEST_RESULTS();
}
```

**Notes:**
- Resets test counters (tests run, passed, failed)
- Prints colored test suite header banner
- Must be called before any `RUN_TEST()` calls

---

#### TEST(name)

**Description:** Defines a new test function.

**Syntax:**
```c
TEST(test_name) {
    // Test code here
}
```

**Parameters:**
- `test_name`: Identifier for the test (no quotes, must be valid C identifier)

**Usage:** Use to define individual test cases. The test name will be displayed when the test runs.

**Example:**
```c
TEST(addition_works) {
    int result = 2 + 2;
    ASSERT_EQ(4, result);
}

TEST(string_comparison) {
    const char *str = "hello";
    ASSERT_STR_EQ("hello", str);
}
```

**Notes:**
- Expands to a function: `void test_<name>(void)`
- Test names should be descriptive and follow naming convention: `test_<feature>_<behavior>`
- Tests should be self-contained and independent

---

#### RUN_TEST(name)

**Description:** Executes a single test and tracks its result.

**Syntax:**
```c
RUN_TEST(test_name)
```

**Parameters:**
- `test_name`: Name of the test to run (must match a `TEST()` definition)

**Usage:** Call from test module entry point to execute a defined test.

**Example:**
```c
TEST(my_test) {
    ASSERT_TRUE(1 == 1);
}

int run_tests(void) {
    TEST_INIT();
    RUN_TEST(my_test);  // Executes test_my_test()
    return TEST_RESULTS();
}
```

**Output:**
```
  Running: my_test ... PASSED
```

**Notes:**
- Prints test name and result (PASSED in green or FAILED in red)
- Updates global pass/fail counters
- On failure, test function returns early after first failed assertion

---

#### TEST_RESULTS()

**Description:** Prints test results summary and returns appropriate exit code.

**Syntax:**
```c
return TEST_RESULTS();
```

**Return Value:**
- `0` if all tests passed
- `1` if any test failed

**Usage:** Call at the end of test module entry point to display results and return exit code.

**Example:**
```c
int run_my_tests(void) {
    TEST_INIT();

    RUN_TEST(test_add);
    RUN_TEST(test_subtract);
    RUN_TEST(test_multiply);

    return TEST_RESULTS();  // Print summary and return exit code
}
```

**Output:**
```
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  Total:  3
  Passed: 3
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

**Notes:**
- Must be called after all tests have run
- Exit code enables CI/CD integration (non-zero = failure)
- Failed test count is displayed in red when failures occur

---

#### TEST_SECTION(name)

**Description:** Prints a section header to group related tests visually.

**Syntax:**
```c
TEST_SECTION("Section Name")
```

**Parameters:**
- `name`: String literal describing the test section

**Usage:** Use to organize tests into logical groups for better readability.

**Example:**
```c
int run_math_tests(void) {
    TEST_INIT();

    TEST_SECTION("Arithmetic Operations");
    RUN_TEST(test_add);
    RUN_TEST(test_subtract);
    RUN_TEST(test_multiply);

    TEST_SECTION("Edge Cases");
    RUN_TEST(test_overflow);
    RUN_TEST(test_division_by_zero);

    return TEST_RESULTS();
}
```

**Output:**
```
[Arithmetic Operations]
  Running: test_add ... PASSED
  Running: test_subtract ... PASSED
  Running: test_multiply ... PASSED

[Edge Cases]
  Running: test_overflow ... PASSED
  Running: test_division_by_zero ... PASSED
```

**Notes:**
- Purely cosmetic - doesn't affect test execution or results
- Helps structure test output for large test suites
- Section names are displayed in yellow

---

#### ASSERT_TRUE(cond)

**Description:** Asserts that a condition evaluates to true.

**Syntax:**
```c
ASSERT_TRUE(condition)
```

**Parameters:**
- `condition`: Expression that should evaluate to non-zero (true)

**Usage:** Use for boolean checks and general condition validation.

**Example:**
```c
TEST(value_is_positive) {
    int value = get_user_count();
    ASSERT_TRUE(value > 0);
    ASSERT_TRUE(value < 1000000);
}

TEST(pointer_is_valid) {
    void *ptr = allocate_buffer();
    ASSERT_TRUE(ptr != NULL);
}
```

**On Failure:**
```
    ASSERT_TRUE failed at test_example.cpp:42
    Condition: value > 0
```

**Notes:**
- Test function exits immediately on failure
- Condition is printed as-is in error message
- Use `ASSERT_FALSE()` for inverse checks

---

#### ASSERT_FALSE(cond)

**Description:** Asserts that a condition evaluates to false.

**Syntax:**
```c
ASSERT_FALSE(condition)
```

**Parameters:**
- `condition`: Expression that should evaluate to zero (false)

**Usage:** Use to verify that conditions are NOT true.

**Example:**
```c
TEST(buffer_not_empty) {
    char buffer[100];
    read_data(buffer, sizeof(buffer));
    ASSERT_FALSE(buffer[0] == '\0');  // Should have data
}

TEST(error_flag_not_set) {
    int status = initialize_system();
    ASSERT_FALSE(status & ERROR_FLAG);
}
```

**On Failure:**
```
    ASSERT_FALSE failed at test_example.cpp:56
    Condition: status & ERROR_FLAG
```

---

#### ASSERT_EQ(expected, actual)

**Description:** Asserts that two integer values are equal.

**Syntax:**
```c
ASSERT_EQ(expected_value, actual_value)
```

**Parameters:**
- `expected`: Expected value (evaluated first)
- `actual`: Actual value to compare

**Usage:** Use for numeric equality checks (integers, characters, enums).

**Example:**
```c
TEST(function_returns_correct_value) {
    int result = calculate_sum(2, 3);
    ASSERT_EQ(5, result);
}

TEST(array_size_correct) {
    int count = get_element_count();
    ASSERT_EQ(10, count);
}
```

**On Failure:**
```
    ASSERT_EQ failed at test_example.cpp:23
    Expected: 5
    Actual:   7
```

**Notes:**
- Values are cast to `long long` for display
- For strings, use `ASSERT_STR_EQ()`
- For floating-point, use `ASSERT_DOUBLE_EQ()`

---

#### ASSERT_NEQ(not_expected, actual)

**Description:** Asserts that two values are NOT equal.

**Syntax:**
```c
ASSERT_NEQ(not_expected_value, actual_value)
```

**Parameters:**
- `not_expected`: Value that should NOT match
- `actual`: Actual value to compare

**Usage:** Use to verify values differ (non-zero returns, unique IDs, etc.).

**Example:**
```c
TEST(function_does_not_return_error) {
    int status = perform_operation();
    ASSERT_NEQ(-1, status);  // Should not be error code
}

TEST(unique_identifiers) {
    int id1 = generate_id();
    int id2 = generate_id();
    ASSERT_NEQ(id1, id2);  // Must be unique
}
```

**On Failure:**
```
    ASSERT_NEQ failed at test_example.cpp:67
    Expected not: -1
    Actual:       -1
```

---

#### ASSERT_STR_EQ(expected, actual)

**Description:** Asserts that two null-terminated strings are equal.

**Syntax:**
```c
ASSERT_STR_EQ(expected_string, actual_string)
```

**Parameters:**
- `expected`: Expected string (const char*)
- `actual`: Actual string to compare (const char*)

**Usage:** Use for string comparisons (uses `strcmp()` internally).

**Example:**
```c
TEST(name_formatting) {
    char buffer[100];
    format_name(buffer, "John", "Doe");
    ASSERT_STR_EQ("John Doe", buffer);
}

TEST(config_value_correct) {
    const char *mode = get_config_mode();
    ASSERT_STR_EQ("production", mode);
}
```

**On Failure:**
```
    ASSERT_STR_EQ failed at test_example.cpp:89
    Expected: "hello world"
    Actual:   "hello"
```

**Notes:**
- Both strings must be null-terminated
- Case-sensitive comparison
- For memory comparison, use `ASSERT_MEM_EQ()`

---

#### ASSERT_MEM_EQ(expected, actual, len)

**Description:** Asserts that two memory regions contain identical bytes.

**Syntax:**
```c
ASSERT_MEM_EQ(expected_buffer, actual_buffer, length)
```

**Parameters:**
- `expected`: Pointer to expected data
- `actual`: Pointer to actual data
- `len`: Number of bytes to compare

**Usage:** Use for binary data comparison (hashes, byte arrays, structs).

**Example:**
```c
TEST(hash_calculation) {
    uint8_t expected_hash[32] = { 0xAB, 0xCD, ... };
    uint8_t actual_hash[32];

    calculate_hash(data, actual_hash);
    ASSERT_MEM_EQ(expected_hash, actual_hash, 32);
}

TEST(struct_serialization) {
    struct Config original = { .id = 42, .enabled = 1 };
    struct Config deserialized;

    serialize(&original, buffer);
    deserialize(buffer, &deserialized);

    ASSERT_MEM_EQ(&original, &deserialized, sizeof(struct Config));
}
```

**On Failure:**
```
    ASSERT_MEM_EQ failed at test_example.cpp:104
    Memory comparison failed for 32 bytes
```

**Notes:**
- Uses `memcmp()` internally
- Both pointers must be valid
- Length must not exceed buffer sizes

---

#### ASSERT_NULL(ptr)

**Description:** Asserts that a pointer is NULL.

**Syntax:**
```c
ASSERT_NULL(pointer)
```

**Parameters:**
- `ptr`: Pointer that should be NULL

**Usage:** Use to verify functions return NULL on error or when data is absent.

**Example:**
```c
TEST(invalid_lookup_returns_null) {
    void *result = find_user("nonexistent");
    ASSERT_NULL(result);
}

TEST(allocation_fails_gracefully) {
    void *ptr = try_allocate(SIZE_MAX);  // Should fail
    ASSERT_NULL(ptr);
}
```

**On Failure:**
```
    ASSERT_NULL failed at test_example.cpp:78
    Pointer is not NULL
```

---

#### ASSERT_NOT_NULL(ptr)

**Description:** Asserts that a pointer is NOT NULL.

**Syntax:**
```c
ASSERT_NOT_NULL(pointer)
```

**Parameters:**
- `ptr`: Pointer that should be valid (non-NULL)

**Usage:** Use to verify successful allocations and valid object creation.

**Example:**
```c
TEST(memory_allocation_succeeds) {
    void *buffer = malloc(1024);
    ASSERT_NOT_NULL(buffer);
    free(buffer);
}

TEST(object_creation) {
    MyObject *obj = create_object();
    ASSERT_NOT_NULL(obj);
    destroy_object(obj);
}
```

**On Failure:**
```
    ASSERT_NOT_NULL failed at test_example.cpp:92
    Pointer is NULL
```

---

#### ASSERT_DOUBLE_EQ(expected, actual, epsilon)

**Description:** Asserts that two floating-point values are equal within a tolerance.

**Syntax:**
```c
ASSERT_DOUBLE_EQ(expected_value, actual_value, epsilon)
```

**Parameters:**
- `expected`: Expected floating-point value
- `actual`: Actual floating-point value
- `epsilon`: Maximum acceptable difference (tolerance)

**Usage:** Use for floating-point comparisons (accounts for rounding errors).

**Example:**
```c
TEST(pi_approximation) {
    double pi = calculate_pi(1000);
    ASSERT_DOUBLE_EQ(3.14159265, pi, 0.00001);
}

TEST(percentage_calculation) {
    double percent = calculate_percentage(1, 3);
    ASSERT_DOUBLE_EQ(33.333, percent, 0.001);
}
```

**On Failure:**
```
    ASSERT_DOUBLE_EQ failed at test_example.cpp:112
    Expected: 3.141593
    Actual:   3.140000
    Diff:     0.001593 (max: 0.001000)
```

**Notes:**
- Never compare floats with `ASSERT_EQ()` - use this macro instead
- Choose epsilon based on expected precision
- Difference is calculated as `fabs(expected - actual)`

---

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

### Writing a New Test Module

This section provides a complete step-by-step guide to creating a new test module from scratch. We'll create a test module for a hypothetical "Config" component.

#### Step 1: Create the Test File

Create `tests/test_config.cpp` with the following structure:

```cpp
/*
 * test_config.cpp - Unit tests for the Config component
 *
 * Tests configuration loading and validation:
 * - Loading from files
 * - Default values
 * - Error handling for invalid configs
 */

#include "test_framework.h"
#include "../src/config.h"

/* ============================================================================
 * Basic Loading Tests
 * ============================================================================ */

TEST(config_load_default) {
    Config cfg;
    config_init(&cfg);
    ASSERT_NOT_NULL(&cfg);
    ASSERT_EQ(DEFAULT_TIMEOUT, cfg.timeout);
}

TEST(config_load_from_file) {
    Config cfg;
    int result = config_load(&cfg, "test_config.json");
    ASSERT_EQ(0, result);
    ASSERT_STR_EQ("production", cfg.mode);
}

/* ============================================================================
 * Validation Tests
 * ============================================================================ */

TEST(config_validate_timeout) {
    Config cfg;
    cfg.timeout = -1;
    ASSERT_FALSE(config_validate(&cfg));
}

TEST(config_validate_port_range) {
    Config cfg;
    cfg.port = 99999;  /* Invalid port */
    ASSERT_FALSE(config_validate(&cfg));
}

/* ============================================================================
 * Error Handling Tests
 * ============================================================================ */

TEST(config_load_nonexistent_file) {
    Config cfg;
    int result = config_load(&cfg, "nonexistent.json");
    ASSERT_NEQ(0, result);  /* Should return error code */
}

TEST(config_parse_invalid_json) {
    Config cfg;
    int result = config_load(&cfg, "tests/invalid.json");
    ASSERT_NEQ(0, result);
}

/* ============================================================================
 * Test Module Entry Point
 * ============================================================================ */

int run_config_tests(void) {
    TEST_INIT();

    TEST_SECTION("Basic Loading");
    RUN_TEST(config_load_default);
    RUN_TEST(config_load_from_file);

    TEST_SECTION("Validation");
    RUN_TEST(config_validate_timeout);
    RUN_TEST(config_validate_port_range);

    TEST_SECTION("Error Handling");
    RUN_TEST(config_load_nonexistent_file);
    RUN_TEST(config_parse_invalid_json);

    return TEST_RESULTS();
}
```

**Key Elements:**
- **File header comment**: Describes what component is being tested
- **Include test_framework.h**: Must be first include
- **Include component header**: The code under test
- **Organize with comments**: Use block comments to group related tests
- **TEST() macros**: Define individual test cases
- **run_*_tests() function**: Entry point that calls all tests
- **TEST_INIT()**: Initialize framework at start of entry point
- **TEST_SECTION()**: Group tests visually for better output
- **RUN_TEST()**: Execute each test
- **TEST_RESULTS()**: Return exit code (0 = success, 1 = failure)

#### Step 2: Add Forward Declaration to run_tests.cpp

Open `tests/run_tests.cpp` and add your module's forward declaration at the top with the other declarations:

```cpp
/* Forward declarations for all test modules */
int run_int_tests(void);
int run_hash_tests(void);
int run_bloom_tests(void);
int run_config_tests(void);  // <-- Add this line
/* ... other declarations ... */
```

**Location**: Add after existing forward declarations, before the `main()` function.

#### Step 3: Add Module Execution to run_tests.cpp

In the `main()` function of `run_tests.cpp`, add the logic to run your new test module:

```cpp
/* Config tests */
if (module == NULL || strcmp(module, "config") == 0) {
    printf(CLR_BOLD "\n>>> Running Config Tests\n" CLR_RESET);
    total_failures += run_config_tests();
}
```

**Location**: Add after existing module blocks, before the final results summary.

**Pattern to follow:**
```cpp
if (module == NULL || strcmp(module, "your_module_name") == 0) {
    printf(CLR_BOLD "\n>>> Running YourModule Tests\n" CLR_RESET);
    total_failures += run_your_module_tests();
}
```

- First condition `module == NULL`: Runs when `./run_tests` is called without arguments (run all)
- Second condition `strcmp(module, "config") == 0`: Runs when `./run_tests config` is called
- Module name should match the command-line argument users will use

#### Step 4: Update the Help Text in run_tests.cpp

Add your module to the usage documentation in `show_usage()`:

```cpp
void show_usage(const char *program) {
    printf("Usage: %s [module]\n\n", program);
    printf("Available test modules:\n");
    printf("  int          - Int (256-bit integer) tests\n");
    printf("  hash         - Hash and crypto tests\n");
    printf("  bloom        - Bloom filter tests\n");
    printf("  config       - Configuration tests\n");  // <-- Add this
    /* ... */
}
```

#### Step 5: Add Build Target to Makefile

Open `tests/Makefile` and add compilation rules for your test module.

**Add to TEST_OBJS variable:**

```makefile
TEST_OBJS = test_int.o \
            test_hash.o \
            test_bloom.o \
            test_config.o  # <-- Add this
```

**Add individual build rule:**

```makefile
# Config tests
test_config.o: test_config.cpp test_framework.h ../src/config.h
	$(CXX) $(CXXFLAGS) -c test_config.cpp -o test_config.o
```

**If your component has source files, link them:**

```makefile
# If config.cpp exists
run_tests: $(TEST_OBJS) run_tests.o ../src/config.o
	$(CXX) $(CXXFLAGS) -o $@ $^

# Add rule to build config.o if needed
../src/config.o: ../src/config.cpp ../src/config.h
	$(CXX) $(CXXFLAGS) -c ../src/config.cpp -o ../src/config.o
```

#### Step 6: Build and Run Your Tests

```bash
# Clean build
cd tests
make clean

# Build all tests
make

# Run only your new module
./run_tests config

# Run all tests (including your new module)
./run_tests
```

**Expected output:**

```
╔═══════════════════════════════════════════════════════════════╗
║           KEYHUNT UNIT TEST SUITE                            ║
╚═══════════════════════════════════════════════════════════════╝

>>> Running Config Tests

=== KEYHUNT UNIT TESTS ===

[Basic Loading]
  Running: config_load_default ... PASSED
  Running: config_load_from_file ... PASSED

[Validation]
  Running: config_validate_timeout ... PASSED
  Running: config_validate_port_range ... PASSED

[Error Handling]
  Running: config_load_nonexistent_file ... PASSED
  Running: config_parse_invalid_json ... PASSED

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  Total:  6
  Passed: 6
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

#### Complete Checklist

When adding a new test module, ensure you complete all these steps:

- [ ] **Step 1**: Created `tests/test_<name>.cpp` with proper structure
  - [ ] Included `test_framework.h`
  - [ ] Included component headers
  - [ ] Defined tests with `TEST()` macro
  - [ ] Created `run_<name>_tests()` entry point function
  - [ ] Used `TEST_INIT()`, `TEST_SECTION()`, `RUN_TEST()`, `TEST_RESULTS()`

- [ ] **Step 2**: Added forward declaration `int run_<name>_tests(void);` to `run_tests.cpp`

- [ ] **Step 3**: Added execution block to `main()` in `run_tests.cpp`
  - [ ] Handles both `module == NULL` and specific module name
  - [ ] Prints section header
  - [ ] Accumulates failures in `total_failures`

- [ ] **Step 4**: Updated help text in `show_usage()` function

- [ ] **Step 5**: Updated `tests/Makefile`
  - [ ] Added to `TEST_OBJS`
  - [ ] Added build rule for `.o` file
  - [ ] Linked component source files if needed

- [ ] **Step 6**: Built and verified tests run successfully

#### Real-World Example: test_int.cpp

The `tests/test_int.cpp` file is an excellent reference implementation showing all best practices:

**Structure:**
- Clear file header documenting what's tested
- Organized into sections with block comments
- Comprehensive coverage: construction, arithmetic, comparisons, bit operations
- Both positive and edge case tests
- Clean entry point with organized `TEST_SECTION()` blocks

**To use as a template:**
```bash
# Copy structure from test_int.cpp
cp tests/test_int.cpp tests/test_mynewcomponent.cpp

# Modify:
# 1. Update file header comment
# 2. Change include from "secp256k1/Int.h" to your component
# 3. Replace all Int-specific tests with your component's tests
# 4. Rename entry point: run_int_tests() -> run_mynewcomponent_tests()
# 5. Follow steps 2-6 above
```

#### Tips for Writing Good Tests

1. **Test one thing per test**: Each `TEST()` should verify a single behavior
2. **Use descriptive names**: `config_load_invalid_json` is better than `test3`
3. **Test edge cases**: Null pointers, empty strings, maximum values, zero, negative numbers
4. **Test error paths**: Don't just test success - verify errors are handled correctly
5. **Keep tests independent**: Tests should not depend on each other's state
6. **Use appropriate assertions**: Choose the most specific assertion (e.g., `ASSERT_STR_EQ` for strings)
7. **Add comments for complex setups**: Explain non-obvious test logic
8. **Group related tests**: Use `TEST_SECTION()` to organize output

#### Common Pitfalls to Avoid

❌ **Don't**: Mix multiple assertions for unrelated things in one test
```cpp
TEST(everything) {
    ASSERT_EQ(5, add(2, 3));
    ASSERT_STR_EQ("hello", format_string());  // Unrelated!
}
```

✅ **Do**: Separate into focused tests
```cpp
TEST(add_returns_sum) {
    ASSERT_EQ(5, add(2, 3));
}

TEST(format_string_returns_greeting) {
    ASSERT_STR_EQ("hello", format_string());
}
```

❌ **Don't**: Forget to handle memory cleanup
```cpp
TEST(memory_leak) {
    char *buf = malloc(100);
    process_buffer(buf);
    // Missing free(buf)!
}
```

✅ **Do**: Clean up resources
```cpp
TEST(proper_cleanup) {
    char *buf = malloc(100);
    ASSERT_NOT_NULL(buf);
    process_buffer(buf);
    free(buf);  // Proper cleanup
}
```

❌ **Don't**: Use magic numbers
```cpp
TEST(unclear_assertion) {
    ASSERT_EQ(42, calculate_checksum(data, 100));  // Why 42?
}
```

✅ **Do**: Use named constants or comments
```cpp
TEST(checksum_calculation) {
    const int EXPECTED_CHECKSUM = 42;  // Verified manually
    ASSERT_EQ(EXPECTED_CHECKSUM, calculate_checksum(data, 100));
}
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
