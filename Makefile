CXX ?= g++
CC ?= gcc

# Output directories
OBJDIR := obj
SRCDIR := src

COMMON_FLAGS := -m64 -march=native -mtune=native -mssse3
OPT_FLAGS := -O3 -ftree-vectorize -funroll-loops -pipe -DNDEBUG
WARN_FLAGS := -Wall -Wextra

CXXFLAGS ?=
CFLAGS ?=

# Include path for src/
INCLUDES := -I$(SRCDIR)

# LTO enabled with -fno-strict-aliasing to fix GCC optimization bug
# The Int class uses a union with uint32_t bits[] and uint64_t bits64[]
# Type punning through this union causes incorrect aliasing assumptions in LTO
# See secp256k1/Int.h for the union definition
LTO_FLAGS ?= -flto=auto -fno-strict-aliasing

# Optional CUDA backend (auto-detected if nvcc is available)
NVCC ?= nvcc
CUDA_ARCH ?= sm_75
# New Fedora/GCC versions may be newer than the CUDA validation matrix.
# This flag allows nvcc to use the system host compiler anyway.
NVCCFLAGS ?= -O3 -std=c++17 -arch=$(CUDA_ARCH) -allow-unsupported-compiler $(INCLUDES)
# Optional: point nvcc to a compatible host compiler (e.g. gcc-13)
CUDA_CC_BINDIR ?=
ifneq ($(CUDA_CC_BINDIR),)
  NVCCFLAGS += --compiler-bindir=$(CUDA_CC_BINDIR)
endif
HAVE_NVCC := $(shell command -v $(NVCC) 2>/dev/null)
ifeq ($(HAVE_NVCC),)
  GPU_OBJS := $(OBJDIR)/gpu/gpu_backend_none.o $(OBJDIR)/gpu/gpu_autotune.o $(OBJDIR)/gpu/multi_gpu_scheduler.o $(OBJDIR)/gpu/async_pipeline.o
  GPU_CXXFLAGS :=
else
  GPU_OBJS := $(OBJDIR)/gpu/gpu_backend_cuda.o $(OBJDIR)/gpu/gpu_autotune.o $(OBJDIR)/gpu/multi_gpu_scheduler.o $(OBJDIR)/gpu/async_pipeline.o
  GPU_CXXFLAGS := -DHAVE_CUDA_BACKEND=1
endif

CXXFLAGS += $(COMMON_FLAGS) $(OPT_FLAGS) $(WARN_FLAGS) -Wno-deprecated-copy -std=gnu++17 $(LTO_FLAGS) -fno-exceptions $(INCLUDES)
CFLAGS += $(COMMON_FLAGS) $(OPT_FLAGS) $(WARN_FLAGS) $(LTO_FLAGS) -Wno-unused-parameter -Wno-unused-result $(INCLUDES)
CXXFLAGS += $(GPU_CXXFLAGS)

LDFLAGS ?=
LDFLAGS += $(COMMON_FLAGS) $(LTO_FLAGS) -Wl,-O3 -Wl,--as-needed
LDLIBS ?=
LDLIBS += -lm -lpthread -ldl

# If CUDA backend is built, link against cudart (toolkit runtime)
CUDA_HOME ?= /usr/local/cuda
ifneq ($(HAVE_NVCC),)
  LDFLAGS += -L$(CUDA_HOME)/lib64
  LDFLAGS += -Wl,-rpath,$(CUDA_HOME)/lib64
  LDLIBS += -lcudart
endif

# Optional TLS support with OpenSSL
# Usage: make ENABLE_TLS=1
ifdef ENABLE_TLS
  CFLAGS += -DHAVE_OPENSSL
  CXXFLAGS += -DHAVE_OPENSSL
  LDLIBS += -lssl -lcrypto
endif

