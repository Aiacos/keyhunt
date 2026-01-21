# Algorithm Reference

This document provides technical details about the cryptographic algorithms, data structures, and optimizations used in keyhunt for Bitcoin private key searching.

---

## 1. Address Generation Pipeline

Bitcoin address generation follows a deterministic pipeline from private key to address. Understanding this pipeline is essential for optimizing key search operations.

### Complete Pipeline

```
Private Key (256-bit integer)
    |
    v
[secp256k1 Scalar Multiplication: P = k * G]
    |
    v
Public Key (uncompressed: 65 bytes, compressed: 33 bytes)
    |
    v
[SHA-256 Hash]
    |
    v
SHA-256 Digest (32 bytes)
    |
    v
[RIPEMD-160 Hash]
    |
    v
Hash160 / PubKeyHash (20 bytes)
    |
    v
[Version Byte + Base58Check Encoding]
    |
    v
Bitcoin Address (25-34 characters)
```

### Step-by-Step Breakdown

#### 1.1 Private Key to Public Key (secp256k1)

The secp256k1 elliptic curve is defined by:

```
y^2 = x^3 + 7 (mod p)

where:
  p = 2^256 - 2^32 - 977
  p = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F
```

Generator point G and curve order n:

```c
// From SECP256K1.cpp - curve initialization
G.x = 0x79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798
G.y = 0x483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8
n   = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
```

Public key computation uses a precomputed generator table (GTable) for efficient scalar multiplication:

```cpp
// Precomputed table: 256 * 32 = 8192 points
// GTable[i*256 + j] = (j+1) * 2^(8*i) * G
Point Secp256K1::ComputePublicKey(Int *privKey) {
    Point Q;
    Q.Clear();
    for (int i = 0; i < 32; i++) {
        uint8_t b = privKey->GetByte(i);
        if (b)
            Q = Add2(Q, GTable[256 * i + (b-1)]);
    }
    Q.Reduce();
    return Q;
}
```

**Complexity**: O(32) point additions using precomputed table (vs O(256) with naive double-and-add).

#### 1.2 Public Key Encoding

**Uncompressed (65 bytes)**:
```
04 | X-coordinate (32 bytes) | Y-coordinate (32 bytes)
```

**Compressed (33 bytes)**:
```
02 | X-coordinate (32 bytes)    if Y is even
03 | X-coordinate (32 bytes)    if Y is odd
```

Example from code:
```cpp
// Compressed key encoding (SECP256K1.cpp)
publicKeyBytes[0] = pubKey.y.IsEven() ? 0x02 : 0x03;
pubKey.x.Get32Bytes(publicKeyBytes + 1);
```

#### 1.3 SHA-256 Hashing

Input sizes:
- Compressed public key: 33 bytes -> 1 block
- Uncompressed public key: 65 bytes -> 2 blocks

```cpp
// SHA-256 for compressed key (single block)
void sha256_33(uint8_t *input, uint8_t *digest);

// SHA-256 for uncompressed key (two blocks)
void sha256_65(uint8_t *input, uint8_t *digest);
```

#### 1.4 RIPEMD-160 Hashing

The SHA-256 output (32 bytes) is hashed with RIPEMD-160 to produce the 20-byte Hash160:

```cpp
void ripemd160_32(uint8_t *sha256_digest, uint8_t *hash160);
```

This is the **performance-critical bottleneck** in address mode searching.

#### 1.5 Base58Check Encoding

Final address encoding adds version byte and checksum:

```
Version (1 byte) | Hash160 (20 bytes) | Checksum (4 bytes)
         |                                    |
         v                                    v
   0x00 for mainnet P2PKH            SHA256(SHA256(version||hash160))[0:4]
   0x05 for mainnet P2SH
```

The 25-byte payload is then Base58 encoded:

```cpp
// From libbase58.h
bool b58check_enc(char *b58c, size_t *b58c_sz, uint8_t ver,
                  const void *data, size_t datasz);
```

---

## 2. SIMD Optimization Levels

keyhunt uses runtime CPU feature detection to select the optimal SIMD implementation. The hashing pipeline is parallelized to process multiple keys simultaneously.

### Optimization Hierarchy

| Level | Technology | Vector Width | Parallel Hashes | Relative Speed |
|-------|-----------|--------------|-----------------|----------------|
| Baseline | Scalar | 32-bit | 1 | 1x |
| SSE2 | 128-bit SIMD | 128-bit | 4 | ~3.5x |
| AVX2 | 256-bit SIMD | 256-bit | 8 | ~7x |
| AVX-512 | 512-bit SIMD | 512-bit | 16 | ~12x |

