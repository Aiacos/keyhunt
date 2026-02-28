# Coding Conventions

**Analysis Date:** 2026-02-28

## Naming Patterns

**Files:**
- C++ source: `.cpp` extension (e.g., `src/secp256k1/Int.cpp`, `tests/test_hash.cpp`)
- C headers: `.h` extension (e.g., `src/core/sysinfo.h`)
- C source: `.c` extension (e.g., `src/config/config.cpp` for mixed C/C++)
- CUDA kernels: `.cu` extension (e.g., `src/gpu/gpu_backend_cuda.cu`)
- OpenCL kernels: `.cl` extension (e.g., `src/gpu/gpu_secp256k1_opencl.cl`)
- Test files: `test_*.cpp` or `test_*.c` pattern (e.g., `tests/test_hash.cpp`)
- Benchmark files: `benchmark_*.cpp` pattern (e.g., `src/benchmarks/benchmark_bloom.cpp`)

**Functions:**
- C-style functions: `snake_case` (e.g., `bloom_init2()`, `sysinfo_init()`, `kh_config_init()`)
- Namespace prefix for module functions: Module abbreviation + underscore (e.g., `kh_` for keyhunt config, `output_` for output module)
- C++ class methods: `PascalCase` (e.g., `Int::Add()`, `Point::Clear()`, `SECP256K1::GetHash160_AVX2()`)
- Getter methods: `Get*()` pattern (e.g., `GetInt64()`, `IsZero()`)
- Setter methods: `Set*()` pattern (e.g., `SetupField()`)
- Boolean predicates: `Is*()` or `Has*()` (e.g., `IsZero()`, `IsGreater()`, `HasAVX2`)

**Variables:**
- Local variables: `snake_case` (e.g., `available_ram`, `device_count`, `thread_handles`)
- Global variables: `SCREAMING_SNAKE_CASE` (e.g., `g_tests_run`, `g_tests_failed`, `g_current_test_name`)
- Struct members: `snake_case` (e.g., `cpu_physical_cores`, `ram_available`, `gpu_vram_mb`)
- Constants: `SCREAMING_SNAKE_CASE` (e.g., `BISIZE`, `NB64BLOCK`, `GPU_MAX_DEVICES`)
- Test global state: `g_` prefix (e.g., `g_tests_run`, `g_current_test_failed`)

**Types:**
- Enums: `snake_case_t` suffix (e.g., `search_mode_t`, `output_level_t`, `key_type_t`, `crypto_type_t`)
- Structs: `snake_case_t` suffix (e.g., `system_info_t`, `keyhunt_config_t`, `bsgs_config_t`)
- Classes: `PascalCase` (e.g., `Int`, `Point`, `SECP256K1`, `IntGroup`)
- Typedef'd pointers: typically `Type*` not `Type_ptr` (e.g., `Int*`, `Point*`)

## Code Style

**Formatting:**
- No external formatter required; code uses conventional C/C++ style
- Indentation: Tabs preferred (can see from Makefile history and source files)
- Line length: No hard limit, but keep reasonable (~100-120 chars for readability)
- Brace style: Opening brace on same line (e.g., `if (condition) { ... }`)
- Function definitions: Opening brace on new line for top-level functions

**Linting:**
- No automated linting tool detected (no `.eslintrc`, `.clang-format`, etc.)
- Code review follows manual conventions and historical patterns
- Compiler warnings: `-Wall -Wextra` enabled in Makefile
- Strict flags: `-fno-exceptions` for C++ (performance-critical code)

**Compiler Flags (from Makefile):**
```makefile
CXXFLAGS = -Wall -Wextra -Wno-deprecated-copy -std=gnu++17 -O2 -flto=auto -fno-exceptions
CFLAGS = -Wall -Wextra -Wno-unused-parameter -Wno-unused-result -std=c99 -O2 -flto=auto
```

**Platform Compatibility:**
- Code supports both POSIX (Linux, macOS) and Windows (MinGW-w64, MSVC)
- Platform-specific code uses `#ifdef _WIN64`, `#ifdef __CYGWIN__`, `#ifdef __cplusplus`
- Platform abstraction in `src/platform/platform.h` for threads, mutexes, timing

## Import Organization

**Order (observed pattern):**
1. Standard C library headers: `<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<stdint.h>`
2. System headers: `<pthread.h>`, `<windows.h>`, `<time.h>`
3. Project headers: `"config.h"`, `"secp256k1/Int.h"`
4. Forward declarations and ifdef blocks for C++/C compatibility

**Example from `src/config/config.h`:**
```cpp
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../cli.h"

#ifdef __cplusplus
extern "C" {
#endif
```

**Path Aliases:**
- Use `-I$(SRCDIR)` include path (from Makefile)
- Relative includes from src root: `#include "secp256k1/Int.h"`, `#include "hash/ripemd160.h"`
- Test files include test framework: `#include "test_framework.h"`

## Error Handling

**Patterns:**
- Return code convention: `0` = success, negative = error (e.g., `-1` for failure)
- Pointer returns: `NULL` for failure/not found, non-NULL for success
- No exceptions: Codebase uses `-fno-exceptions` flag
- Error context preserved in structs: `keyhunt_config_t` contains validation flags
- Silent fallback pattern: Invalid values auto-corrected by validation system (`kh_config_validate()`)