# Object files organized by module (all in obj/ directory)
BLOOM_OBJS := $(OBJDIR)/oldbloom/bloom.o $(OBJDIR)/bloom/bloom.o $(OBJDIR)/bloom/bloom_simd.o
HASH_OBJS := $(OBJDIR)/hash/ripemd160.o $(OBJDIR)/hash/ripemd160_sse.o $(OBJDIR)/hash/ripemd160_avx2.o $(OBJDIR)/hash/ripemd160_avx512.o $(OBJDIR)/hash/sha256.o $(OBJDIR)/hash/sha256_sse.o $(OBJDIR)/hash/sha256_avx2.o $(OBJDIR)/hash/sha256_shani.o
SHA3_OBJS := $(OBJDIR)/sha3/sha3.o $(OBJDIR)/sha3/keccak.o
SECP256K1_OBJS := $(OBJDIR)/secp256k1/Int.o $(OBJDIR)/secp256k1/Point.o $(OBJDIR)/secp256k1/SECP256K1.o $(OBJDIR)/secp256k1/IntMod.o $(OBJDIR)/secp256k1/Random.o $(OBJDIR)/secp256k1/IntGroup.o
GMP256K1_OBJS := $(OBJDIR)/gmp256k1/Int.o $(OBJDIR)/gmp256k1/Point.o $(OBJDIR)/gmp256k1/GMP256K1.o $(OBJDIR)/gmp256k1/IntMod.o $(OBJDIR)/gmp256k1/Random.o $(OBJDIR)/gmp256k1/IntGroup.o
BSGS_OBJS := $(OBJDIR)/bsgs/bsgs_ops.o $(OBJDIR)/bsgs/bsgs_fast.o
HYBRID_OBJS := $(OBJDIR)/hybrid/adaptive_scheduler.o
UTIL_OBJS := $(OBJDIR)/util/mempool.o
DIST_OBJS := $(OBJDIR)/distributed/distributed.o
OUTPUT_OBJS := $(OBJDIR)/output.o
PROGRESS_OBJS := $(OBJDIR)/progress.o
BENCHMARK_OBJS := $(OBJDIR)/benchmark.o
CLI_OBJS := $(OBJDIR)/cli.o
WIZARD_OBJS := $(OBJDIR)/wizard/wizard.o $(OBJDIR)/wizard/wizard_config.o $(OBJDIR)/wizard/wizard_ui.o $(OBJDIR)/wizard/wizard_community.o $(OBJDIR)/wizard/wizard_server.o $(OBJDIR)/wizard/wizard_client.o
CORE_OBJS := $(OBJDIR)/core/util.o $(OBJDIR)/core/sysinfo.o $(OBJDIR)/core/parameter_validator.o $(OBJDIR)/core/config.o
CONFIG_OBJS := $(OBJDIR)/config/config.o
SEARCH_OBJS := $(OBJDIR)/search/search_xpoint.o $(OBJDIR)/search/search_rmd160.o

COMMON_OBJS := $(OBJDIR)/base58/base58.o $(OBJDIR)/rmd160/rmd160.o $(OBJDIR)/xxhash/xxhash.o $(CORE_OBJS) $(CONFIG_OBJS) $(GPU_OBJS) $(BLOOM_OBJS) $(HASH_OBJS) $(SHA3_OBJS) $(BSGS_OBJS) $(HYBRID_OBJS) $(UTIL_OBJS) $(DIST_OBJS) $(OUTPUT_OBJS) $(PROGRESS_OBJS) $(BENCHMARK_OBJS) $(CLI_OBJS) $(SEARCH_OBJS)

KEYHUNT_OBJS := $(OBJDIR)/keyhunt.o $(COMMON_OBJS) $(SECP256K1_OBJS) $(WIZARD_OBJS)
BSGSD_OBJS := $(OBJDIR)/bsgsd.o $(COMMON_OBJS) $(SECP256K1_OBJS)
LEGACY_OBJS := $(OBJDIR)/keyhunt_legacy.o $(OBJDIR)/core/hashing.o $(COMMON_OBJS) $(GMP256K1_OBJS)

# Create obj directory structure
OBJ_DIRS := $(OBJDIR) $(OBJDIR)/base58 $(OBJDIR)/rmd160 $(OBJDIR)/xxhash $(OBJDIR)/core $(OBJDIR)/config $(OBJDIR)/gpu $(OBJDIR)/oldbloom $(OBJDIR)/bloom $(OBJDIR)/hash $(OBJDIR)/sha3 $(OBJDIR)/bsgs $(OBJDIR)/hybrid $(OBJDIR)/util $(OBJDIR)/distributed $(OBJDIR)/wizard $(OBJDIR)/secp256k1 $(OBJDIR)/gmp256k1 $(OBJDIR)/search $(OBJDIR)/tests $(OBJDIR)/benchmarks