### Runtime Detection

```cpp
// Global flag set during initialization (keyhunt.cpp:84)
static bool g_avx2_available = false;

// Detection function (ripemd160_avx2.cpp)
int ripemd160_avx2_available(void) {
    unsigned int eax, ebx, ecx, edx;
    // CPUID function 7, subleaf 0, EBX bit 5 = AVX2
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        if ((ebx & (1 << 5)) == 0) return 0;
        return os_avx_enabled();  // Check OS YMM state support
    }
    return 0;
}
```

System information structure (sysinfo.h):
```c
typedef struct {
    bool has_avx2;
    bool has_avx512;
    bool has_avx512f;      // Foundation
    bool has_avx512dq;     // Doubleword/Quadword
    bool has_avx512bw;     // Byte/Word
    bool has_avx512vl;     // Vector Length Extensions
    bool has_sha_ni;       // Intel SHA extensions
} system_info_t;
```

### SSE2 Implementation (4-way parallel)

Processes 4 hashes in parallel using 128-bit XMM registers:

```cpp
// 4-way parallel RIPEMD-160
void ripemd160sse_32(
    unsigned char *i0, unsigned char *i1,
    unsigned char *i2, unsigned char *i3,
    unsigned char *d0, unsigned char *d1,
    unsigned char *d2, unsigned char *d3
);

// 4-way parallel SHA-256
void sha256sse_1B(
    uint32_t *i0, uint32_t *i1, uint32_t *i2, uint32_t *i3,
    uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3
);
```

### AVX2 Implementation (8-way parallel)

Processes 8 hashes in parallel using 256-bit YMM registers:

```cpp
// From ripemd160_avx2.cpp
#define ROL(x,n) _mm256_or_si256(_mm256_slli_epi32(x, n), \
                                 _mm256_srli_epi32(x, 32 - n))

// Boolean functions using AVX2 intrinsics
#define f1(x,y,z) _mm256_xor_si256(x, _mm256_xor_si256(y, z))
#define f2(x,y,z) _mm256_or_si256(_mm256_and_si256(x,y), \
                                  _mm256_andnot_si256(x,z))

// 8 copies of initial state for parallel processing
static const uint32_t _init[] __attribute__((aligned(32))) = {
    0x67452301, 0x67452301, 0x67452301, 0x67452301,
    0x67452301, 0x67452301, 0x67452301, 0x67452301,
    // ... (5 state words x 8 lanes)
};
```

The AVX2 implementation achieves approximately **2x speedup** over SSE2 by doubling the vector width.

### AVX-512 Implementation (16-way parallel)

For CPUs with AVX-512 support (Skylake-X, Ice Lake, Zen 4):

```cpp
// 16-way parallel using 512-bit ZMM registers
// Requires: AVX-512F + AVX-512DQ + AVX-512BW
void ripemd160_avx512_32(uint8_t inputs[16][32], uint8_t outputs[16][20]);
```

### SHA-NI Hardware Acceleration

Intel SHA Extensions provide hardware SHA-256 acceleration:

```cpp
// Hardware-accelerated SHA-256 using SHA-NI instructions
// ~3-4x faster than software implementation
bool has_sha_ni;  // Detected at runtime
```

### GetHash160 Function Variants

The main hashing function selects optimal implementation at runtime:

```cpp
// Standard 4-way SSE2 (SECP256K1.cpp)
void Secp256K1::GetHash160(int type, bool compressed,
    Point &k0, Point &k1, Point &k2, Point &k3,
    uint8_t *h0, uint8_t *h1, uint8_t *h2, uint8_t *h3);

// AVX2 8-way variant
void Secp256K1::GetHash160_AVX2(...);

// From X-coordinate only (skip Y computation)
void Secp256K1::GetHash160_fromX(int type, unsigned char prefix,
    Int *k0, Int *k1, Int *k2, Int *k3,
    uint8_t *h0, uint8_t *h1, uint8_t *h2, uint8_t *h3);
```

---

## 3. Bloom Filter Architecture

keyhunt uses a three-tier bloom filter hierarchy for fast negative lookups before expensive binary search operations.

### Bloom Filter Fundamentals

A bloom filter is a probabilistic data structure that can tell you:
- **Definitely NOT in set** (no false negatives)
- **Probably in set** (possible false positives)

