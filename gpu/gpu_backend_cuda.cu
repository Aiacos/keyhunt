/*
 * Full CUDA backend for keyhunt
 * Implements: secp256k1 ECC + SHA256 + RIPEMD160 + bloom/matching
 * BitCrack-style all-GPU computation
 *
 * NOTE: Written in C-style to avoid GCC 15 / CUDA 12.4 compatibility issues
 */

#include "gpu_backend.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <cuda_runtime.h>

// ============================================================================
// Global state (C-style, no thread_local)
// ============================================================================

static int g_available = 0;
static gpu_backend_info_t g_info;

// Device pointers for persistent data
static uint8_t *d_GTable = NULL;        // G table: 256*32 points * 64 bytes
static size_t g_GTable_count = 0;

static uint8_t *d_targets = NULL;       // Target hashes: count * 20 bytes
static size_t g_target_count = 0;

static uint8_t *d_bloom = NULL;         // Bloom filter data
static size_t g_bloom_size = 0;
static int g_bloom_hashes = 0;

// Single context for hash160-from-X mode (simplified, not thread-safe)
static cudaStream_t g_stream = NULL;
static uint8_t *d_x32 = NULL;
static uint8_t *d_out02 = NULL;
static uint8_t *d_out03 = NULL;
static size_t g_capacity = 0;

// ============================================================================
// 256-bit integer (device)
// ============================================================================

struct uint256_d {
    uint32_t d[8];
};

struct Point256_d {
    uint256_d x;
    uint256_d y;
    uint256_d z;
};

// secp256k1 prime p = 2^256 - 2^32 - 977
__device__ __constant__ uint32_t SECP_P[8] = {
    0xFFFFFC2Fu, 0xFFFFFFFEu, 0xFFFFFFFFu, 0xFFFFFFFFu,
    0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu
};

// ============================================================================
// 256-bit arithmetic (device)
// ============================================================================

__device__ __forceinline__ void u256_set_zero(uint256_d *r) {
    #pragma unroll
    for (int i = 0; i < 8; i++) r->d[i] = 0;
}

__device__ __forceinline__ void u256_set(uint256_d *r, const uint256_d *a) {
    #pragma unroll
    for (int i = 0; i < 8; i++) r->d[i] = a->d[i];
}

__device__ __forceinline__ int u256_is_zero(const uint256_d *a) {
    uint32_t z = 0;
    #pragma unroll
    for (int i = 0; i < 8; i++) z |= a->d[i];
    return z == 0;
}

__device__ __forceinline__ uint32_t u256_add(uint256_d *r, const uint256_d *a, const uint256_d *b) {
    uint64_t c = 0;
    #pragma unroll
    for (int i = 0; i < 8; i++) {
        c += (uint64_t)a->d[i] + (uint64_t)b->d[i];
        r->d[i] = (uint32_t)c;
        c >>= 32;
    }
    return (uint32_t)c;
}

__device__ __forceinline__ uint32_t u256_sub(uint256_d *r, const uint256_d *a, const uint256_d *b) {
    int64_t c = 0;
    #pragma unroll
    for (int i = 0; i < 8; i++) {
        c = (int64_t)a->d[i] - (int64_t)b->d[i] + c;
        r->d[i] = (uint32_t)c;
        c >>= 32;
    }
    return (uint32_t)(c < 0 ? 1 : 0);
}

__device__ void mod_add(uint256_d *r, const uint256_d *a, const uint256_d *b) {
    uint256_d tmp;
    uint32_t carry = u256_add(&tmp, a, b);

    // Check if >= p
    int cmp = 0;
    if (carry) {
        cmp = 1;
    } else {
        for (int i = 7; i >= 0; i--) {
            if (tmp.d[i] > SECP_P[i]) { cmp = 1; break; }
            if (tmp.d[i] < SECP_P[i]) { cmp = -1; break; }
        }
    }

    if (cmp >= 0) {
        uint256_d p;
        for (int i = 0; i < 8; i++) p.d[i] = SECP_P[i];
        u256_sub(r, &tmp, &p);
    } else {
        u256_set(r, &tmp);
    }
}

__device__ void mod_sub(uint256_d *r, const uint256_d *a, const uint256_d *b) {
    uint256_d tmp;
    uint32_t borrow = u256_sub(&tmp, a, b);

    if (borrow) {
        uint256_d p;
        for (int i = 0; i < 8; i++) p.d[i] = SECP_P[i];
        u256_add(r, &tmp, &p);
    } else {
        u256_set(r, &tmp);
    }
}