.PHONY: all clean legacy bsgsd directories test sanitize tsan coverage

all: directories keyhunt

directories: $(OBJ_DIRS)

$(OBJ_DIRS):
	@mkdir -p $@

keyhunt: directories $(KEYHUNT_OBJS)
	$(CXX) $(LDFLAGS) $(KEYHUNT_OBJS) $(LDLIBS) -o $@

bsgsd: directories $(BSGSD_OBJS)
	$(CXX) $(LDFLAGS) $(BSGSD_OBJS) $(LDLIBS) -o $@

legacy: keyhunt_legacy

keyhunt_legacy: directories $(LEGACY_OBJS)
	$(CXX) $(LDFLAGS) $(LEGACY_OBJS) $(LDLIBS) -lcrypto -lgmp -o $@

clean:
	$(RM) keyhunt keyhunt_legacy bsgsd run_tests benchmark_intgroup test_intgroup_avx2
	$(RM) -r $(OBJDIR)

# ============================================================================
# Unit Tests
# ============================================================================

# Test object directory
TEST_OBJDIR := $(OBJDIR)/tests

# Test object files
TEST_INT_OBJ := $(TEST_OBJDIR)/test_int.o
TEST_BLOOM_OBJ := $(TEST_OBJDIR)/test_bloom.o
TEST_BSGS_OBJ := $(TEST_OBJDIR)/test_bsgs_integration.o
TEST_GPU_OBJ := $(TEST_OBJDIR)/test_gpu_backend.o
TEST_DISTRIBUTED_OBJ := $(TEST_OBJDIR)/test_distributed.o
TEST_WIZARD_OBJ := $(TEST_OBJDIR)/test_wizard.o
TEST_RUNNER_OBJ := $(TEST_OBJDIR)/run_tests.o

# Shared objects needed by tests
# CORE_OBJS includes util.o which has tohex() needed by SECP256K1
TEST_SHARED_OBJS := $(SECP256K1_OBJS) $(BLOOM_OBJS) $(HASH_OBJS) $(SHA3_OBJS) \
                    $(OBJDIR)/base58/base58.o $(OBJDIR)/rmd160/rmd160.o \
                    $(OBJDIR)/xxhash/xxhash.o $(UTIL_OBJS) $(CORE_OBJS) \
                    $(GPU_OBJS) $(DIST_OBJS) $(WIZARD_OBJS)

# Build test object files
$(TEST_INT_OBJ): tests/test_int.cpp tests/test_framework.h | directories
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TEST_BLOOM_OBJ): tests/test_bloom.cpp tests/test_framework.h | directories
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TEST_BSGS_OBJ): tests/test_bsgs_integration.cpp tests/test_framework.h | directories
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TEST_GPU_OBJ): tests/test_gpu_backend.cpp tests/test_framework.h | directories
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TEST_DISTRIBUTED_OBJ): tests/test_distributed.cpp tests/test_framework.h | directories
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TEST_WIZARD_OBJ): tests/test_wizard.cpp tests/test_framework.h | directories
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TEST_RUNNER_OBJ): tests/run_tests.cpp | directories
	$(CXX) $(CXXFLAGS) -c $< -o $@

# All test objects
TEST_OBJS := $(TEST_RUNNER_OBJ) $(TEST_INT_OBJ) $(TEST_BLOOM_OBJ) $(TEST_BSGS_OBJ) \
             $(TEST_GPU_OBJ) $(TEST_DISTRIBUTED_OBJ) $(TEST_WIZARD_OBJ)

# Build test runner
run_tests: directories $(TEST_OBJS) $(TEST_SHARED_OBJS)
	$(CXX) $(LDFLAGS) $(TEST_OBJS) $(TEST_SHARED_OBJS) $(LDLIBS) -o $@

# Run all tests
test: run_tests
	./run_tests

# Generic rules for building object files
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | directories
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c | directories
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.cu | directories
	$(NVCC) $(NVCCFLAGS) -c $< -o $@

# Specific rules for C files that need C++ compilation
$(OBJDIR)/core/util.o: $(SRCDIR)/core/util.c | directories
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/core/hashing.o: $(SRCDIR)/core/hashing.c | directories
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/sha3/sha3.o: $(SRCDIR)/sha3/sha3.c | directories
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/sha3/keccak.o: $(SRCDIR)/sha3/keccak.c | directories
	$(CXX) $(CXXFLAGS) -c $< -o $@