Optimal parameters from Wikipedia:
```
bits = (entries * ln(error)) / ln(2)^2
hashes = bpe * ln(2)

where:
  entries = expected number of elements
  error = desired false positive rate
  bpe = bits per element
```

### Three-Tier Hierarchy

```
                    +------------------+
                    |     bloom3       |  <- Ultra-reduced filter
                    |  1/1024 of bloom1|     First-pass screening
                    +------------------+
                            |
                            v (if hit)
                    +------------------+
                    |     bloom2       |  <- Reduced filter
                    |  1/32 of bloom1  |     Second-pass verification
                    +------------------+
                            |
                            v (if hit)
                    +------------------+
                    |     bloom1       |  <- Main filter
                    | 3.5 bytes/element|     Final bloom check
                    +------------------+
                            |
                            v (if hit)
                    +------------------+
                    |  Binary Search   |  <- Sorted array lookup
                    |  Definitive match|     Confirms true positive
                    +------------------+
```

### Memory Usage

| Filter | Size Ratio | Bytes/Element | Purpose |
|--------|-----------|---------------|---------|
| bloom1 | 1x | 3.5 | Main filter, low false positive |
| bloom2 | 1/32x | ~0.11 | Pre-filter for bloom1 |
| bloom3 | 1/1024x | ~0.0034 | Ultra-fast pre-screening |

### Bloom Filter Structure

```c
// From bloom/bloom.h
struct bloom {
    uint64_t entries;     // Expected number of entries
    uint64_t bits;        // Total bits in filter
    uint64_t bytes;       // Total bytes (bits/8)
    uint8_t hashes;       // Number of hash functions
    long double error;    // Target false positive rate
    double bpe;           // Bits per element
    uint8_t *bf;          // Bit field pointer
};
```

### Batch Bloom Checking

keyhunt processes bloom filter checks in batches of 64 with prefetching:

```cpp
// From keyhunt.cpp - batch bloom check
const size_t BATCH_SIZE = 64;
const size_t HASH_STRIDE = 20;  // RMD160 hash size

// Check 64 hashes at once, return bitmask of hits
uint64_t hits = bloom_ext_check_rmd160_strided(
    &bloom,
    (const uint8_t*)hashCompressed02[base],
    HASH_STRIDE,
    batchCount
);

// Process hits using bit manipulation
while (hits) {
    int i = __builtin_ctzll(hits);  // Count trailing zeros
    hits &= hits - 1;               // Clear lowest bit
    size_t idx = base + (size_t)i;

    // Only perform expensive binary search on bloom hits
    if (searchbinary(addressTable, hashCompressed02[idx], N)) {
        // Found! Write key to output
    }
}
```

### False Positive Handling

When a bloom filter reports a potential match, binary search confirms:

```cpp
int searchbinary(address_t *table, uint8_t *hash, int N) {
    int left = 0, right = N - 1;
    while (left <= right) {
        int mid = (left + right) / 2;
        int cmp = memcmp(table[mid].value, hash, 20);
        if (cmp == 0) return 1;      // True positive
        if (cmp < 0) left = mid + 1;
        else right = mid - 1;
    }
    return 0;  // False positive from bloom
}
```

---

## 4. BSGS Algorithm Details

The Baby Step Giant Step algorithm reduces the complexity of discrete logarithm from O(N) to O(sqrt(N)) with O(sqrt(N)) memory trade-off.

### Mathematical Foundation

**Problem**: Given public key P and generator G, find private key k where:
```
P = k * G
```

**BSGS Approach**:

1. Choose m = ceil(sqrt(N)) where N is the search range size

2. Express k as: k = i*m + j, where 0 <= j < m and 0 <= i < m

3. Rewrite: P = (i*m + j) * G = i*m*G + j*G

4. Rearrange: P - i*m*G = j*G

**Baby Steps**: Precompute and store j*G for j = 0, 1, ..., m-1

**Giant Steps**: For i = 0, 1, 2, ..., compute P - i*m*G and check against baby step table

**Collision**: When P - i*m*G = j*G, then k = i*m + j

### Algorithm Pseudocode

```
function BSGS(P, G, N):
    m = ceil(sqrt(N))

    // Baby steps - precompute table
    table = {}
    point = Identity
    for j = 0 to m-1:
        table[point.x] = j
        point = point + G

    // Giant step multiplier
    giant_step = -m * G  // negative for subtraction

    // Giant steps - search for collision
    gamma = P
    for i = 0 to m-1:
        if gamma.x in table:
            j = table[gamma.x]
            return i*m + j
        gamma = gamma + giant_step

    return NOT_FOUND
```