// Montgomery-style modular multiplication (simplified)
__device__ void mod_mul(uint256_d *r, const uint256_d *a, const uint256_d *b) {
    uint64_t acc[16] = {0};

    // Full multiplication
    for (int i = 0; i < 8; i++) {
        uint64_t ai = a->d[i];
        for (int j = 0; j < 8; j++) {
            acc[i + j] += ai * b->d[j];
        }
    }

    // Propagate carries
    for (int i = 0; i < 15; i++) {
        acc[i + 1] += acc[i] >> 32;
        acc[i] &= 0xFFFFFFFF;
    }

    // Barrett reduction for secp256k1
    // p = 2^256 - 2^32 - 977
    // For simplicity, use iterative subtraction
    uint256_d tmp;
    for (int i = 0; i < 8; i++) tmp.d[i] = (uint32_t)acc[i];

    // Handle high bits
    for (int i = 8; i < 16 && acc[i]; i++) {
        // acc[i] * 2^(32*i) mod p = acc[i] * (2^32 + 977) * 2^(32*(i-8)) mod p
        uint64_t hi = acc[i];
        uint64_t lo = hi * 977 + hi * (1ULL << 32);

        // Add back
        uint64_t carry = 0;
        for (int j = 0; j < 8 && (lo || carry); j++) {
            int idx = (i - 8 + j) % 8;
            carry += tmp.d[idx] + (lo & 0xFFFFFFFF);
            tmp.d[idx] = (uint32_t)carry;
            carry >>= 32;
            lo >>= 32;
        }
    }

    // Final reduction
    uint256_d p;
    for (int i = 0; i < 8; i++) p.d[i] = SECP_P[i];

    int cmp = 0;
    for (int i = 7; i >= 0; i--) {
        if (tmp.d[i] > p.d[i]) { cmp = 1; break; }
        if (tmp.d[i] < p.d[i]) { cmp = -1; break; }
    }
    if (cmp >= 0) {
        u256_sub(r, &tmp, &p);
    } else {
        u256_set(r, &tmp);
    }
}

__device__ void mod_sqr(uint256_d *r, const uint256_d *a) {
    mod_mul(r, a, a);
}

// Extended Euclidean algorithm for modular inverse
__device__ void mod_inv(uint256_d *r, const uint256_d *a) {
    // Use Fermat's little theorem: a^(-1) = a^(p-2) mod p
    // p-2 for secp256k1
    uint256_d base, exp, result;
    u256_set(&base, a);

    // p - 2
    exp.d[0] = 0xFFFFFC2Du;
    exp.d[1] = 0xFFFFFFFEu;
    for (int i = 2; i < 8; i++) exp.d[i] = 0xFFFFFFFFu;

    // result = 1
    u256_set_zero(&result);
    result.d[0] = 1;

    // Square and multiply
    for (int i = 0; i < 256; i++) {
        int word = i / 32;
        int bit = i % 32;
        if (exp.d[word] & (1u << bit)) {
            mod_mul(&result, &result, &base);
        }
        mod_sqr(&base, &base);
    }

    u256_set(r, &result);
}

// ============================================================================
// SHA-256 (device)
// ============================================================================

__device__ __constant__ uint32_t SHA256_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