# AVX2 optimized builds
$(OBJDIR)/hash/ripemd160_avx2.o: $(SRCDIR)/hash/ripemd160_avx2.cpp | directories
	$(CXX) $(CXXFLAGS) -mavx2 -c $< -o $@

$(OBJDIR)/hash/sha256_avx2.o: $(SRCDIR)/hash/sha256_avx2.cpp | directories
	$(CXX) $(CXXFLAGS) -mavx2 -c $< -o $@

# AVX-512 optimized builds
$(OBJDIR)/hash/ripemd160_avx512.o: $(SRCDIR)/hash/ripemd160_avx512.cpp | directories
	$(CXX) $(CXXFLAGS) -mavx512f -mavx512dq -c $< -o $@

# SHA-NI optimized builds (Intel SHA Extensions)
$(OBJDIR)/hash/sha256_shani.o: $(SRCDIR)/hash/sha256_shani.cpp | directories
	$(CXX) $(CXXFLAGS) -msha -msse4.1 -c $< -o $@

# SIMD bloom filter (AVX2/AVX-512)
$(OBJDIR)/bloom/bloom_simd.o: $(SRCDIR)/bloom/bloom_simd.cpp | directories
	$(CXX) $(CXXFLAGS) -mavx2 -c $< -o $@

# BSGS optimized modules
$(OBJDIR)/bsgs/bsgs_ops.o: $(SRCDIR)/bsgs/bsgs_ops.cpp | directories
	$(CXX) $(CXXFLAGS) -mavx2 -c $< -o $@

$(OBJDIR)/bsgs/bsgs_fast.o: $(SRCDIR)/bsgs/bsgs_fast.cpp | directories
	$(CXX) $(CXXFLAGS) -mavx2 -c $< -o $@

# Secp256k1 AVX2 optimizations
$(OBJDIR)/secp256k1/IntMod.o: $(SRCDIR)/secp256k1/IntMod.cpp | directories
	$(CXX) $(CXXFLAGS) -mavx2 -c $< -o $@

# ============================================================================
# Sanitizer Builds (Memory Safety Testing)
# ============================================================================
# AddressSanitizer: Detects memory errors (buffer overflow, use-after-free, etc.)
# ThreadSanitizer: Detects data races in multithreaded code
#
# Usage:
#   make sanitize    # Build with AddressSanitizer and run tests
#   make tsan        # Build with ThreadSanitizer and run tests
#
# Note: Sanitizer builds are slower and use more memory.
#       They are intended for CI testing, not production.
# ============================================================================

# Sanitizer-specific directories to avoid conflicts with regular builds
SANITIZE_OBJDIR := obj_asan
TSAN_OBJDIR := obj_tsan
COVERAGE_OBJDIR := obj_coverage

# AddressSanitizer flags
ASAN_FLAGS := -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -g -O1
ASAN_LDFLAGS := -fsanitize=address -fsanitize=undefined

# ThreadSanitizer flags
TSAN_FLAGS := -fsanitize=thread -fno-omit-frame-pointer -g -O1
TSAN_LDFLAGS := -fsanitize=thread

# Coverage flags
COVERAGE_FLAGS := --coverage -fprofile-arcs -ftest-coverage -g -O0
COVERAGE_LDFLAGS := --coverage

# Sanitizer build: AddressSanitizer + UndefinedBehaviorSanitizer
sanitize: clean-sanitize
	@echo "Building with AddressSanitizer..."
	@mkdir -p $(SANITIZE_OBJDIR)
	$(MAKE) run_tests_asan OBJDIR=$(SANITIZE_OBJDIR) \
		CXXFLAGS="$(COMMON_FLAGS) $(WARN_FLAGS) -Wno-deprecated-copy -std=gnu++17 -fno-exceptions $(INCLUDES) $(ASAN_FLAGS)" \
		CFLAGS="$(COMMON_FLAGS) $(WARN_FLAGS) -Wno-unused-parameter -Wno-unused-result $(INCLUDES) $(ASAN_FLAGS)" \
		LDFLAGS="$(COMMON_FLAGS) $(ASAN_LDFLAGS) -Wl,--as-needed" \
		LTO_FLAGS="" GPU_CXXFLAGS="" GPU_OBJS="$(SANITIZE_OBJDIR)/gpu/gpu_backend_none.o $(SANITIZE_OBJDIR)/gpu/gpu_autotune.o $(SANITIZE_OBJDIR)/gpu/multi_gpu_scheduler.o $(SANITIZE_OBJDIR)/gpu/async_pipeline.o"
	@echo ""
	@echo "Running tests with AddressSanitizer..."
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1:print_stats=1 ./run_tests_asan

