/*
 * AVX-512 optimized SHA-512 implementation
 * Processes 8 hashes in parallel (64-bit operations)
 *
 * Based on VanitySearch by Jean Luc PONS
 * AVX-512 optimization for keyhunt
 */

#include "sha512_avx512.h"
#include "sha512.h"
#include <string.h>
#include <immintrin.h>
#include <cpuid.h>
#include <stdio.h>
#include <stdint.h>

#if defined(__i386__) || defined(__x86_64__)
static inline uint64_t xgetbv_u32(uint32_t index) {
    uint32_t eax, edx;
    __asm__ volatile (".byte 0x0f, 0x01, 0xd0" : "=a"(eax), "=d"(edx) : "c"(index));
    return ((uint64_t)edx << 32) | eax;
}

static inline int os_avx512_enabled(void) {
    unsigned int eax, ebx, ecx, edx;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return 0;
    if (!(ecx & bit_OSXSAVE)) return 0;
    if (!(ecx & bit_AVX)) return 0;
    /* XCR0: require XMM, YMM, opmask, ZMM_hi256, Hi16_ZMM (bits 1,2,5,6,7). */
    return (xgetbv_u32(0) & 0xE6u) == 0xE6u;
}
#endif

// Check CPU support for AVX512F (AVX-512 Foundation)
int sha512_avx512_available(void) {
    unsigned int eax, ebx, ecx, edx;

    // Check for AVX512F support (CPUID function 7, subleaf 0, EBX bit 16)
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        if ((ebx & (1 << 16)) == 0) return 0;  // AVX512F bit
#if defined(__i386__) || defined(__x86_64__)
        return os_avx512_enabled();
#else
        return 1;
#endif
    }
    return 0;
}