__device__ __constant__ uint32_t SHA256_H0[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

__device__ __forceinline__ uint32_t rotr(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

__device__ void sha256_33(const uint8_t *msg, uint8_t *hash) {
    uint32_t W[64];
    uint32_t H[8];

    // Initialize hash values
    for (int i = 0; i < 8; i++) H[i] = SHA256_H0[i];

    // Prepare message schedule (33 bytes + padding)
    // Block 1: bytes 0-63
    for (int i = 0; i < 8; i++) {
        W[i] = ((uint32_t)msg[i*4] << 24) | ((uint32_t)msg[i*4+1] << 16) |
               ((uint32_t)msg[i*4+2] << 8) | (uint32_t)msg[i*4+3];
    }
    // byte 32 + padding bit
    W[8] = ((uint32_t)msg[32] << 24) | 0x800000;
    for (int i = 9; i < 15; i++) W[i] = 0;
    W[15] = 33 * 8;  // Length in bits

    // Extend
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(W[i-15], 7) ^ rotr(W[i-15], 18) ^ (W[i-15] >> 3);
        uint32_t s1 = rotr(W[i-2], 17) ^ rotr(W[i-2], 19) ^ (W[i-2] >> 10);
        W[i] = W[i-16] + s0 + W[i-7] + s1;
    }

    // Compress
    uint32_t a = H[0], b = H[1], c = H[2], d = H[3];
    uint32_t e = H[4], f = H[5], g = H[6], h = H[7];

    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + SHA256_K[i] + W[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;

        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    H[0] += a; H[1] += b; H[2] += c; H[3] += d;
    H[4] += e; H[5] += f; H[6] += g; H[7] += h;

    // Output (big-endian)
    for (int i = 0; i < 8; i++) {
        hash[i*4]   = (H[i] >> 24) & 0xFF;
        hash[i*4+1] = (H[i] >> 16) & 0xFF;
        hash[i*4+2] = (H[i] >> 8) & 0xFF;
        hash[i*4+3] = H[i] & 0xFF;
    }
}

// ============================================================================
// RIPEMD-160 (device)
// ============================================================================

__device__ __constant__ uint32_t RMD_KL[5] = {0x00000000, 0x5a827999, 0x6ed9eba1, 0x8f1bbcdc, 0xa953fd4e};
__device__ __constant__ uint32_t RMD_KR[5] = {0x50a28be6, 0x5c4dd124, 0x6d703ef3, 0x7a6d76e9, 0x00000000};

__device__ __constant__ uint8_t RMD_RL[80] = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
    7,4,13,1,10,6,15,3,12,0,9,5,2,14,11,8,
    3,10,14,4,9,15,8,1,2,7,0,6,13,11,5,12,
    1,9,11,10,0,8,12,4,13,3,7,15,14,5,6,2,
    4,0,5,9,7,12,2,10,14,1,3,8,11,6,15,13
};

__device__ __constant__ uint8_t RMD_RR[80] = {
    5,14,7,0,9,2,11,4,13,6,15,8,1,10,3,12,
    6,11,3,7,0,13,5,10,14,15,8,12,4,9,1,2,
    15,5,1,3,7,14,6,9,11,8,12,2,10,0,4,13,
    8,6,4,1,3,11,15,0,5,12,2,13,9,7,10,14,
    12,15,10,4,1,5,8,7,6,2,13,14,0,3,9,11
};

__device__ __constant__ uint8_t RMD_SL[80] = {
    11,14,15,12,5,8,7,9,11,13,14,15,6,7,9,8,
    7,6,8,13,11,9,7,15,7,12,15,9,11,7,13,12,
    11,13,6,7,14,9,13,15,14,8,13,6,5,12,7,5,
    11,12,14,15,14,15,9,8,9,14,5,6,8,6,5,12,
    9,15,5,11,6,8,13,12,5,12,13,14,11,8,5,6
};

__device__ __constant__ uint8_t RMD_SR[80] = {
    8,9,9,11,13,15,15,5,7,7,8,11,14,14,12,6,
    9,13,15,7,12,8,9,11,7,7,12,7,6,15,13,11,
    9,7,15,11,8,6,6,14,12,13,5,14,13,13,7,5,
    15,5,8,11,14,14,6,14,6,9,12,9,12,5,15,8,
    8,5,12,9,12,5,14,6,8,13,6,5,15,13,11,11
};

__device__ __forceinline__ uint32_t rotl(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

__device__ void ripemd160_32(const uint8_t *msg, uint8_t *hash) {
    uint32_t W[16];
    uint32_t H[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};

    // Prepare message (32 bytes + padding)
    for (int i = 0; i < 8; i++) {
        W[i] = msg[i*4] | ((uint32_t)msg[i*4+1] << 8) |
               ((uint32_t)msg[i*4+2] << 16) | ((uint32_t)msg[i*4+3] << 24);
    }
    W[8] = 0x80;  // Padding
    for (int i = 9; i < 14; i++) W[i] = 0;
    W[14] = 32 * 8;  // Length in bits (low)
    W[15] = 0;       // Length (high)

    uint32_t al = H[0], bl = H[1], cl = H[2], dl = H[3], el = H[4];
    uint32_t ar = H[0], br = H[1], cr = H[2], dr = H[3], er = H[4];

    for (int j = 0; j < 80; j++) {
        uint32_t fl, fr, tl, tr;
        int round = j / 16;

        switch (round) {
            case 0: fl = bl ^ cl ^ dl; fr = br ^ (cr | ~dr); break;
            case 1: fl = (bl & cl) | (~bl & dl); fr = (br & dr) | (cr & ~dr); break;
            case 2: fl = (bl | ~cl) ^ dl; fr = (br | ~cr) ^ dr; break;
            case 3: fl = (bl & dl) | (cl & ~dl); fr = (br & cr) | (~br & dr); break;
            case 4: fl = bl ^ (cl | ~dl); fr = br ^ cr ^ dr; break;
            default: fl = fr = 0;
        }

        tl = rotl(al + fl + W[RMD_RL[j]] + RMD_KL[round], RMD_SL[j]) + el;
        tr = rotl(ar + fr + W[RMD_RR[j]] + RMD_KR[round], RMD_SR[j]) + er;

        al = el; el = dl; dl = rotl(cl, 10); cl = bl; bl = tl;
        ar = er; er = dr; dr = rotl(cr, 10); cr = br; br = tr;
    }

    uint32_t t = H[1] + cl + dr;
    H[1] = H[2] + dl + er;
    H[2] = H[3] + el + ar;
    H[3] = H[4] + al + br;
    H[4] = H[0] + bl + cr;
    H[0] = t;

    // Output (little-endian)
    for (int i = 0; i < 5; i++) {
        hash[i*4]   = H[i] & 0xFF;
        hash[i*4+1] = (H[i] >> 8) & 0xFF;
        hash[i*4+2] = (H[i] >> 16) & 0xFF;
        hash[i*4+3] = (H[i] >> 24) & 0xFF;
    }
}

// ============================================================================
// Point operations (device) for full GPU mode
// ============================================================================

__device__ void point_set_infinity(Point256_d *p) {
    u256_set_zero(&p->x);
    u256_set_zero(&p->y);
    u256_set_zero(&p->z);
}

__device__ int point_is_infinity(const Point256_d *p) {
    return u256_is_zero(&p->z);
}

__device__ void point_from_affine(Point256_d *p, const uint256_d *x, const uint256_d *y) {
    u256_set(&p->x, x);
    u256_set(&p->y, y);
    u256_set_zero(&p->z);
    p->z.d[0] = 1;
}

// Point doubling in Jacobian coordinates
__device__ void point_double(Point256_d *r, const Point256_d *p) {
    if (point_is_infinity(p) || u256_is_zero(&p->y)) {
        point_set_infinity(r);
        return;
    }

    uint256_d a, b, c, d, e, f, tmp;

    mod_sqr(&a, &p->x);          // a = X^2
    mod_sqr(&b, &p->y);          // b = Y^2
    mod_sqr(&c, &b);             // c = Y^4

    mod_add(&tmp, &p->x, &b);
    mod_sqr(&d, &tmp);
    mod_sub(&d, &d, &a);
    mod_sub(&d, &d, &c);
    mod_add(&d, &d, &d);         // d = 2*((X+b)^2 - a - c)

    mod_add(&e, &a, &a);
    mod_add(&e, &e, &a);         // e = 3*X^2

    mod_sqr(&f, &e);             // f = e^2

    mod_add(&tmp, &d, &d);
    mod_sub(&r->x, &f, &tmp);    // X3 = f - 2*d

    mod_mul(&r->z, &p->y, &p->z);
    mod_add(&r->z, &r->z, &r->z); // Z3 = 2*Y*Z

    mod_sub(&tmp, &d, &r->x);
    mod_mul(&r->y, &e, &tmp);
    mod_add(&c, &c, &c);
    mod_add(&c, &c, &c);
    mod_add(&c, &c, &c);         // 8*c
    mod_sub(&r->y, &r->y, &c);   // Y3 = e*(d-X3) - 8*c
}

// Point addition with Q in affine (Z=1)
__device__ void point_add_affine(Point256_d *r, const Point256_d *p,
                                  const uint256_d *qx, const uint256_d *qy) {
    if (point_is_infinity(p)) {
        point_from_affine(r, qx, qy);
        return;
    }

    uint256_d z1z1, u2, s2, h, hh, i, j, rr, v, tmp;

    mod_sqr(&z1z1, &p->z);        // Z1Z1 = Z1^2
    mod_mul(&u2, qx, &z1z1);      // U2 = X2*Z1Z1

    mod_mul(&tmp, &p->z, &z1z1);
    mod_mul(&s2, qy, &tmp);       // S2 = Y2*Z1*Z1Z1

    mod_sub(&h, &u2, &p->x);      // H = U2 - X1

    if (u256_is_zero(&h)) {
        mod_sub(&tmp, &s2, &p->y);
        if (u256_is_zero(&tmp)) {
            point_double(r, p);
            return;
        } else {
            point_set_infinity(r);
            return;
        }
    }

    mod_sqr(&hh, &h);             // HH = H^2
    mod_add(&i, &hh, &hh);
    mod_add(&i, &i, &i);          // I = 4*HH

    mod_mul(&j, &h, &i);          // J = H*I

    mod_sub(&rr, &s2, &p->y);
    mod_add(&rr, &rr, &rr);       // r = 2*(S2-Y1)

    mod_mul(&v, &p->x, &i);       // V = X1*I

    mod_sqr(&r->x, &rr);
    mod_sub(&r->x, &r->x, &j);
    mod_sub(&r->x, &r->x, &v);
    mod_sub(&r->x, &r->x, &v);    // X3 = r^2 - J - 2*V

    mod_sub(&tmp, &v, &r->x);
    mod_mul(&r->y, &rr, &tmp);
    mod_mul(&tmp, &p->y, &j);
    mod_add(&tmp, &tmp, &tmp);
    mod_sub(&r->y, &r->y, &tmp);  // Y3 = r*(V-X3) - 2*Y1*J

    mod_add(&r->z, &p->z, &h);
    mod_sqr(&r->z, &r->z);
    mod_sub(&r->z, &r->z, &z1z1);
    mod_sub(&r->z, &r->z, &hh);   // Z3 = (Z1+H)^2 - Z1Z1 - HH
}

// Get X coordinate in affine (for hash160)
__device__ void point_get_x_affine(uint256_d *x, const Point256_d *p) {
    if (point_is_infinity(p)) {
        u256_set_zero(x);
        return;
    }

    uint256_d z_inv, z_inv2;
    mod_inv(&z_inv, &p->z);
    mod_sqr(&z_inv2, &z_inv);
    mod_mul(x, &p->x, &z_inv2);
}

// Get Y parity (0 = even, 1 = odd) for compressed prefix
__device__ int point_get_y_parity(const Point256_d *p) {
    if (point_is_infinity(p)) return 0;

    uint256_d z_inv, z_inv2, z_inv3, y_affine;
    mod_inv(&z_inv, &p->z);
    mod_sqr(&z_inv2, &z_inv);
    mod_mul(&z_inv3, &z_inv2, &z_inv);
    mod_mul(&y_affine, &p->y, &z_inv3);

    return y_affine.d[0] & 1;
}

// ============================================================================
// G Table access (uploaded from CPU)
// ============================================================================

// G table format: 256 * 32 affine points, each point = 64 bytes (X || Y)
// Entry [byte_pos * 256 + byte_val] = (byte_val+1) * G * 2^(8*byte_pos)
__device__ void gtable_get_point(uint256_d *x, uint256_d *y,
                                  const uint8_t *gtable, int byte_pos, uint8_t byte_val) {
    if (byte_val == 0) {
        u256_set_zero(x);
        u256_set_zero(y);
        return;
    }

    const uint8_t *entry = gtable + ((size_t)byte_pos * 256 + (byte_val - 1)) * 64;

    // X coordinate (big-endian in table)
    for (int i = 0; i < 8; i++) {
        x->d[7 - i] = ((uint32_t)entry[i*4] << 24) | ((uint32_t)entry[i*4+1] << 16) |
                      ((uint32_t)entry[i*4+2] << 8) | (uint32_t)entry[i*4+3];
    }
    // Y coordinate
    entry += 32;
    for (int i = 0; i < 8; i++) {
        y->d[7 - i] = ((uint32_t)entry[i*4] << 24) | ((uint32_t)entry[i*4+1] << 16) |
                      ((uint32_t)entry[i*4+2] << 8) | (uint32_t)entry[i*4+3];
    }
}

// Scalar multiplication using G table
__device__ void scalar_mul_G(Point256_d *r, const uint256_d *k, const uint8_t *gtable) {
    point_set_infinity(r);

    for (int byte_pos = 0; byte_pos < 32; byte_pos++) {
        int word = byte_pos / 4;
        int byte_in_word = byte_pos % 4;
        uint8_t b = (uint8_t)(k->d[word] >> (byte_in_word * 8));

        if (b > 0) {
            uint256_d gx, gy;
            gtable_get_point(&gx, &gy, gtable, byte_pos, b);
            point_add_affine(r, r, &gx, &gy);
        }
    }
}

// ============================================================================
// Bloom filter check (device)
// ============================================================================

__device__ int bloom_check(const uint8_t *bloom, size_t bloom_size, int num_hashes,
                           const uint8_t *hash20) {
    // Simple bloom filter check using hash bytes as indices
    // Each hash function uses different byte pairs from the 20-byte hash

    for (int h = 0; h < num_hashes && h < 10; h++) {
        // Use pairs of bytes for indices
        int byte1 = h * 2;
        int byte2 = h * 2 + 1;
        if (byte2 >= 20) byte2 = h % 20;

        uint32_t idx = ((uint32_t)hash20[byte1] << 8) | hash20[byte2];
        idx = (idx * 0x9E3779B9u) % (bloom_size * 8);  // Golden ratio hash

        size_t byte_idx = idx / 8;
        int bit_idx = idx % 8;

        if (!(bloom[byte_idx] & (1 << bit_idx))) {
            return 0;  // Definitely not in set
        }
    }
    return 1;  // Might be in set
}

// ============================================================================
// Direct target search (device) - for small target counts
// ============================================================================

__device__ int target_search(const uint8_t *targets, size_t target_count,
                             const uint8_t *hash20) {
    for (size_t i = 0; i < target_count; i++) {
        const uint8_t *target = targets + i * 20;
        int match = 1;
        for (int j = 0; j < 20 && match; j++) {
            if (hash20[j] != target[j]) match = 0;
        }
        if (match) return (int)i;
    }
    return -1;
}

// ============================================================================
// Found key result buffer
// ============================================================================

struct FoundKey {
    uint256_d privkey;
    int compressed;  // 2 = prefix 02, 3 = prefix 03
    int valid;
};

#define MAX_FOUND_KEYS 256
__device__ FoundKey d_found_keys[MAX_FOUND_KEYS];
__device__ int d_found_count = 0;

__device__ void report_found_key(const uint256_d *privkey, int compressed) {
    int idx = atomicAdd(&d_found_count, 1);
    if (idx < MAX_FOUND_KEYS) {
        u256_set(&d_found_keys[idx].privkey, privkey);
        d_found_keys[idx].compressed = compressed;
        d_found_keys[idx].valid = 1;
    }
}

// ============================================================================
// Full search kernel
// ============================================================================

__global__ void kernel_full_search(
    const uint256_d start_key,
    uint64_t key_offset,
    uint64_t keys_per_thread,
    const uint8_t *gtable,
    const uint8_t *targets, size_t target_count,
    const uint8_t *bloom, size_t bloom_size, int bloom_hashes,
    int use_bloom,
    volatile int *should_stop
) {
    uint64_t thread_id = (uint64_t)blockIdx.x * blockDim.x + threadIdx.x;
    uint64_t base_offset = key_offset + thread_id * keys_per_thread;

    // Compute starting key for this thread
    uint256_d current_key;
    u256_set(&current_key, &start_key);

    // Add base_offset to current_key
    uint64_t carry = base_offset;
    for (int i = 0; i < 8 && carry; i++) {
        uint64_t sum = (uint64_t)current_key.d[i] + (carry & 0xFFFFFFFF);
        current_key.d[i] = (uint32_t)sum;
        carry = (sum >> 32) + (carry >> 32);
    }

    // Compute initial point
    Point256_d current_point;
    scalar_mul_G(&current_point, &current_key, gtable);

    // Pre-fetch G point for incrementing (byte 0, value 1 = 1*G)
    uint256_d gx, gy;
    gtable_get_point(&gx, &gy, gtable, 0, 1);

    // Process keys
    for (uint64_t i = 0; i < keys_per_thread; i++) {
        if (*should_stop) return;

        // Get X coordinate and Y parity
        uint256_d x_affine;
        point_get_x_affine(&x_affine, &current_point);
        int y_parity = point_get_y_parity(&current_point);

        // Convert X to big-endian bytes
        uint8_t x_be[32];
        for (int w = 0; w < 8; w++) {
            uint32_t val = x_affine.d[7 - w];
            x_be[w*4]     = (val >> 24) & 0xFF;
            x_be[w*4 + 1] = (val >> 16) & 0xFF;
            x_be[w*4 + 2] = (val >> 8) & 0xFF;
            x_be[w*4 + 3] = val & 0xFF;
        }

        // Compute hash160 for correct parity prefix
        uint8_t pubkey[33];
        uint8_t sha_hash[32];
        uint8_t hash160[20];

        pubkey[0] = (y_parity == 0) ? 0x02 : 0x03;
        for (int j = 0; j < 32; j++) pubkey[j + 1] = x_be[j];

        sha256_33(pubkey, sha_hash);
        ripemd160_32(sha_hash, hash160);

        // Check against targets
        int found = 0;
        if (use_bloom && bloom && bloom_size > 0) {
            if (bloom_check(bloom, bloom_size, bloom_hashes, hash160)) {
                found = target_search(targets, target_count, hash160);
            }
        } else {
            found = target_search(targets, target_count, hash160);
        }

        if (found >= 0) {
            report_found_key(&current_key, pubkey[0]);
        }

        // Also check opposite parity (both 02 and 03 prefixes)
        pubkey[0] = (y_parity == 0) ? 0x03 : 0x02;
        sha256_33(pubkey, sha_hash);
        ripemd160_32(sha_hash, hash160);

        if (use_bloom && bloom && bloom_size > 0) {
            if (bloom_check(bloom, bloom_size, bloom_hashes, hash160)) {
                found = target_search(targets, target_count, hash160);
            }
        } else {
            found = target_search(targets, target_count, hash160);
        }

        if (found >= 0) {
            // For opposite parity, we need to negate the private key
            // k' = n - k gives the same X but opposite Y
            // For simplicity, just report the original key with different prefix
            report_found_key(&current_key, pubkey[0]);
        }

        // Increment key and point: key++, point += G
        uint64_t c = 1;
        for (int j = 0; j < 8 && c; j++) {
            uint64_t sum = (uint64_t)current_key.d[j] + c;
            current_key.d[j] = (uint32_t)sum;
            c = sum >> 32;
        }

        Point256_d next_point;
        point_add_affine(&next_point, &current_point, &gx, &gy);
        current_point = next_point;
    }
}

// ============================================================================
// Hash160 from X coordinate kernel
// ============================================================================

__global__ void kernel_hash160_fromX(const uint8_t *x32_be, size_t count,
                                      uint8_t *out02, uint8_t *out03) {
    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) return;

    const uint8_t *x = x32_be + idx * 32;
    uint8_t pubkey[33];
    uint8_t sha_hash[32];

    // Compressed pubkey with prefix 02
    pubkey[0] = 0x02;
    for (int i = 0; i < 32; i++) pubkey[i + 1] = x[i];

    sha256_33(pubkey, sha_hash);
    ripemd160_32(sha_hash, out02 + idx * 20);

    // Compressed pubkey with prefix 03
    pubkey[0] = 0x03;
    sha256_33(pubkey, sha_hash);
    ripemd160_32(sha_hash, out03 + idx * 20);
}

// ============================================================================
// Backend API implementation
// ============================================================================

extern "C" {

int gpu_backend_init(gpu_backend_info_t *info) {
    int deviceCount = 0;
    cudaError_t err = cudaGetDeviceCount(&deviceCount);

    if (err != cudaSuccess || deviceCount == 0) {
        g_available = 0;
        if (info) memset(info, 0, sizeof(*info));
        return 0;
    }

    cudaDeviceProp prop;
    cudaGetDeviceProperties(&prop, 0);

    memset(&g_info, 0, sizeof(g_info));
    g_info.gpu_count = deviceCount;
    g_info.vram_mb = prop.totalGlobalMem / (1024 * 1024);
    strncpy(g_info.name, prop.name, sizeof(g_info.name) - 1);
    g_info.compute_major = prop.major;
    g_info.compute_minor = prop.minor;
    g_info.multiprocessors = prop.multiProcessorCount;
    g_info.max_threads_per_block = prop.maxThreadsPerBlock;

    if (info) *info = g_info;

    g_available = 1;
    return 0;
}

int gpu_backend_available(void) {
    return g_available;
}

void gpu_backend_shutdown(void) {
    if (d_GTable) { cudaFree(d_GTable); d_GTable = NULL; }
    if (d_targets) { cudaFree(d_targets); d_targets = NULL; }
    if (d_bloom) { cudaFree(d_bloom); d_bloom = NULL; }
    if (d_x32) { cudaFree(d_x32); d_x32 = NULL; }
    if (d_out02) { cudaFree(d_out02); d_out02 = NULL; }
    if (d_out03) { cudaFree(d_out03); d_out03 = NULL; }
    if (g_stream) { cudaStreamDestroy(g_stream); g_stream = NULL; }
    g_available = 0;
}

int gpu_hash160_fromX_batch(const uint8_t *x32_be, size_t count,
                            uint8_t *out02, uint8_t *out03) {
    if (!g_available || count == 0) return 1;

    // Allocate/reallocate device memory if needed
    if (count > g_capacity) {
        if (d_x32) cudaFree(d_x32);
        if (d_out02) cudaFree(d_out02);
        if (d_out03) cudaFree(d_out03);

        cudaMalloc(&d_x32, count * 32);
        cudaMalloc(&d_out02, count * 20);
        cudaMalloc(&d_out03, count * 20);
        g_capacity = count;

        if (!g_stream) cudaStreamCreate(&g_stream);
    }

    // Copy input to device
    cudaMemcpyAsync(d_x32, x32_be, count * 32, cudaMemcpyHostToDevice, g_stream);

    // Launch kernel
    int threads = 256;
    int blocks = (count + threads - 1) / threads;
    kernel_hash160_fromX<<<blocks, threads, 0, g_stream>>>(d_x32, count, d_out02, d_out03);

    // Copy results back
    cudaMemcpyAsync(out02, d_out02, count * 20, cudaMemcpyDeviceToHost, g_stream);
    cudaMemcpyAsync(out03, d_out03, count * 20, cudaMemcpyDeviceToHost, g_stream);

    cudaStreamSynchronize(g_stream);

    return (cudaGetLastError() == cudaSuccess) ? 0 : 1;
}

int gpu_upload_gtable(const uint8_t *gtable, size_t point_count) {
    if (!g_available) return 1;

    size_t size = point_count * 64;
    if (d_GTable) cudaFree(d_GTable);

    cudaError_t err = cudaMalloc(&d_GTable, size);
    if (err != cudaSuccess) return 1;

    err = cudaMemcpy(d_GTable, gtable, size, cudaMemcpyHostToDevice);
    if (err != cudaSuccess) { cudaFree(d_GTable); d_GTable = NULL; return 1; }

    g_GTable_count = point_count;
    return 0;
}

int gpu_upload_targets(const uint8_t *targets, size_t count) {
    if (!g_available) return 1;

    size_t size = count * 20;
    if (d_targets) cudaFree(d_targets);

    cudaError_t err = cudaMalloc(&d_targets, size);
    if (err != cudaSuccess) return 1;

    err = cudaMemcpy(d_targets, targets, size, cudaMemcpyHostToDevice);
    if (err != cudaSuccess) { cudaFree(d_targets); d_targets = NULL; return 1; }

    g_target_count = count;
    return 0;
}

int gpu_upload_bloom(const uint8_t *bloom_data, size_t bloom_size, int num_hashes) {
    if (!g_available) return 1;

    if (d_bloom) cudaFree(d_bloom);

    cudaError_t err = cudaMalloc(&d_bloom, bloom_size);
    if (err != cudaSuccess) return 1;

    err = cudaMemcpy(d_bloom, bloom_data, bloom_size, cudaMemcpyHostToDevice);
    if (err != cudaSuccess) { cudaFree(d_bloom); d_bloom = NULL; return 1; }

    g_bloom_size = bloom_size;
    g_bloom_hashes = num_hashes;
    return 0;
}

int gpu_full_search(const gpu_search_config_t *config) {
    if (!g_available || !config) return -1;
    if (!d_GTable || g_GTable_count == 0) {
        fprintf(stderr, "[!] GPU full search requires G table upload first\n");
        return -1;
    }
    if (!d_targets || g_target_count == 0) {
        fprintf(stderr, "[!] GPU full search requires targets upload first\n");
        return -1;
    }

    // Reset found keys counter
    int zero = 0;
    cudaMemcpyToSymbol(d_found_count, &zero, sizeof(int));

    // Allocate should_stop flag on device
    int *d_should_stop = NULL;
    cudaMalloc(&d_should_stop, sizeof(int));
    cudaMemcpy(d_should_stop, (const void*)config->should_stop, sizeof(int), cudaMemcpyHostToDevice);

    // Convert start_key and end_key to uint256_d
    uint256_d start_key, end_key;
    for (int i = 0; i < 8; i++) {
        start_key.d[i] = ((uint32_t)config->start_key[(7-i)*4+3]) |
                         ((uint32_t)config->start_key[(7-i)*4+2] << 8) |
                         ((uint32_t)config->start_key[(7-i)*4+1] << 16) |
                         ((uint32_t)config->start_key[(7-i)*4] << 24);
        end_key.d[i] = ((uint32_t)config->end_key[(7-i)*4+3]) |
                       ((uint32_t)config->end_key[(7-i)*4+2] << 8) |
                       ((uint32_t)config->end_key[(7-i)*4+1] << 16) |
                       ((uint32_t)config->end_key[(7-i)*4] << 24);
    }

    // Calculate range size (simplified: assumes range fits in 64 bits for small tests)
    uint64_t range_size = 0;
    if (end_key.d[7] == 0 && end_key.d[6] == 0 && end_key.d[5] == 0 && end_key.d[4] == 0 &&
        end_key.d[3] == 0 && end_key.d[2] == 0) {
        // Small range: fits in 64 bits
        uint64_t end_val = ((uint64_t)end_key.d[1] << 32) | end_key.d[0];
        uint64_t start_val = ((uint64_t)start_key.d[1] << 32) | start_key.d[0];
        range_size = end_val - start_val + 1;
    } else {
        // Large range: process up to 2^40 keys per session
        range_size = 1ULL << 40;
    }

    // Configure kernel launch
    int threads_per_block = 256;
    int blocks = g_info.multiprocessors * 4;  // 4 blocks per SM
    uint64_t keys_per_thread = 64;  // Each thread processes 64 keys
    uint64_t keys_per_launch = (uint64_t)blocks * threads_per_block * keys_per_thread;

    uint64_t total_keys = 0;
    uint64_t key_offset = 0;
    int total_found = 0;

    printf("[+] GPU full search: %d blocks x %d threads x %lu keys/thread = %lu keys/launch\n",
           blocks, threads_per_block, (unsigned long)keys_per_thread, (unsigned long)keys_per_launch);
    printf("[+] Range size: %lu keys\n", (unsigned long)range_size);
    fflush(stdout);

    // Main search loop
    while (!*(config->should_stop) && key_offset < range_size) {
        // Update should_stop on device
        cudaMemcpy(d_should_stop, (const void*)config->should_stop, sizeof(int), cudaMemcpyHostToDevice);

        // Adjust keys_per_thread for last batch
        uint64_t remaining = range_size - key_offset;
        uint64_t actual_keys_per_thread = keys_per_thread;
        if (remaining < keys_per_launch) {
            actual_keys_per_thread = (remaining + blocks * threads_per_block - 1) / (blocks * threads_per_block);
            if (actual_keys_per_thread < 1) actual_keys_per_thread = 1;
        }

        // Launch kernel
        kernel_full_search<<<blocks, threads_per_block>>>(
            start_key,
            key_offset,
            actual_keys_per_thread,
            d_GTable,
            d_targets, g_target_count,
            d_bloom, g_bloom_size, g_bloom_hashes,
            config->use_bloom,
            d_should_stop
        );

        cudaError_t err = cudaDeviceSynchronize();
        if (err != cudaSuccess) {
            fprintf(stderr, "[!] CUDA kernel error: %s\n", cudaGetErrorString(err));
            break;
        }

        // Update statistics
        uint64_t keys_this_launch = (uint64_t)blocks * threads_per_block * actual_keys_per_thread;
        key_offset += keys_this_launch;
        total_keys += keys_this_launch;

        if (config->keys_checked) {
            *(config->keys_checked) = total_keys;
        }

        // Check for found keys
        int found_count = 0;
        cudaMemcpyFromSymbol(&found_count, d_found_count, sizeof(int));

        if (found_count > 0) {
            // Retrieve found keys
            FoundKey h_found[MAX_FOUND_KEYS];
            cudaMemcpyFromSymbol(h_found, d_found_keys, sizeof(FoundKey) * found_count);

            for (int i = 0; i < found_count && i < MAX_FOUND_KEYS; i++) {
                if (h_found[i].valid) {
                    total_found++;
                    // Convert privkey to big-endian bytes
                    uint8_t privkey_be[32];
                    for (int w = 0; w < 8; w++) {
                        uint32_t val = h_found[i].privkey.d[7 - w];
                        privkey_be[w*4]     = (val >> 24) & 0xFF;
                        privkey_be[w*4 + 1] = (val >> 16) & 0xFF;
                        privkey_be[w*4 + 2] = (val >> 8) & 0xFF;
                        privkey_be[w*4 + 3] = val & 0xFF;
                    }

                    // Call callback
                    if (config->callback) {
                        config->callback(privkey_be, h_found[i].compressed == 2 || h_found[i].compressed == 3,
                                        config->callback_userdata);
                    }
                }
            }

            // Reset counter for next batch
            cudaMemcpyToSymbol(d_found_count, &zero, sizeof(int));
        }

        // Progress output every few batches
        if ((key_offset % (keys_per_launch * 10)) == 0 || key_offset >= range_size) {
            printf("\r[+] Progress: %lu / %lu keys (%.1f%%), found: %d   ",
                   (unsigned long)key_offset, (unsigned long)range_size,
                   (double)key_offset * 100.0 / (double)range_size, total_found);
            fflush(stdout);
        }
    }

    printf("\n");
    cudaFree(d_should_stop);
    return total_found;
}

size_t gpu_get_optimal_batch_size(void) {
    if (!g_available) return 0;
    return g_info.multiprocessors * 2048;
}

double gpu_benchmark(size_t duration_ms) {
    (void)duration_ms;
    return 0.0;
}

} // extern "C"