run_tests_asan: directories $(TEST_OBJDIR) $(TEST_OBJS) $(TEST_SHARED_OBJS)
	$(CXX) $(LDFLAGS) $(TEST_OBJS) $(TEST_SHARED_OBJS) $(LDLIBS) -o $@

clean-sanitize:
	$(RM) -r $(SANITIZE_OBJDIR) run_tests_asan

# ThreadSanitizer build
tsan: clean-tsan
	@echo "Building with ThreadSanitizer..."
	@mkdir -p $(TSAN_OBJDIR)
	$(MAKE) run_tests_tsan OBJDIR=$(TSAN_OBJDIR) \
		CXXFLAGS="$(COMMON_FLAGS) $(WARN_FLAGS) -Wno-deprecated-copy -std=gnu++17 -fno-exceptions $(INCLUDES) $(TSAN_FLAGS)" \
		CFLAGS="$(COMMON_FLAGS) $(WARN_FLAGS) -Wno-unused-parameter -Wno-unused-result $(INCLUDES) $(TSAN_FLAGS)" \
		LDFLAGS="$(COMMON_FLAGS) $(TSAN_LDFLAGS) -Wl,--as-needed" \
		LTO_FLAGS="" GPU_CXXFLAGS="" GPU_OBJS="$(TSAN_OBJDIR)/gpu/gpu_backend_none.o $(TSAN_OBJDIR)/gpu/gpu_autotune.o $(TSAN_OBJDIR)/gpu/multi_gpu_scheduler.o $(TSAN_OBJDIR)/gpu/async_pipeline.o"
	@echo ""
	@echo "Running tests with ThreadSanitizer..."
	TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1 ./run_tests_tsan

run_tests_tsan: directories $(TEST_OBJDIR) $(TEST_OBJS) $(TEST_SHARED_OBJS)
	$(CXX) $(LDFLAGS) $(TEST_OBJS) $(TEST_SHARED_OBJS) $(LDLIBS) -o $@

clean-tsan:
	$(RM) -r $(TSAN_OBJDIR) run_tests_tsan

# ============================================================================
# Code Coverage
# ============================================================================
# Builds with coverage instrumentation and generates coverage reports
#
# Usage:
#   make coverage           # Build, run tests, generate report
#   make coverage-report    # Generate report from existing .gcda files
#   make clean-coverage     # Remove coverage artifacts
#
# Requirements:
#   - gcov (comes with GCC)
#   - lcov (for HTML reports): sudo apt install lcov
# ============================================================================

coverage: clean-coverage
	@echo "Building with coverage instrumentation..."
	@mkdir -p $(COVERAGE_OBJDIR)
	$(MAKE) run_tests_cov OBJDIR=$(COVERAGE_OBJDIR) \
		CXXFLAGS="$(COMMON_FLAGS) $(WARN_FLAGS) -Wno-deprecated-copy -std=gnu++17 -fno-exceptions $(INCLUDES) $(COVERAGE_FLAGS)" \
		CFLAGS="$(COMMON_FLAGS) $(WARN_FLAGS) -Wno-unused-parameter -Wno-unused-result $(INCLUDES) $(COVERAGE_FLAGS)" \
		LDFLAGS="$(COMMON_FLAGS) $(COVERAGE_LDFLAGS) -Wl,--as-needed" \
		LTO_FLAGS="" GPU_CXXFLAGS="" GPU_OBJS="$(COVERAGE_OBJDIR)/gpu/gpu_backend_none.o $(COVERAGE_OBJDIR)/gpu/gpu_autotune.o $(COVERAGE_OBJDIR)/gpu/multi_gpu_scheduler.o $(COVERAGE_OBJDIR)/gpu/async_pipeline.o"
	@echo ""
	@echo "Running tests for coverage data..."
	./run_tests_cov
	@echo ""
	$(MAKE) coverage-report