// Internal AVX-512 SHA-512 implementation
namespace _sha512avx512 {

#ifdef WIN64
    static const __declspec(align(64)) uint64_t _init[] = {
#else
    static const uint64_t _init[] __attribute__ ((aligned (64))) = {
#endif
        // 8 copies of initial state (for 8-way parallel processing)
        0x6a09e667f3bcc908ULL, 0x6a09e667f3bcc908ULL, 0x6a09e667f3bcc908ULL, 0x6a09e667f3bcc908ULL,
        0x6a09e667f3bcc908ULL, 0x6a09e667f3bcc908ULL, 0x6a09e667f3bcc908ULL, 0x6a09e667f3bcc908ULL,
        0xbb67ae8584caa73bULL, 0xbb67ae8584caa73bULL, 0xbb67ae8584caa73bULL, 0xbb67ae8584caa73bULL,
        0xbb67ae8584caa73bULL, 0xbb67ae8584caa73bULL, 0xbb67ae8584caa73bULL, 0xbb67ae8584caa73bULL,
        0x3c6ef372fe94f82bULL, 0x3c6ef372fe94f82bULL, 0x3c6ef372fe94f82bULL, 0x3c6ef372fe94f82bULL,
        0x3c6ef372fe94f82bULL, 0x3c6ef372fe94f82bULL, 0x3c6ef372fe94f82bULL, 0x3c6ef372fe94f82bULL,
        0xa54ff53a5f1d36f1ULL, 0xa54ff53a5f1d36f1ULL, 0xa54ff53a5f1d36f1ULL, 0xa54ff53a5f1d36f1ULL,
        0xa54ff53a5f1d36f1ULL, 0xa54ff53a5f1d36f1ULL, 0xa54ff53a5f1d36f1ULL, 0xa54ff53a5f1d36f1ULL,
        0x510e527fade682d1ULL, 0x510e527fade682d1ULL, 0x510e527fade682d1ULL, 0x510e527fade682d1ULL,
        0x510e527fade682d1ULL, 0x510e527fade682d1ULL, 0x510e527fade682d1ULL, 0x510e527fade682d1ULL,
        0x9b05688c2b3e6c1fULL, 0x9b05688c2b3e6c1fULL, 0x9b05688c2b3e6c1fULL, 0x9b05688c2b3e6c1fULL,
        0x9b05688c2b3e6c1fULL, 0x9b05688c2b3e6c1fULL, 0x9b05688c2b3e6c1fULL, 0x9b05688c2b3e6c1fULL,
        0x1f83d9abfb41bd6bULL, 0x1f83d9abfb41bd6bULL, 0x1f83d9abfb41bd6bULL, 0x1f83d9abfb41bd6bULL,
        0x1f83d9abfb41bd6bULL, 0x1f83d9abfb41bd6bULL, 0x1f83d9abfb41bd6bULL, 0x1f83d9abfb41bd6bULL,
        0x5be0cd19137e2179ULL, 0x5be0cd19137e2179ULL, 0x5be0cd19137e2179ULL, 0x5be0cd19137e2179ULL,
        0x5be0cd19137e2179ULL, 0x5be0cd19137e2179ULL, 0x5be0cd19137e2179ULL, 0x5be0cd19137e2179ULL
    };

    static const uint64_t K[80] = {
        0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL,
        0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
        0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL,
        0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
        0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
        0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
        0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL,
        0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
        0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL,
        0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
        0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL,
        0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
        0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL,
        0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
        0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
        0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
        0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL,
        0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
        0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL,
        0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
        0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL,
        0xc67178f2e372532bULL, 0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
        0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL,
        0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
        0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
        0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
        0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
    };

// AVX-512 macros for SHA-512 (64-bit operations)
#define ROR64(x, n) _mm512_ror_epi64(x, n)

// SHA-512 functions using AVX-512
#define S0(x) (_mm512_xor_si512(_mm512_xor_si512(ROR64(x, 28), ROR64(x, 34)), ROR64(x, 39)))
#define S1(x) (_mm512_xor_si512(_mm512_xor_si512(ROR64(x, 14), ROR64(x, 18)), ROR64(x, 41)))
#define G0(x) (_mm512_xor_si512(_mm512_xor_si512(ROR64(x, 1), ROR64(x, 8)), _mm512_srli_epi64(x, 7)))
#define G1(x) (_mm512_xor_si512(_mm512_xor_si512(ROR64(x, 19), ROR64(x, 61)), _mm512_srli_epi64(x, 6)))

// Ch(e,f,g) = (e & f) ^ (~e & g) using ternarylogic
#define Ch(e, f, g) _mm512_ternarylogic_epi64(e, f, g, 0xCA)

// Maj(a,b,c) = (a & b) ^ (a & c) ^ (b & c) using ternarylogic
#define Maj(a, b, c) _mm512_ternarylogic_epi64(a, b, c, 0xE8)

// Round macro
#define ROUND(a, b, c, d, e, f, g, h, k, w) \
    { \
        __m512i t1 = _mm512_add_epi64(h, S1(e)); \
        t1 = _mm512_add_epi64(t1, Ch(e, f, g)); \
        t1 = _mm512_add_epi64(t1, _mm512_set1_epi64(k)); \
        t1 = _mm512_add_epi64(t1, w); \
        __m512i t2 = _mm512_add_epi64(S0(a), Maj(a, b, c)); \
        d = _mm512_add_epi64(d, t1); \
        h = _mm512_add_epi64(t1, t2); \
    }

// Load 8 64-bit words from 8 different message blocks (big-endian)
#define LOADW(i) _mm512_set_epi64( \
    __builtin_bswap64(*((const uint64_t *)blk[0] + i)), \
    __builtin_bswap64(*((const uint64_t *)blk[1] + i)), \
    __builtin_bswap64(*((const uint64_t *)blk[2] + i)), \
    __builtin_bswap64(*((const uint64_t *)blk[3] + i)), \
    __builtin_bswap64(*((const uint64_t *)blk[4] + i)), \
    __builtin_bswap64(*((const uint64_t *)blk[5] + i)), \
    __builtin_bswap64(*((const uint64_t *)blk[6] + i)), \
    __builtin_bswap64(*((const uint64_t *)blk[7] + i)))

    // Optimized transpose load with prefetching
    static inline void transpose_and_load(__m512i *w, const uint8_t *blk[8]) {
        // Prefetch all input blocks
        for (int i = 0; i < 8; i++) {
            _mm_prefetch((const char*)blk[i], _MM_HINT_T0);
        }

        // Load word by word from each block (first 16 words)
        for (int i = 0; i < 16; i++) {
            w[i] = LOADW(i);
        }
    }

    // Initialize SHA-512 state
    void Initialize(__m512i *s) {
        memcpy(s, _init, sizeof(_init));
    }

