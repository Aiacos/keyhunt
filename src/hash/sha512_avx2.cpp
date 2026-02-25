/*
 * AVX2 optimized SHA-512 implementation
 * Processes 4 hashes in parallel (64-bit operations)
 *
 * Based on VanitySearch by Jean Luc PONS
 * AVX2 optimization for keyhunt
 */

#include "sha512_avx2.h"
#include "sha512.h"
#include <immintrin.h>
#include <cpuid.h>
#include <string.h>
#include <stdint.h>

#if defined(__i386__) || defined(__x86_64__)
static inline uint64_t xgetbv_u32(uint32_t index) {
    uint32_t eax, edx;
    __asm__ volatile (".byte 0x0f, 0x01, 0xd0" : "=a"(eax), "=d"(edx) : "c"(index));
    return ((uint64_t)edx << 32) | eax;
}

static inline int os_avx_enabled(void) {
    unsigned int eax, ebx, ecx, edx;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return 0;
    if (!(ecx & bit_OSXSAVE)) return 0;
    if (!(ecx & bit_AVX)) return 0;
    return (xgetbv_u32(0) & 0x6u) == 0x6u; /* XMM (bit1) + YMM (bit2) state enabled */
}
#endif

// Check CPU support for AVX2
int sha512_avx2_available(void) {
    unsigned int eax, ebx, ecx, edx;

    // Check for AVX2 support (CPUID function 7, subleaf 0, EBX bit 5)
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        if ((ebx & (1 << 5)) == 0) return 0;  // AVX2 bit
#if defined(__i386__) || defined(__x86_64__)
        return os_avx_enabled();
#else
        return 1;
#endif
    }
    return 0;
}