### Memory Formula

The BSGS mode uses bloom filters instead of hash tables for memory efficiency:

```
N = search range size (e.g., 2^66 - 2^65 for puzzle 66)
M = sqrt(N) = number of baby steps
K = multiplier factor (increases coverage per iteration)

Memory breakdown:
  bloom1 = M * K * 3.5 bytes        (main bloom filter)
  bloom2 = M * K * 3.5 / 32 bytes   (secondary filter)
  bloom3 = M * K * 3.5 / 1024 bytes (tertiary filter)
  bP_table = M / 32 * K * 16 bytes  (point storage)

Total RAM = bloom1 + bloom2 + bloom3 + bP_table
```

### Example Calculation

**For puzzle 66** (range 2^65 to 2^66):

```
N = 2^65 (range size)
M = 2^32.5 ≈ 6.07 billion (impractical)
```

With practical parameters (N = 2^40, K = 4):
```
M = 2^20 = 1,048,576 baby steps
K = 4 multiplier

bloom1 = 1,048,576 * 4 * 3.5 = 14.7 MB
bloom2 = 14.7 MB / 32 = 0.46 MB
bloom3 = 14.7 MB / 1024 = 0.014 MB
bP_table = (1,048,576 / 32) * 4 * 16 = 2.1 MB

Total ≈ 17.3 MB RAM
```

**For larger ranges** (N = 2^50, K = 128):
```
M = 2^25 = 33.5 million baby steps

bloom1 = 33.5M * 128 * 3.5 = 15 GB
bloom2 = 15 GB / 32 = 480 MB
bloom3 = 15 GB / 1024 = 15 MB
bP_table = (33.5M / 32) * 128 * 16 = 2.1 GB

Total ≈ 17.6 GB RAM
```

### BSGS Batch Operations

The implementation uses batched processing with prefetching:

```cpp
// From bsgs/bsgs_ops.cpp
int bsgs_batch_bloom_check(bsgs_batch_ctx_t *ctx, void *bloom_array, int num_points) {
    struct bloom *blooms = (struct bloom*)bloom_array;
    int hits = 0;

    for (int i = 0; i < num_points; i++) {
        // Prefetch next xpoint
        if (i + PREFETCH_DISTANCE < num_points) {
            _mm_prefetch((const char*)(ctx->xpoint_raw + (i + PREFETCH_DISTANCE) * 32),
                        _MM_HINT_T0);
        }

        uint8_t *xpoint = ctx->xpoint_raw + i * 32;
        uint8_t bloom_idx = xpoint[0];

        // Prefetch bloom filter
        _mm_prefetch((const char*)blooms[bloom_idx].bf, _MM_HINT_T0);

        if (bloom_check(&blooms[bloom_idx], (char*)xpoint, 32)) {
            ctx->bloom_results[i] = 1;
            hits++;
        }
    }
    return hits;
}
```

### File Caching System

BSGS precomputation can be saved and loaded to avoid recomputation:

```
keyhunt_bsgs_<params>.blm  - Bloom filter data
keyhunt_bsgs_<params>.tbl  - Baby step point table
```

Usage:
```bash
# Save precomputation to files
./keyhunt -m bsgs -f target.txt -b 66 -S bsgs_puzzle66

# Automatically loads if files exist on restart
./keyhunt -m bsgs -f target.txt -b 66 -S bsgs_puzzle66
```

---

## 5. Kangaroo Algorithm (External Reference)

The Pollard's Kangaroo algorithm is an alternative to BSGS with better memory characteristics. keyhunt does not implement this internally, but it is commonly used via Jean-Luc Pons' Kangaroo tool.

### Algorithm Overview

```
Given: P = k*G where k is in [a, b]
Find: k

Setup:
  - Wild kangaroos start at P
  - Tame kangaroos start at known point (a+b)/2 * G
  - Both perform random walks using same jump function

Collision Detection:
  - Distinguished points (DP): Points where hash has d leading zeros
  - Store DP in hash table with position
  - When wild and tame reach same DP, private key can be computed
```

### Complexity Comparison

| Algorithm | Time | Space | Use Case |
|-----------|------|-------|----------|
| Brute Force | O(N) | O(1) | Small ranges, no pubkey |
| BSGS | O(sqrt(N)) | O(sqrt(N)) | Known pubkey, has RAM |
| Kangaroo | O(sqrt(N)) | O(sqrt(N)/2^d) | Known pubkey, limited RAM |

