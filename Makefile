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

COMMON_OBJS := $(OBJDIR)/base58/base58.o $(OBJDIR)/rmd160/rmd160.o $(OBJDIR)/xxhash/xxhash.o $(CORE_OBJS) $(GPU_OBJS) $(BLOOM_OBJS) $(HASH_OBJS) $(SHA3_OBJS) $(BSGS_OBJS) $(HYBRID_OBJS) $(UTIL_OBJS) $(DIST_OBJS) $(OUTPUT_OBJS) $(PROGRESS_OBJS) $(BENCHMARK_OBJS) $(CLI_OBJS)

KEYHUNT_OBJS := $(OBJDIR)/keyhunt.o $(COMMON_OBJS) $(SECP256K1_OBJS) $(WIZARD_OBJS)
BSGSD_OBJS := $(OBJDIR)/bsgsd.o $(COMMON_OBJS) $(SECP256K1_OBJS)
LEGACY_OBJS := $(OBJDIR)/keyhunt_legacy.o $(OBJDIR)/core/hashing.o $(COMMON_OBJS) $(GMP256K1_OBJS)

# Create obj directory structure
OBJ_DIRS := $(OBJDIR) $(OBJDIR)/base58 $(OBJDIR)/rmd160 $(OBJDIR)/xxhash $(OBJDIR)/core $(OBJDIR)/gpu $(OBJDIR)/oldbloom $(OBJDIR)/bloom $(OBJDIR)/hash $(OBJDIR)/sha3 $(OBJDIR)/bsgs $(OBJDIR)/hybrid $(OBJDIR)/util $(OBJDIR)/distributed $(OBJDIR)/wizard $(OBJDIR)/secp256k1 $(OBJDIR)/gmp256k1

.PHONY: all clean legacy bsgsd directories

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
	$(RM) keyhunt keyhunt_legacy bsgsd
	$(RM) -r $(OBJDIR)

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