    // Perform 8 SHA-512 transforms in parallel using AVX-512
    void Transform(__m512i *s, const uint8_t *blk[8]) {
        __m512i a = _mm512_load_si512(s + 0);
        __m512i b = _mm512_load_si512(s + 1);
        __m512i c = _mm512_load_si512(s + 2);
        __m512i d = _mm512_load_si512(s + 3);
        __m512i e = _mm512_load_si512(s + 4);
        __m512i f = _mm512_load_si512(s + 5);
        __m512i g = _mm512_load_si512(s + 6);
        __m512i h = _mm512_load_si512(s + 7);
        __m512i w[80];

        // Load and expand message schedule
        transpose_and_load(w, blk);

        // Expand message schedule (W[16..79])
        for (int i = 16; i < 80; i++) {
            w[i] = _mm512_add_epi64(
                _mm512_add_epi64(w[i - 16], G0(w[i - 15])),
                _mm512_add_epi64(w[i - 7], G1(w[i - 2]))
            );
        }

        // 80 rounds
        for (int i = 0; i < 80; i += 8) {
            ROUND(a, b, c, d, e, f, g, h, K[i + 0], w[i + 0]);
            ROUND(h, a, b, c, d, e, f, g, K[i + 1], w[i + 1]);
            ROUND(g, h, a, b, c, d, e, f, K[i + 2], w[i + 2]);
            ROUND(f, g, h, a, b, c, d, e, K[i + 3], w[i + 3]);
            ROUND(e, f, g, h, a, b, c, d, K[i + 4], w[i + 4]);
            ROUND(d, e, f, g, h, a, b, c, K[i + 5], w[i + 5]);
            ROUND(c, d, e, f, g, h, a, b, K[i + 6], w[i + 6]);
            ROUND(b, c, d, e, f, g, h, a, K[i + 7], w[i + 7]);
        }

        // Add back to state
        _mm512_store_si512(s + 0, _mm512_add_epi64(s[0], a));
        _mm512_store_si512(s + 1, _mm512_add_epi64(s[1], b));
        _mm512_store_si512(s + 2, _mm512_add_epi64(s[2], c));
        _mm512_store_si512(s + 3, _mm512_add_epi64(s[3], d));
        _mm512_store_si512(s + 4, _mm512_add_epi64(s[4], e));
        _mm512_store_si512(s + 5, _mm512_add_epi64(s[5], f));
        _mm512_store_si512(s + 6, _mm512_add_epi64(s[6], g));
        _mm512_store_si512(s + 7, _mm512_add_epi64(s[7], h));
    }

} // namespace _sha512avx512

// Helper function to write uint64_t in big-endian format
static inline void write_be64(uint8_t *out, uint64_t val) {
    out[0] = (uint8_t)(val >> 56);
    out[1] = (uint8_t)(val >> 48);
    out[2] = (uint8_t)(val >> 40);
    out[3] = (uint8_t)(val >> 32);
    out[4] = (uint8_t)(val >> 24);
    out[5] = (uint8_t)(val >> 16);
    out[6] = (uint8_t)(val >> 8);
    out[7] = (uint8_t)(val);
}