### Distinguished Points

A point is "distinguished" if its x-coordinate has d leading zero bits:

```
if (point.x & ((1 << d) - 1)) == 0:
    store(point, position)
```

Memory usage scales as O(sqrt(N) / 2^d), trading computation for storage.

---

## 6. Endomorphism Optimization

The secp256k1 curve has an efficient endomorphism that allows checking multiple related keys per EC multiplication.

### Mathematical Background

secp256k1 has an endomorphism defined by:

```
lambda * P = (beta * P.x, P.y)

where:
  beta^3 = 1 (mod p)
  lambda^2 + lambda + 1 = 0 (mod n)

  beta = 0x7AE96A2B657C07106E64479EAC3434E99CF0497512F58995C1396C28719501EE
  lambda = 0x5363AD4CC05C30E0A5261C028812645A122E22EA20816678DF02967C1B23BD72
```

### Six Related Keys

For any private key k, these six keys produce points with the same x-coordinate (up to sign):

```
k, n-k           -> P, -P
lambda*k mod n   -> lambda*P
n - lambda*k     -> -lambda*P
lambda^2*k mod n -> lambda^2*P
n - lambda^2*k   -> -lambda^2*P
```

### WARNING: NOT Useful for Puzzles!

The endomorphism optimization is **counterproductive** for Bitcoin puzzles:

```
Problem: Searching for k in range [2^65, 2^66]

If k is in 66-bit range:
  - lambda*k mod n produces 256-bit result
  - Only ~1/6 chance it falls in target range
  - Checking 6 keys wastes 5x computation

Result: 6x MORE work for no benefit
```

**Use endomorphism only when**:
- Searching the full curve (no range restriction)
- Probability of finding related key equals original key

Enable with `-e` flag (disabled by default for puzzle solving):
```bash
./keyhunt -m address -f targets.txt -e  # Only for full-curve search
```

---

## 7. Key Modes Comparison

keyhunt supports six distinct search modes, each optimized for different scenarios.

### Mode Summary Table

| Mode | Flag | Input File | Algorithm | Time Complexity | Memory | Best For |
|------|------|-----------|-----------|-----------------|--------|----------|
| address | `-m address` | BTC addresses | Brute force + Bloom | O(N) | Low | General search |
| rmd160 | `-m rmd160` | RIPEMD160 hashes | Brute force + Bloom | O(N) | Low | Direct hash match |
| xpoint | `-m xpoint` | X-coordinates | EC comparison | O(N) | Low | Known partial pubkey |
| bsgs | `-m bsgs` | Full public keys | Baby-Giant Step | O(sqrt(N)) | High | Known full pubkey |
| pub2rmd | `-m pub2rmd` | RIPEMD160 hashes | EC + Hash search | O(N) | Medium | Research |
| vanity | `-m vanity` | Address prefix | Generation | O(58^prefix_len) | Low | Custom addresses |

### Mode Details

#### ADDRESS Mode (`-m address`)

**Input**: Base58-encoded Bitcoin addresses (P2PKH, P2SH, or Bech32)

**Process**:
1. Generate private key in range
2. Compute public key (EC multiplication)
3. Hash: SHA256 -> RIPEMD160
4. Check against bloom filter
5. Binary search on hit

**Example**:
```bash
./keyhunt -m address -f addresses.txt -b 66 -l compress -R -q
```

#### RMD160 Mode (`-m rmd160`)

**Input**: Raw RIPEMD160 hashes (20 bytes hex)

**Process**: Same as address mode, but skips Base58 decoding. Faster for pre-decoded targets.

**Example**:
```bash
./keyhunt -m rmd160 -f hashes.rmd -b 66 -R
```

#### XPOINT Mode (`-m xpoint`)

**Input**: Public key X-coordinates (32 bytes hex)

**Process**:
1. Generate private key
2. Compute public key
3. Compare X-coordinate directly (skip hashing)

**Advantage**: Fastest mode when public key X-coordinate is known (no hashing required).

**Example**:
```bash
./keyhunt -m xpoint -f xpoints.txt -b 125 -R -q
```

#### BSGS Mode (`-m bsgs`)

**Input**: Full public keys (compressed or uncompressed)

**Process**: Baby Step Giant Step algorithm (see Section 4)

**Advantage**: O(sqrt(N)) complexity for known public keys.