run_tests_cov: directories $(TEST_OBJDIR) $(TEST_OBJS) $(TEST_SHARED_OBJS)
	$(CXX) $(LDFLAGS) $(TEST_OBJS) $(TEST_SHARED_OBJS) $(LDLIBS) -o $@

coverage-report:
	@echo "Generating coverage report..."
	@mkdir -p coverage
	@# Capture coverage data
	lcov --capture --directory $(COVERAGE_OBJDIR) --output-file coverage/coverage.info --ignore-errors mismatch 2>/dev/null || \
		lcov --capture --directory $(COVERAGE_OBJDIR) --output-file coverage/coverage.info 2>/dev/null || \
		echo "lcov capture completed with warnings"
	@# Filter out test files and system headers
	lcov --remove coverage/coverage.info '/usr/*' '*/tests/*' --output-file coverage/coverage.filtered.info 2>/dev/null || \
		echo "lcov filtering completed with warnings"
	@# Generate HTML report
	genhtml coverage/coverage.filtered.info --output-directory coverage/html --title "Keyhunt Test Coverage" 2>/dev/null || \
		echo "genhtml completed with warnings"
	@echo ""
	@echo "Coverage report generated in coverage/html/index.html"
	@# Print summary
	@lcov --summary coverage/coverage.filtered.info 2>/dev/null || true

clean-coverage:
	$(RM) -r $(COVERAGE_OBJDIR) run_tests_cov coverage *.gcda *.gcno

.PHONY: sanitize run_tests_asan clean-sanitize tsan run_tests_tsan clean-tsan
.PHONY: coverage run_tests_cov coverage-report clean-coverage

# ============================================================================
# Fuzzing Targets
# ============================================================================
# Build JSON parser fuzzer with libFuzzer (requires clang)
# Usage: make fuzz
#        ./fuzz_json tests/fuzz_corpus/
#
# For AFL: afl-g++ -g -O1 tests/fuzz_json.cpp -o fuzz_json_afl
#          afl-fuzz -i tests/fuzz_corpus -o fuzz_out ./fuzz_json_afl @@
# ============================================================================

FUZZ_CXX ?= clang++
FUZZ_FLAGS = -g -O1 -fno-omit-frame-pointer -fsanitize=fuzzer,address

fuzz_json: tests/fuzz_json.cpp
	$(FUZZ_CXX) $(FUZZ_FLAGS) $< -o $@
	@echo ""
	@echo "Fuzzer built successfully. Run with:"
	@echo "  ./fuzz_json tests/fuzz_corpus/"
	@echo ""

fuzz: fuzz_json

fuzz_afl: tests/fuzz_json.cpp
	afl-g++ -g -O1 -D__AFL_COMPILER $< -o fuzz_json_afl
	@echo ""
	@echo "AFL fuzzer built. Run with:"
	@echo "  afl-fuzz -i tests/fuzz_corpus -o fuzz_out ./fuzz_json_afl @@"
	@echo ""

# ============================================================================
# Benchmarks
# ============================================================================
# Build standalone benchmark executables for performance testing
#
# Usage:
#   make benchmark_intgroup     # Build IntGroup::ModInv() benchmark
#   ./benchmark_intgroup         # Run the benchmark
# ============================================================================

# IntGroup::ModInv() benchmark - compares original vs optimized
benchmark_intgroup: directories $(SECP256K1_OBJS)
	$(CXX) $(CXXFLAGS) -mavx2 $(SRCDIR)/benchmarks/benchmark_intgroup.cpp $(SECP256K1_OBJS) $(LDFLAGS) $(LDLIBS) -o $@
	@echo ""
	@echo "IntGroup benchmark built successfully. Run with:"
	@echo "  ./benchmark_intgroup"
	@echo ""

# IntGroup AVX2 unit tests
test_intgroup_avx2: directories $(SECP256K1_OBJS)
	$(CXX) $(CXXFLAGS) -mavx2 tests/test_intgroup_avx2.cpp $(SECP256K1_OBJS) $(LDFLAGS) $(LDLIBS) -o $@
	@echo ""
	@echo "IntGroup AVX2 test suite built successfully. Run with:"
	@echo "  ./test_intgroup_avx2"
	@echo ""

.PHONY: fuzz fuzz_afl