// Public API: Process 16 SHA-512 hashes in parallel using AVX-512
// Each input is 128 bytes (SHA-512 block size), each output is 64 bytes
void sha512avx512_128(
    const uint8_t *i0,  const uint8_t *i1,  const uint8_t *i2,  const uint8_t *i3,
    const uint8_t *i4,  const uint8_t *i5,  const uint8_t *i6,  const uint8_t *i7,
    const uint8_t *i8,  const uint8_t *i9,  const uint8_t *i10, const uint8_t *i11,
    const uint8_t *i12, const uint8_t *i13, const uint8_t *i14, const uint8_t *i15,
    uint8_t *d0,  uint8_t *d1,  uint8_t *d2,  uint8_t *d3,
    uint8_t *d4,  uint8_t *d5,  uint8_t *d6,  uint8_t *d7,
    uint8_t *d8,  uint8_t *d9,  uint8_t *d10, uint8_t *d11,
    uint8_t *d12, uint8_t *d13, uint8_t *d14, uint8_t *d15)
{
    // AVX-512 processes 8 hashes at a time, so we need 2 rounds for 16 hashes
    __m512i s[8] __attribute__((aligned(64)));

    // First batch: process i0-i7 -> d0-d7
    const uint8_t *batch1[8] = { i0, i1, i2, i3, i4, i5, i6, i7 };
    _sha512avx512::Initialize(s);
    _sha512avx512::Transform(s, batch1);

    // Unpack results: Each s[i] contains 8 x 64-bit values for state word i
    // Layout: s[i] = [hash0_word_i, hash1_word_i, ..., hash7_word_i] (MSB to LSB based on _mm512_set_epi64 order)
    uint64_t *state_words = (uint64_t *)s;

    // Extract hashes 0-7 from first batch
    // Note: _mm512_set_epi64(e7,e6,e5,e4,e3,e2,e1,e0) puts e7 at lane 7, e0 at lane 0
    // Since batch1[0]=i0 goes to the first argument (e7), it ends up at lane 7
    // When cast to array: array[0]=lane0, array[7]=lane7
    // So: batch1[0]=i0 is at array[7], batch1[7]=i7 is at array[0]
    for (int i = 0; i < 8; i++) {  // For each state word
        write_be64(d0 + i * 8, state_words[i * 8 + 7]);  // hash from i0 (lane 7)
        write_be64(d1 + i * 8, state_words[i * 8 + 6]);  // hash from i1 (lane 6)
        write_be64(d2 + i * 8, state_words[i * 8 + 5]);  // hash from i2 (lane 5)
        write_be64(d3 + i * 8, state_words[i * 8 + 4]);  // hash from i3 (lane 4)
        write_be64(d4 + i * 8, state_words[i * 8 + 3]);  // hash from i4 (lane 3)
        write_be64(d5 + i * 8, state_words[i * 8 + 2]);  // hash from i5 (lane 2)
        write_be64(d6 + i * 8, state_words[i * 8 + 1]);  // hash from i6 (lane 1)
        write_be64(d7 + i * 8, state_words[i * 8 + 0]);  // hash from i7 (lane 0)
    }

    // Second batch: process i8-i15 -> d8-d15
    const uint8_t *batch2[8] = { i8, i9, i10, i11, i12, i13, i14, i15 };
    _sha512avx512::Initialize(s);
    _sha512avx512::Transform(s, batch2);

    state_words = (uint64_t *)s;

    // Extract hashes 8-15 from second batch
    for (int i = 0; i < 8; i++) {  // For each state word
        write_be64(d8  + i * 8, state_words[i * 8 + 7]);  // hash from i8 (lane 7)
        write_be64(d9  + i * 8, state_words[i * 8 + 6]);  // hash from i9 (lane 6)
        write_be64(d10 + i * 8, state_words[i * 8 + 5]);  // hash from i10 (lane 5)
        write_be64(d11 + i * 8, state_words[i * 8 + 4]);  // hash from i11 (lane 4)
        write_be64(d12 + i * 8, state_words[i * 8 + 3]);  // hash from i12 (lane 3)
        write_be64(d13 + i * 8, state_words[i * 8 + 2]);  // hash from i13 (lane 2)
        write_be64(d14 + i * 8, state_words[i * 8 + 1]);  // hash from i14 (lane 1)
        write_be64(d15 + i * 8, state_words[i * 8 + 0]);  // hash from i15 (lane 0)
    }
}

// Pad a message into a SHA-512 block (for messages <= 111 bytes)
static void sha512_pad_block(uint8_t block[128], const char *msg, size_t len) {
    memset(block, 0, 128);
    if (len > 0) memcpy(block, msg, len);
    block[len] = 0x80;
    uint64_t bit_len = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++)
        block[120 + i] = (uint8_t)(bit_len >> (56 - i * 8));
}