**Examples:**
```c
// Return code pattern
int kh_config_validate(keyhunt_config_t *cfg);  // Returns 0=success, -1=error

// Pointer pattern
Int* uint64_to_int(uint64_t value);  // Returns NULL or allocated Int*

// Error state in struct
typedef struct {
    int return_code;  // 0=success, non-zero=error
    char error_msg[256];
} result_t;
```

**Parameter Validation System:**
- `src/core/parameter_validator.h` provides intelligent auto-correction
- Validates CPU cores, RAM usage, BSGS parameters
- Four feedback levels: ✓ (optimal), i (works), ! (corrected), ⚠ (risky)

## Logging

**Framework:** `console` based (no external logging library)

**Module Functions:**
- `output_info()`: Informational messages with [I] prefix
- `output_success()`: Success messages with [+] prefix
- `output_warning()`: Warnings with [W] prefix
- `output_error()`: Errors with [E] prefix
- `output_debug()`: Debug output with [D] prefix (respects verbosity level)

**Verbosity Levels** (from `src/output.h`):
```c
typedef enum {
    OUTPUT_SILENT = 0,    // No output except errors and results
    OUTPUT_MINIMAL = 1,   // Clean single-line progress
    OUTPUT_NORMAL = 2,    // Standard output (default)
    OUTPUT_VERBOSE = 3,   // Detailed statistics
    OUTPUT_DEBUG = 4      // Debug-level diagnostics
} output_level_t;
```

**Usage:**
```cpp
output_info("Starting BSGS search");
output_success("Key found: %s", private_key);
output_warning("Memory usage high: %lu MB", used_mb);
output_error("Failed to load bloom filter");
```

**Progress Tracking:**
- Real-time progress via `src/progress.h`
- Persists to `~/.keyhunt/progress/` every 60 seconds
- JSON format for human inspection

## Comments

**When to Comment:**
- Algorithm complexity or non-obvious optimizations
- Workarounds for known issues (e.g., Ubuntu freeze with -Ofast)
- Legal notices and attribution (GPL headers)
- Test vector source documentation (e.g., "RIPEMD160("abc") = ...")
- API contracts and preconditions

**Style:**
- Single-line comments: `//` (C++ style used throughout)
- Multi-line comments: `/* ... */` (C style for file headers)
- Section markers: `/* ============================================================================ */` for logical divisions

**Examples from codebase:**
```cpp
// Test vector from ISO/IEC 10118-3:2004
hex_to_bytes("9c1185a5c5e9fc54612808977ee8f548b2258d31", expected, 20);

// Ubuntu freeze workaround: use -O2 instead of -Ofast
OPT_FLAGS := -O2 -ftree-vectorize -funroll-loops -pipe

// RIPEMD160 hashing in address generation (critical performance path)
```

**Documentation Comments:**
- Doxygen-style docstrings in headers (not enforced but used in new code)
- Parameter descriptions with `@param`, `@return` tags
- Example: See `src/config/config.h` lines 278-324

## Function Design

**Size Guidelines:**
- Keep functions focused and single-responsibility
- Large monolithic functions in `keyhunt.cpp` (~4300 lines total) being refactored into modules
- BSGS mode spans `keyhunt.cpp:1800-2100` — potential candidate for extraction

**Parameters:**
- Prefer passing structs over many individual parameters (see `keyhunt_config_t`)
- Pointers for output parameters (e.g., `uint64_t *out_value`)
- Input parameters const-qualified where applicable (e.g., `const keyhunt_config_t *cfg`)
- C++ methods commonly take `Int*` (const or mutable) for arithmetic operations

**Return Values:**
- Integer return codes: 0 for success, negative for error
- Pointer returns: NULL for failure, non-NULL for success
- Out parameters: Pass pointers, fill on success
- No output on error paths (fail-fast style)

## Module Design

**Exports Pattern:**
- Header file lists all public API
- C modules use `snake_case_function_name()` exported from headers
- C++ classes use full public interface in `.h` files
- Implementation in `.cpp` or `.c` files

**Examples:**
- `src/config/config.h`: Exports ~25 functions (`kh_config_*`, `kh_bsgs_*`)
- `src/output.h`: Exports logging functions (`output_info()`, `output_progress()`, etc.)
- `src/secp256k1/Int.h`: Exports `Int` class with ~30 public methods

**Barrel Files:**
- Not used (direct includes of specific headers preferred)
- Include paths: `#include "secp256k1/Int.h"` not `#include "secp256k1.h"`

**Layered Dependencies:**
```
Cryptography Layer: src/secp256k1/, src/hash/
    ↓
Algorithm Layer: src/bsgs/, src/bloom/
    ↓
Core Logic: keyhunt.cpp
    ↓
I/O & Output: src/output.cpp, src/progress.cpp
```

## C/C++ Interoperability

**Pattern:**
- Pure C headers use `#ifdef __cplusplus extern "C" { #endif` guards
- C++ can call C functions when needed
- Common pattern for cryptographic code (C-based) called from C++ main

**Example:**
```cpp
#ifdef __cplusplus
extern "C" {
#endif

void sysinfo_init(system_info_t *info);

#ifdef __cplusplus
}
#endif
```

---

*Convention analysis: 2026-02-28*
