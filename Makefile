CXX ?= g++
CC ?= gcc

COMMON_FLAGS := -m64 -march=native -mtune=native -mssse3
OPT_FLAGS := -O3 -ftree-vectorize -funroll-loops -pipe -DNDEBUG
WARN_FLAGS := -Wall -Wextra

CXXFLAGS ?=
CFLAGS ?=

LTO_FLAGS ?= -flto=auto

CXXFLAGS += $(COMMON_FLAGS) $(OPT_FLAGS) $(WARN_FLAGS) -Wno-deprecated-copy -std=gnu++17 $(LTO_FLAGS) -fno-exceptions
CFLAGS += $(COMMON_FLAGS) $(OPT_FLAGS) $(WARN_FLAGS) $(LTO_FLAGS) -Wno-unused-parameter -Wno-unused-result

LDFLAGS ?=
LDFLAGS += $(COMMON_FLAGS) $(LTO_FLAGS) -Wl,-O3 -Wl,--as-needed
LDLIBS ?=
LDLIBS += -lm -lpthread

BLOOM_OBJS := oldbloom/bloom.o bloom/bloom.o
HASH_OBJS := hash/ripemd160.o hash/ripemd160_sse.o hash/ripemd160_avx2.o hash/ripemd160_avx512.o hash/sha256.o hash/sha256_sse.o hash/sha256_avx2.o
SHA3_OBJS := sha3/sha3.o sha3/keccak.o
SECP256K1_OBJS := secp256k1/Int.o secp256k1/Point.o secp256k1/SECP256K1.o secp256k1/IntMod.o secp256k1/Random.o secp256k1/IntGroup.o
GMP256K1_OBJS := gmp256k1/Int.o gmp256k1/Point.o gmp256k1/GMP256K1.o gmp256k1/IntMod.o gmp256k1/Random.o gmp256k1/IntGroup.o

COMMON_OBJS := base58/base58.o rmd160/rmd160.o xxhash/xxhash.o util.o sysinfo.o $(BLOOM_OBJS) $(HASH_OBJS) $(SHA3_OBJS)

KEYHUNT_OBJS := keyhunt.o $(COMMON_OBJS) $(SECP256K1_OBJS)
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
	$(RM) $(KEYHUNT_OBJS) $(BSGSD_OBJS) $(LEGACY_OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

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