namespace _sha512avx2
{

#ifdef WIN64
  static const __declspec(align(32)) uint64_t _init[] = {
#else
  static const uint64_t _init[] __attribute__ ((aligned (32))) = {
#endif
      // 4 copies of SHA512 initial state (for 4-way parallel processing)
      0x6a09e667f3bcc908ULL,0x6a09e667f3bcc908ULL,0x6a09e667f3bcc908ULL,0x6a09e667f3bcc908ULL,
      0xbb67ae8584caa73bULL,0xbb67ae8584caa73bULL,0xbb67ae8584caa73bULL,0xbb67ae8584caa73bULL,
      0x3c6ef372fe94f82bULL,0x3c6ef372fe94f82bULL,0x3c6ef372fe94f82bULL,0x3c6ef372fe94f82bULL,
      0xa54ff53a5f1d36f1ULL,0xa54ff53a5f1d36f1ULL,0xa54ff53a5f1d36f1ULL,0xa54ff53a5f1d36f1ULL,
      0x510e527fade682d1ULL,0x510e527fade682d1ULL,0x510e527fade682d1ULL,0x510e527fade682d1ULL,
      0x9b05688c2b3e6c1fULL,0x9b05688c2b3e6c1fULL,0x9b05688c2b3e6c1fULL,0x9b05688c2b3e6c1fULL,
      0x1f83d9abfb41bd6bULL,0x1f83d9abfb41bd6bULL,0x1f83d9abfb41bd6bULL,0x1f83d9abfb41bd6bULL,
      0x5be0cd19137e2179ULL,0x5be0cd19137e2179ULL,0x5be0cd19137e2179ULL,0x5be0cd19137e2179ULL
  };

// AVX2 SHA-512 macros using 256-bit registers (4-way parallel, 64-bit operations)
#define Maj(b,c,d) _mm256_or_si256(_mm256_and_si256(b, c), _mm256_and_si256(d, _mm256_or_si256(b, c)))
#define Ch(b,c,d)  _mm256_xor_si256(_mm256_and_si256(b, c), _mm256_andnot_si256(b, d))
#define ROR64(x,n) _mm256_or_si256(_mm256_srli_epi64(x, n), _mm256_slli_epi64(x, 64 - n))
#define SHR64(x,n) _mm256_srli_epi64(x, n)

/* SHA512 Functions - AVX2 versions with 64-bit rotations */
#define S0(x) (_mm256_xor_si256(ROR64((x), 28), _mm256_xor_si256(ROR64((x), 34), ROR64((x), 39))))
#define S1(x) (_mm256_xor_si256(ROR64((x), 14), _mm256_xor_si256(ROR64((x), 18), ROR64((x), 41))))
#define s0(x) (_mm256_xor_si256(ROR64((x), 1), _mm256_xor_si256(ROR64((x), 8), SHR64((x), 7))))
#define s1(x) (_mm256_xor_si256(ROR64((x), 19), _mm256_xor_si256(ROR64((x), 61), SHR64((x), 6))))

#define add4(x0, x1, x2, x3) _mm256_add_epi64(_mm256_add_epi64(x0, x1), _mm256_add_epi64(x2, x3))
#define add3(x0, x1, x2)     _mm256_add_epi64(_mm256_add_epi64(x0, x1), x2)
#define add5(x0, x1, x2, x3, x4) _mm256_add_epi64(add3(x0, x1, x2), _mm256_add_epi64(x3, x4))

#define Round(a, b, c, d, e, f, g, h, k, w)                 \
    T1 = add5(h, S1(e), Ch(e, f, g), _mm256_set1_epi64x(k), w); \
    d = _mm256_add_epi64(d, T1);                            \
    T2 = _mm256_add_epi64(S0(a), Maj(a, b, c));             \
    h = _mm256_add_epi64(T1, T2);

  // SHA-512 round constants
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

#define WMIX() \
  w0 = add4(s1(w14), w9, s0(w1), w0);   \
  w1 = add4(s1(w15), w10, s0(w2), w1);  \
  w2 = add4(s1(w0), w11, s0(w3), w2);   \
  w3 = add4(s1(w1), w12, s0(w4), w3);   \
  w4 = add4(s1(w2), w13, s0(w5), w4);   \
  w5 = add4(s1(w3), w14, s0(w6), w5);   \
  w6 = add4(s1(w4), w15, s0(w7), w6);   \
  w7 = add4(s1(w5), w0, s0(w8), w7);    \
  w8 = add4(s1(w6), w1, s0(w9), w8);    \
  w9 = add4(s1(w7), w2, s0(w10), w9);   \
  w10 = add4(s1(w8), w3, s0(w11), w10); \
  w11 = add4(s1(w9), w4, s0(w12), w11); \
  w12 = add4(s1(w10), w5, s0(w13), w12); \
  w13 = add4(s1(w11), w6, s0(w14), w13); \
  w14 = add4(s1(w12), w7, s0(w15), w14); \
  w15 = add4(s1(w13), w8, s0(w0), w15);

  // Initialize state
  void Initialize(__m256i *s) {
    memcpy(s, _init, sizeof(_init));
  }

  // Perform 4 SHA-512 in parallel using AVX2
  void Transform(__m256i *s, uint64_t *b0, uint64_t *b1, uint64_t *b2, uint64_t *b3)
  {
    __m256i a, b, c, d, e, f, g, h;
    __m256i w0, w1, w2, w3, w4, w5, w6, w7;
    __m256i w8, w9, w10, w11, w12, w13, w14, w15;
    __m256i T1, T2;

    a = _mm256_load_si256(s + 0);
    b = _mm256_load_si256(s + 1);
    c = _mm256_load_si256(s + 2);
    d = _mm256_load_si256(s + 3);
    e = _mm256_load_si256(s + 4);
    f = _mm256_load_si256(s + 5);
    g = _mm256_load_si256(s + 6);
    h = _mm256_load_si256(s + 7);

    // Load data from 4 different message blocks (transpose operation)
    // Each block has 16 x 64-bit words
    w0 = _mm256_set_epi64x(b0[0], b1[0], b2[0], b3[0]);
    w1 = _mm256_set_epi64x(b0[1], b1[1], b2[1], b3[1]);
    w2 = _mm256_set_epi64x(b0[2], b1[2], b2[2], b3[2]);
    w3 = _mm256_set_epi64x(b0[3], b1[3], b2[3], b3[3]);
    w4 = _mm256_set_epi64x(b0[4], b1[4], b2[4], b3[4]);
    w5 = _mm256_set_epi64x(b0[5], b1[5], b2[5], b3[5]);
    w6 = _mm256_set_epi64x(b0[6], b1[6], b2[6], b3[6]);
    w7 = _mm256_set_epi64x(b0[7], b1[7], b2[7], b3[7]);
    w8 = _mm256_set_epi64x(b0[8], b1[8], b2[8], b3[8]);
    w9 = _mm256_set_epi64x(b0[9], b1[9], b2[9], b3[9]);
    w10 = _mm256_set_epi64x(b0[10], b1[10], b2[10], b3[10]);
    w11 = _mm256_set_epi64x(b0[11], b1[11], b2[11], b3[11]);
    w12 = _mm256_set_epi64x(b0[12], b1[12], b2[12], b3[12]);
    w13 = _mm256_set_epi64x(b0[13], b1[13], b2[13], b3[13]);
    w14 = _mm256_set_epi64x(b0[14], b1[14], b2[14], b3[14]);
    w15 = _mm256_set_epi64x(b0[15], b1[15], b2[15], b3[15]);

    // 80 rounds of SHA-512 (first 16 rounds)
    Round(a, b, c, d, e, f, g, h, K[0], w0);
    Round(h, a, b, c, d, e, f, g, K[1], w1);
    Round(g, h, a, b, c, d, e, f, K[2], w2);
    Round(f, g, h, a, b, c, d, e, K[3], w3);
    Round(e, f, g, h, a, b, c, d, K[4], w4);
    Round(d, e, f, g, h, a, b, c, K[5], w5);
    Round(c, d, e, f, g, h, a, b, K[6], w6);
    Round(b, c, d, e, f, g, h, a, K[7], w7);
    Round(a, b, c, d, e, f, g, h, K[8], w8);
    Round(h, a, b, c, d, e, f, g, K[9], w9);
    Round(g, h, a, b, c, d, e, f, K[10], w10);
    Round(f, g, h, a, b, c, d, e, K[11], w11);
    Round(e, f, g, h, a, b, c, d, K[12], w12);
    Round(d, e, f, g, h, a, b, c, K[13], w13);
    Round(c, d, e, f, g, h, a, b, K[14], w14);
    Round(b, c, d, e, f, g, h, a, K[15], w15);

    WMIX()

    // Rounds 16-31
    Round(a, b, c, d, e, f, g, h, K[16], w0);
    Round(h, a, b, c, d, e, f, g, K[17], w1);
    Round(g, h, a, b, c, d, e, f, K[18], w2);
    Round(f, g, h, a, b, c, d, e, K[19], w3);
    Round(e, f, g, h, a, b, c, d, K[20], w4);
    Round(d, e, f, g, h, a, b, c, K[21], w5);
    Round(c, d, e, f, g, h, a, b, K[22], w6);
    Round(b, c, d, e, f, g, h, a, K[23], w7);
    Round(a, b, c, d, e, f, g, h, K[24], w8);
    Round(h, a, b, c, d, e, f, g, K[25], w9);
    Round(g, h, a, b, c, d, e, f, K[26], w10);
    Round(f, g, h, a, b, c, d, e, K[27], w11);
    Round(e, f, g, h, a, b, c, d, K[28], w12);
    Round(d, e, f, g, h, a, b, c, K[29], w13);
    Round(c, d, e, f, g, h, a, b, K[30], w14);
    Round(b, c, d, e, f, g, h, a, K[31], w15);

    WMIX()

    // Rounds 32-47
    Round(a, b, c, d, e, f, g, h, K[32], w0);
    Round(h, a, b, c, d, e, f, g, K[33], w1);
    Round(g, h, a, b, c, d, e, f, K[34], w2);
    Round(f, g, h, a, b, c, d, e, K[35], w3);
    Round(e, f, g, h, a, b, c, d, K[36], w4);
    Round(d, e, f, g, h, a, b, c, K[37], w5);
    Round(c, d, e, f, g, h, a, b, K[38], w6);
    Round(b, c, d, e, f, g, h, a, K[39], w7);
    Round(a, b, c, d, e, f, g, h, K[40], w8);
    Round(h, a, b, c, d, e, f, g, K[41], w9);
    Round(g, h, a, b, c, d, e, f, K[42], w10);
    Round(f, g, h, a, b, c, d, e, K[43], w11);
    Round(e, f, g, h, a, b, c, d, K[44], w12);
    Round(d, e, f, g, h, a, b, c, K[45], w13);
    Round(c, d, e, f, g, h, a, b, K[46], w14);
    Round(b, c, d, e, f, g, h, a, K[47], w15);

    WMIX()

    // Rounds 48-63
    Round(a, b, c, d, e, f, g, h, K[48], w0);
    Round(h, a, b, c, d, e, f, g, K[49], w1);
    Round(g, h, a, b, c, d, e, f, K[50], w2);
    Round(f, g, h, a, b, c, d, e, K[51], w3);
    Round(e, f, g, h, a, b, c, d, K[52], w4);
    Round(d, e, f, g, h, a, b, c, K[53], w5);
    Round(c, d, e, f, g, h, a, b, K[54], w6);
    Round(b, c, d, e, f, g, h, a, K[55], w7);
    Round(a, b, c, d, e, f, g, h, K[56], w8);
    Round(h, a, b, c, d, e, f, g, K[57], w9);
    Round(g, h, a, b, c, d, e, f, K[58], w10);
    Round(f, g, h, a, b, c, d, e, K[59], w11);
    Round(e, f, g, h, a, b, c, d, K[60], w12);
    Round(d, e, f, g, h, a, b, c, K[61], w13);
    Round(c, d, e, f, g, h, a, b, K[62], w14);
    Round(b, c, d, e, f, g, h, a, K[63], w15);

    WMIX()

    // Rounds 64-79
    Round(a, b, c, d, e, f, g, h, K[64], w0);
    Round(h, a, b, c, d, e, f, g, K[65], w1);
    Round(g, h, a, b, c, d, e, f, K[66], w2);
    Round(f, g, h, a, b, c, d, e, K[67], w3);
    Round(e, f, g, h, a, b, c, d, K[68], w4);
    Round(d, e, f, g, h, a, b, c, K[69], w5);
    Round(c, d, e, f, g, h, a, b, K[70], w6);
    Round(b, c, d, e, f, g, h, a, K[71], w7);
    Round(a, b, c, d, e, f, g, h, K[72], w8);
    Round(h, a, b, c, d, e, f, g, K[73], w9);
    Round(g, h, a, b, c, d, e, f, K[74], w10);
    Round(f, g, h, a, b, c, d, e, K[75], w11);
    Round(e, f, g, h, a, b, c, d, K[76], w12);
    Round(d, e, f, g, h, a, b, c, K[77], w13);
    Round(c, d, e, f, g, h, a, b, K[78], w14);
    Round(b, c, d, e, f, g, h, a, K[79], w15);

    // Add back to state
    _mm256_store_si256(s + 0, _mm256_add_epi64(s[0], a));
    _mm256_store_si256(s + 1, _mm256_add_epi64(s[1], b));
    _mm256_store_si256(s + 2, _mm256_add_epi64(s[2], c));
    _mm256_store_si256(s + 3, _mm256_add_epi64(s[3], d));
    _mm256_store_si256(s + 4, _mm256_add_epi64(s[4], e));
    _mm256_store_si256(s + 5, _mm256_add_epi64(s[5], f));
    _mm256_store_si256(s + 6, _mm256_add_epi64(s[6], g));
    _mm256_store_si256(s + 7, _mm256_add_epi64(s[7], h));
  }

} // namespace _sha512avx2

// Public API functions (stubs for now, to be implemented in later subtasks)

void sha512avx2(
    uint64_t *i0, uint64_t *i1, uint64_t *i2, uint64_t *i3,
    uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3,
    int length)
{
    // TODO: Implement in subtask-1-3
    // This will perform 4 parallel SHA-512 hashes
}

void sha512avx2_hmac(
    uint8_t *key0, uint8_t *key1, uint8_t *key2, uint8_t *key3,
    int key_length,
    uint8_t *msg0, uint8_t *msg1, uint8_t *msg2, uint8_t *msg3,
    int msg_length,
    uint8_t *d0, uint8_t *d1, uint8_t *d2, uint8_t *d3)
{
    // TODO: Implement HMAC-SHA512 using AVX2 (subtask-1-4)
    // This will compute HMAC-SHA512 for 4 different messages in parallel
}
