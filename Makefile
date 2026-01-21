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
  GPU_OBJS := gpu/gpu_backend_none.o
  GPU_CXXFLAGS :=
else
  GPU_OBJS := gpu/gpu_backend_cuda.o
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

BLOOM_OBJS := oldbloom/bloom.o bloom/bloom.o bloom/bloom_simd.o
HASH_OBJS := hash/ripemd160.o hash/ripemd160_sse.o hash/ripemd160_avx2.o hash/ripemd160_avx512.o hash/sha256.o hash/sha256_sse.o hash/sha256_avx2.o hash/sha256_shani.o
SHA3_OBJS := sha3/sha3.o sha3/keccak.o
SECP256K1_OBJS := secp256k1/Int.o secp256k1/Point.o secp256k1/SECP256K1.o secp256k1/IntMod.o secp256k1/Random.o secp256k1/IntGroup.o
GMP256K1_OBJS := gmp256k1/Int.o gmp256k1/Point.o gmp256k1/GMP256K1.o gmp256k1/IntMod.o gmp256k1/Random.o gmp256k1/IntGroup.o
BSGS_OBJS := bsgs/bsgs_ops.o bsgs/bsgs_fast.o
HYBRID_OBJS := hybrid/adaptive_scheduler.o
UTIL_OBJS := util/mempool.o
DIST_OBJS := distributed/distributed.o
WIZARD_OBJS := wizard/wizard.o wizard/wizard_config.o wizard/wizard_ui.o wizard/wizard_community.o wizard/wizard_server.o wizard/wizard_client.o

COMMON_OBJS := base58/base58.o rmd160/rmd160.o xxhash/xxhash.o util.o sysinfo.o parameter_validator.o config.o $(GPU_OBJS) $(BLOOM_OBJS) $(HASH_OBJS) $(SHA3_OBJS) $(BSGS_OBJS) $(HYBRID_OBJS) $(UTIL_OBJS) $(DIST_OBJS)

KEYHUNT_OBJS := keyhunt.o $(COMMON_OBJS) $(SECP256K1_OBJS) $(WIZARD_OBJS)
BSGSD_OBJS := bsgsd.o $(COMMON_OBJS) $(SECP256K1_OBJS)
LEGACY_OBJS := keyhunt_legacy.o hashing.o $(COMMON_OBJS) $(GMP256K1_OBJS)

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
	$(RM) $(KEYHUNT_OBJS) $(BSGSD_OBJS) $(LEGACY_OBJS) parameter_validator.o config.o
	$(RM) $(BSGS_OBJS) $(HYBRID_OBJS) $(UTIL_OBJS) $(DIST_OBJS) bloom/bloom_simd.o hash/sha256_shani.o

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cu
	$(NVCC) $(NVCCFLAGS) -c $< -o $@

util.o: util.c
	$(CXX) $(CXXFLAGS) -c $< -o $@

hashing.o: hashing.c
	$(CXX) $(CXXFLAGS) -c $< -o $@

sha3/sha3.o: sha3/sha3.c
	$(CXX) $(CXXFLAGS) -c $< -o $@

sha3/keccak.o: sha3/keccak.c
	$(CXX) $(CXXFLAGS) -c $< -o $@

# AVX2 optimized builds
hash/ripemd160_avx2.o: hash/ripemd160_avx2.cpp
	$(CXX) $(CXXFLAGS) -mavx2 -c $< -o $@

hash/sha256_avx2.o: hash/sha256_avx2.cpp
	$(CXX) $(CXXFLAGS) -mavx2 -c $< -o $@

# AVX-512 optimized builds
hash/ripemd160_avx512.o: hash/ripemd160_avx512.cpp
	$(CXX) $(CXXFLAGS) -mavx512f -mavx512dq -c $< -o $@

# SHA-NI optimized builds (Intel SHA Extensions)
hash/sha256_shani.o: hash/sha256_shani.cpp
	$(CXX) $(CXXFLAGS) -msha -msse4.1 -c $< -o $@

# SIMD bloom filter (AVX2/AVX-512)
bloom/bloom_simd.o: bloom/bloom_simd.cpp
	$(CXX) $(CXXFLAGS) -mavx2 -c $< -o $@

# BSGS optimized modules
bsgs/bsgs_ops.o: bsgs/bsgs_ops.cpp
	$(CXX) $(CXXFLAGS) -mavx2 -c $< -o $@

bsgs/bsgs_fast.o: bsgs/bsgs_fast.cpp
	$(CXX) $(CXXFLAGS) -mavx2 -c $< -o $@
