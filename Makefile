CXX ?= g++
CC ?= gcc

COMMON_FLAGS := -m64 -march=native -mtune=native -mssse3
OPT_FLAGS := -O3 -ftree-vectorize -funroll-loops -pipe -DNDEBUG
WARN_FLAGS := -Wall -Wextra

CXXFLAGS ?=
CFLAGS ?=

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
NVCCFLAGS ?= -O3 -std=c++17 -arch=$(CUDA_ARCH) -allow-unsupported-compiler
# Optional: point nvcc to a compatible host compiler (e.g. gcc-13)
CUDA_CC_BINDIR ?=
ifneq ($(CUDA_CC_BINDIR),)
  NVCCFLAGS += --compiler-bindir=$(CUDA_CC_BINDIR)
endif
HAVE_NVCC := $(shell command -v $(NVCC) 2>/dev/null)
ifeq ($(HAVE_NVCC),)
  GPU_OBJS := src/gpu/gpu_backend_none.o src/gpu/gpu_autotune.o src/gpu/multi_gpu_scheduler.o src/gpu/async_pipeline.o
  GPU_CXXFLAGS :=
else
  GPU_OBJS := src/gpu/gpu_backend_cuda.o src/gpu/gpu_autotune.o src/gpu/multi_gpu_scheduler.o src/gpu/async_pipeline.o
  GPU_CXXFLAGS := -DHAVE_CUDA_BACKEND=1
endif

CXXFLAGS += $(COMMON_FLAGS) $(OPT_FLAGS) $(WARN_FLAGS) -Wno-deprecated-copy -std=gnu++17 $(LTO_FLAGS) -fno-exceptions
CFLAGS += $(COMMON_FLAGS) $(OPT_FLAGS) $(WARN_FLAGS) $(LTO_FLAGS) -Wno-unused-parameter -Wno-unused-result
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

BLOOM_OBJS := src/oldbloom/bloom.o src/bloom/bloom.o src/bloom/bloom_simd.o
HASH_OBJS := src/hash/ripemd160.o src/hash/ripemd160_sse.o src/hash/ripemd160_avx2.o src/hash/ripemd160_avx512.o src/hash/sha256.o src/hash/sha256_sse.o src/hash/sha256_avx2.o src/hash/sha256_shani.o
SHA3_OBJS := src/sha3/sha3.o src/sha3/keccak.o
SECP256K1_OBJS := src/secp256k1/Int.o src/secp256k1/Point.o src/secp256k1/SECP256K1.o src/secp256k1/IntMod.o src/secp256k1/Random.o src/secp256k1/IntGroup.o
GMP256K1_OBJS := src/gmp256k1/Int.o src/gmp256k1/Point.o src/gmp256k1/GMP256K1.o src/gmp256k1/IntMod.o src/gmp256k1/Random.o src/gmp256k1/IntGroup.o
BSGS_OBJS := src/bsgs/bsgs_ops.o src/bsgs/bsgs_fast.o
HYBRID_OBJS := src/hybrid/adaptive_scheduler.o
UTIL_OBJS := src/util/mempool.o
DIST_OBJS := src/distributed/distributed.o
OUTPUT_OBJS := src/output.o
PROGRESS_OBJS := src/progress.o
BENCHMARK_OBJS := src/benchmark.o
CLI_OBJS := src/cli.o
WIZARD_OBJS := src/wizard/wizard.o src/wizard/wizard_config.o src/wizard/wizard_ui.o src/wizard/wizard_community.o src/wizard/wizard_server.o src/wizard/wizard_client.o
CORE_OBJS := src/core/util.o src/core/sysinfo.o src/core/parameter_validator.o src/core/config.o

COMMON_OBJS := src/base58/base58.o src/rmd160/rmd160.o src/xxhash/xxhash.o $(CORE_OBJS) $(GPU_OBJS) $(BLOOM_OBJS) $(HASH_OBJS) $(SHA3_OBJS) $(BSGS_OBJS) $(HYBRID_OBJS) $(UTIL_OBJS) $(DIST_OBJS) $(OUTPUT_OBJS) $(PROGRESS_OBJS) $(BENCHMARK_OBJS) $(CLI_OBJS)

KEYHUNT_OBJS := keyhunt.o $(COMMON_OBJS) $(SECP256K1_OBJS) $(WIZARD_OBJS)
BSGSD_OBJS := bsgsd.o $(COMMON_OBJS) $(SECP256K1_OBJS)
LEGACY_OBJS := keyhunt_legacy.o src/core/hashing.o $(COMMON_OBJS) $(GMP256K1_OBJS)

.PHONY: all clean legacy bsgsd

all: keyhunt

keyhunt: $(KEYHUNT_OBJS)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@

bsgsd: $(BSGSD_OBJS)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@

legacy: keyhunt_legacy

keyhunt_legacy: $(LEGACY_OBJS)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -lcrypto -lgmp -o $@

clean:
	$(RM) keyhunt keyhunt_legacy bsgsd
	$(RM) $(KEYHUNT_OBJS) $(BSGSD_OBJS) $(LEGACY_OBJS)
	$(RM) $(BSGS_OBJS) $(HYBRID_OBJS) $(UTIL_OBJS) $(DIST_OBJS) $(OUTPUT_OBJS) $(PROGRESS_OBJS) $(BENCHMARK_OBJS) $(CLI_OBJS) src/bloom/bloom_simd.o src/hash/sha256_shani.o
	$(RM) src/gpu/gpu_autotune.o src/gpu/gpu_backend_none.o src/gpu/gpu_backend_cuda.o src/gpu/multi_gpu_scheduler.o src/gpu/async_pipeline.o

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cu
	$(NVCC) $(NVCCFLAGS) -c $< -o $@

src/core/util.o: src/core/util.c
	$(CXX) $(CXXFLAGS) -c $< -o $@

src/core/hashing.o: src/core/hashing.c
	$(CXX) $(CXXFLAGS) -c $< -o $@

src/sha3/sha3.o: src/sha3/sha3.c
	$(CXX) $(CXXFLAGS) -c $< -o $@

src/sha3/keccak.o: src/sha3/keccak.c
	$(CXX) $(CXXFLAGS) -c $< -o $@

# AVX2 optimized builds
src/hash/ripemd160_avx2.o: src/hash/ripemd160_avx2.cpp
	$(CXX) $(CXXFLAGS) -mavx2 -c $< -o $@

src/hash/sha256_avx2.o: src/hash/sha256_avx2.cpp
	$(CXX) $(CXXFLAGS) -mavx2 -c $< -o $@

# AVX-512 optimized builds
src/hash/ripemd160_avx512.o: src/hash/ripemd160_avx512.cpp
	$(CXX) $(CXXFLAGS) -mavx512f -mavx512dq -c $< -o $@

# SHA-NI optimized builds (Intel SHA Extensions)
src/hash/sha256_shani.o: src/hash/sha256_shani.cpp
	$(CXX) $(CXXFLAGS) -msha -msse4.1 -c $< -o $@

# SIMD bloom filter (AVX2/AVX-512)
src/bloom/bloom_simd.o: src/bloom/bloom_simd.cpp
	$(CXX) $(CXXFLAGS) -mavx2 -c $< -o $@

# BSGS optimized modules
src/bsgs/bsgs_ops.o: src/bsgs/bsgs_ops.cpp
	$(CXX) $(CXXFLAGS) -mavx2 -c $< -o $@

src/bsgs/bsgs_fast.o: src/bsgs/bsgs_fast.cpp
	$(CXX) $(CXXFLAGS) -mavx2 -c $< -o $@