// Test function for AVX-512 SHA-512 implementation
void sha512avx512_test(void) {
    if (!sha512_avx512_available()) {
        printf("SHA512-AVX512: AVX-512 not available on this CPU\n");
        return;
    }

    const char *messages[16] = {
        "Test message 01 for AVX-512 SHA-512",
        "Test message 02 for AVX-512 SHA-512",
        "Test message 03 for AVX-512 SHA-512",
        "Test message 04 for AVX-512 SHA-512",
        "Test message 05 for AVX-512 SHA-512",
        "Test message 06 for AVX-512 SHA-512",
        "Test message 07 for AVX-512 SHA-512",
        "Test message 08 for AVX-512 SHA-512",
        "Test message 09 for AVX-512 SHA-512",
        "Test message 10 for AVX-512 SHA-512",
        "Test message 11 for AVX-512 SHA-512",
        "Test message 12 for AVX-512 SHA-512",
        "Test message 13 for AVX-512 SHA-512",
        "Test message 14 for AVX-512 SHA-512",
        "Test message 15 for AVX-512 SHA-512",
        "Test message 16 for AVX-512 SHA-512"
    };

    uint8_t input[16][128];
    uint8_t hash_simd[16][64];
    uint8_t hash_scalar[16][64];

    // Pre-pad inputs (SIMD processes raw 128-byte blocks, not padded messages)
    for (int i = 0; i < 16; i++)
        sha512_pad_block(input[i], messages[i], strlen(messages[i]));

    // Compute SIMD hashes (16-way parallel via 2x8-way)
    sha512avx512_128(
        input[0],  input[1],  input[2],  input[3],
        input[4],  input[5],  input[6],  input[7],
        input[8],  input[9],  input[10], input[11],
        input[12], input[13], input[14], input[15],
        hash_simd[0],  hash_simd[1],  hash_simd[2],  hash_simd[3],
        hash_simd[4],  hash_simd[5],  hash_simd[6],  hash_simd[7],
        hash_simd[8],  hash_simd[9],  hash_simd[10], hash_simd[11],
        hash_simd[12], hash_simd[13], hash_simd[14], hash_simd[15]
    );

    // Compute scalar reference hashes (sha512() handles padding internally)
    for (int i = 0; i < 16; i++) {
        sha512((unsigned char *)messages[i], (int)strlen(messages[i]), hash_scalar[i]);
    }

    // Compare results
    int passed = 0;
    for (int i = 0; i < 16; i++) {
        if (memcmp(hash_simd[i], hash_scalar[i], 64) == 0) {
            passed++;
        } else {
            printf("SHA512-AVX512 Test FAILED for input %d\n", i);
            printf("  Expected: ");
            for (int j = 0; j < 64; j++) printf("%02x", hash_scalar[i][j]);
            printf("\n  Got:      ");
            for (int j = 0; j < 64; j++) printf("%02x", hash_simd[i][j]);
            printf("\n");
        }
    }

    if (passed == 16) {
        printf("SHA512-AVX512 Test: PASS (16/16 hashes correct, 16-way parallel via 2x8-way SIMD)\n");
    } else {
        printf("SHA512-AVX512 Test: FAIL (%d/16 passed)\n", passed);
    }
}

// Stub functions for future implementation
void sha512avx512(
    uint64_t * /*i0*/, uint64_t * /*i1*/, uint64_t * /*i2*/, uint64_t * /*i3*/,
    uint64_t * /*i4*/, uint64_t * /*i5*/, uint64_t * /*i6*/, uint64_t * /*i7*/,
    uint8_t * /*d0*/, uint8_t * /*d1*/, uint8_t * /*d2*/, uint8_t * /*d3*/,
    uint8_t * /*d4*/, uint8_t * /*d5*/, uint8_t * /*d6*/, uint8_t * /*d7*/,
    int /*length*/)
{
    // TODO: Variable-length API (not required for current spec)
}

void sha512avx512_hmac(
    uint8_t * /*key0*/, uint8_t * /*key1*/, uint8_t * /*key2*/, uint8_t * /*key3*/,
    uint8_t * /*key4*/, uint8_t * /*key5*/, uint8_t * /*key6*/, uint8_t * /*key7*/,
    int /*key_length*/,
    uint8_t * /*msg0*/, uint8_t * /*msg1*/, uint8_t * /*msg2*/, uint8_t * /*msg3*/,
    uint8_t * /*msg4*/, uint8_t * /*msg5*/, uint8_t * /*msg6*/, uint8_t * /*msg7*/,
    int /*msg_length*/,
    uint8_t * /*d0*/, uint8_t * /*d1*/, uint8_t * /*d2*/, uint8_t * /*d3*/,
    uint8_t * /*d4*/, uint8_t * /*d5*/, uint8_t * /*d6*/, uint8_t * /*d7*/)
{
    // TODO: HMAC variant (not required for current spec)
}