**Example**:
```bash
./keyhunt -m bsgs -f pubkeys.txt -b 66 -n 0x100000000000 -k 128
```

#### PUB2RMD Mode (`-m pub2rmd`)

**Input**: RIPEMD160 hashes

**Process**: Research mode attempting to find public keys that hash to given RIPEMD160.

**Example**:
```bash
./keyhunt -m pub2rmd -f targets.rmd -b 66 -R
```

#### VANITY Mode (`-m vanity`)

**Input**: Desired address prefix

**Process**: Generate keys until address matches prefix.

**Example**:
```bash
./keyhunt -m vanity -f prefix.txt  # prefix.txt contains "1ABC"
```

---

## 8. File Caching System

BSGS mode supports saving and loading precomputed data to avoid expensive recomputation.

### File Formats

```
keyhunt_bsgs_<N>_<K>.blm    # Bloom filter serialization
keyhunt_bsgs_<N>_<K>.tbl    # Baby step point table

Header format:
  - Magic number (4 bytes)
  - Version (2 bytes)
  - N parameter (8 bytes)
  - K factor (4 bytes)
  - Checksum (4 bytes)
```

### Usage

```bash
# First run: compute and save
./keyhunt -m bsgs -f target.txt -b 66 -n 0x1000000000000 -k 128 -S bsgs_cache

# Subsequent runs: load from cache
./keyhunt -m bsgs -f target.txt -b 66 -S bsgs_cache
# Output: "Loading cached bloom filters and bP table..."
```

### Cache Validation

The cache is validated on load:
- Magic number verification
- Version compatibility check
- Parameter matching (N, K must match)
- Checksum verification

If validation fails, cache is recomputed.

---

## 9. Thread Scaling

keyhunt parallelizes key generation and checking across multiple CPU threads.

### Scaling Characteristics

| Thread Count | Scaling | Notes |
|--------------|---------|-------|
| 1 to physical cores | Near-linear | Optimal performance |
| Physical to logical | Sub-linear | Hyperthreading overhead |
| Beyond logical cores | Diminishing | Context switching overhead |

### Auto-Tuning

The system automatically detects optimal thread count:

```c
// From sysinfo.h
typedef struct {
    int cpu_physical_cores;     // Physical cores (without HT)
    int cpu_logical_cores;      // Logical cores (with HT)
    int recommended_threads;    // Auto-tuned recommendation
} system_info_t;
```

### Thread Work Distribution

Each thread processes a batch of keys independently:

```cpp
// Thread workload (keyhunt.cpp)
uint32_t THREADBPWORKLOAD = 1048576;  // Keys per thread iteration

// Work distribution
void *thread_process(void *arg) {
    int thread_id = (int)(intptr_t)arg;
    Int key_start = range_start + (thread_id * stride);

    while (!found && key_start < range_end) {
        process_batch(key_start, CPU_GRP_SIZE);
        key_start.Add(&total_stride);
    }
}
```

### Manual Thread Configuration

Override auto-tuning with `-t` flag:

```bash
# Use 8 threads explicitly
./keyhunt -m address -f targets.txt -b 66 -t 8

# Let system auto-tune
./keyhunt -m address -f targets.txt -b 66
```

### Optimal Configuration

For best performance:
1. Use physical core count for compute-bound work
2. Avoid oversubscription (more threads than cores)
3. Consider NUMA topology for multi-socket systems
4. Batch size should be multiple of SIMD width (8 for AVX2)

```bash
# Check system capabilities
./keyhunt --sysinfo

# Example output:
# CPU: 8 physical cores, 16 logical cores
# Recommended threads: 16
# AVX2: YES, AVX-512: NO
# Recommended batch size: 1024
```

---

## References

1. SEC 2: Recommended Elliptic Curve Domain Parameters - https://www.secg.org/sec2-v2.pdf
2. Bitcoin Wiki: Technical Background of Addresses - https://en.bitcoin.it/wiki/Technical_background_of_Bitcoin_addresses
3. Bloom Filter - Wikipedia - https://en.wikipedia.org/wiki/Bloom_filter
4. Baby-step Giant-step - Wikipedia - https://en.wikipedia.org/wiki/Baby-step_giant-step
5. Pollard's Kangaroo Algorithm - https://en.wikipedia.org/wiki/Pollard%27s_kangaroo_algorithm
6. secp256k1 Endomorphism - https://bitcointalk.org/index.php?topic=3238.0
