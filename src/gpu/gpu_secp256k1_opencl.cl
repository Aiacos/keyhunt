/*
 * GPU secp256k1 implementation for OpenCL
 * 256-bit integer arithmetic and elliptic curve operations
 */

// 256-bit integer represented as 8 x 32-bit words (little-endian)
typedef struct {
    uint d[8];
} uint256_t;

// secp256k1 curve point (Jacobian coordinates: X, Y, Z where x=X/Z^2, y=Y/Z^3)
typedef struct {
    uint256_t x;
    uint256_t y;
    uint256_t z;
} Point256;

// secp256k1 prime: p = 2^256 - 2^32 - 977
__constant uint256_t SECP256K1_P = {{
    0xFFFFFC2Fu, 0xFFFFFFFEu, 0xFFFFFFFFu, 0xFFFFFFFFu,
    0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu
}};

// secp256k1 order: n
__constant uint256_t SECP256K1_N = {{
    0xD0364141u, 0xBFD25E8Cu, 0xAF48A03Bu, 0xBAAEDCE6u,
    0xFFFFFFFEu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu
}};

// Generator point G (affine)
__constant uint256_t SECP256K1_GX = {{
    0x16F81798u, 0x59F2815Bu, 0x2DCE28D9u, 0x029BFCDBu,
    0xCE870B07u, 0x55A06295u, 0xF9DCBBACu, 0x79BE667Eu
}};

__constant uint256_t SECP256K1_GY = {{
    0xFB10D4B8u, 0x9C47D08Fu, 0xA6855419u, 0xFD17B448u,
    0x0E1108A8u, 0x5DA4FBFCu, 0x26A3C465u, 0x483ADA77u
}};

// ============================================================================
// 256-bit integer arithmetic
// ============================================================================

inline void u256_set_zero(uint256_t *r) {
    #pragma unroll
    for (int i = 0; i < 8; i++) r->d[i] = 0;
}

inline void u256_set(uint256_t *r, const uint256_t *a) {
    #pragma unroll
    for (int i = 0; i < 8; i++) r->d[i] = a->d[i];
}

inline void u256_set_u32(uint256_t *r, uint v) {
    r->d[0] = v;
    #pragma unroll
    for (int i = 1; i < 8; i++) r->d[i] = 0;
}

inline int u256_is_zero(const uint256_t *a) {
    uint z = 0;
    #pragma unroll
    for (int i = 0; i < 8; i++) z |= a->d[i];
    return z == 0;
}

inline int u256_cmp(const uint256_t *a, const uint256_t *b) {
    for (int i = 7; i >= 0; i--) {
        if (a->d[i] > b->d[i]) return 1;
        if (a->d[i] < b->d[i]) return -1;
    }
    return 0;
}

// r = a + b, returns carry
inline uint u256_add(uint256_t *r, const uint256_t *a, const uint256_t *b) {
    ulong c = 0;
    #pragma unroll
    for (int i = 0; i < 8; i++) {
        c += (ulong)a->d[i] + (ulong)b->d[i];
        r->d[i] = (uint)c;
        c >>= 32;
    }
    return (uint)c;
}

// r = a - b, returns borrow
inline uint u256_sub(uint256_t *r, const uint256_t *a, const uint256_t *b) {
    long c = 0;
    #pragma unroll
    for (int i = 0; i < 8; i++) {
        c += (long)a->d[i] - (long)b->d[i];
        r->d[i] = (uint)c;
        c >>= 32;
    }
    return (uint)(c < 0 ? 1 : 0);
}

// ============================================================================
// Modular arithmetic (mod p for secp256k1)
// ============================================================================

// Reduce mod p using fast reduction for secp256k1
// p = 2^256 - 2^32 - 977 = 2^256 - 0x1000003D1
inline void mod_reduce(uint256_t *r) {
    // If r >= p, subtract p
    if (u256_cmp(r, &SECP256K1_P) >= 0) {
        u256_sub(r, r, &SECP256K1_P);
    }
}

// r = (a + b) mod p
inline void mod_add(uint256_t *r, const uint256_t *a, const uint256_t *b) {
    uint carry = u256_add(r, a, b);
    if (carry || u256_cmp(r, &SECP256K1_P) >= 0) {
        u256_sub(r, r, &SECP256K1_P);
    }
}

// r = (a - b) mod p
inline void mod_sub(uint256_t *r, const uint256_t *a, const uint256_t *b) {
    uint borrow = u256_sub(r, a, b);
    if (borrow) {
        u256_add(r, r, &SECP256K1_P);
    }
}

// r = -a mod p
inline void mod_neg(uint256_t *r, const uint256_t *a) {
    if (u256_is_zero(a)) {
        u256_set_zero(r);
    } else {
        u256_sub(r, &SECP256K1_P, a);
    }
}

// 512-bit result type for multiplication
typedef struct {
    uint d[16];
} uint512_t;

// 256x256 -> 512 bit multiplication
inline void u256_mul_512(uint512_t *r, const uint256_t *a, const uint256_t *b) {
    #pragma unroll
    for (int i = 0; i < 16; i++) r->d[i] = 0;

    #pragma unroll
    for (int i = 0; i < 8; i++) {
        ulong carry = 0;
        #pragma unroll
        for (int j = 0; j < 8; j++) {
            ulong prod = (ulong)a->d[i] * (ulong)b->d[j] + (ulong)r->d[i+j] + carry;
            r->d[i+j] = (uint)prod;
            carry = prod >> 32;
        }
        r->d[i+8] = (uint)carry;
    }
}

// Fast reduction for secp256k1: r = a mod p where a is 512-bit
// p = 2^256 - c where c = 0x1000003D1
inline void mod_reduce_512(uint256_t *r, const uint512_t *a) {
    // a = a_lo + a_hi * 2^256
    // a mod p = a_lo + a_hi * c (mod p)  where c = 2^32 + 977

    uint256_t low, high;
    #pragma unroll
    for (int i = 0; i < 8; i++) {
        low.d[i] = a->d[i];
        high.d[i] = a->d[i+8];
    }

    // c = 0x1000003D1 = 2^32 + 977
    // high * c = high * 2^32 + high * 977
    // high * 2^32 means shift left by 1 word

    ulong carry = 0;

    // First: add high * 977 to low
    #pragma unroll
    for (int i = 0; i < 8; i++) {
        ulong prod = (ulong)high.d[i] * 977UL + (ulong)low.d[i] + carry;
        low.d[i] = (uint)prod;
        carry = prod >> 32;
    }

    // Now add high * 2^32 (shifted by 1 word)
    ulong carry2 = 0;
    carry2 += (ulong)low.d[1] + (ulong)high.d[0];
    low.d[1] = (uint)carry2;
    carry2 >>= 32;

    #pragma unroll
    for (int i = 2; i < 8; i++) {
        carry2 += (ulong)low.d[i] + (ulong)high.d[i-1];
        low.d[i] = (uint)carry2;
        carry2 >>= 32;
    }

    // Handle overflow from both carries
    ulong overflow = carry + carry2 + (ulong)high.d[7];

    // Reduce overflow * c again
    while (overflow > 0) {
        ulong c = 0;
        #pragma unroll
        for (int i = 0; i < 8 && (overflow > 0 || c > 0); i++) {
            ulong prod = overflow * 977UL * (i == 0 ? 1 : 0) + (ulong)low.d[i] + c;
            if (i == 1) prod += overflow; // *2^32 part
            low.d[i] = (uint)prod;
            c = prod >> 32;
        }
        overflow = c;
    }

    // Final reduction if needed
    while (u256_cmp(&low, &SECP256K1_P) >= 0) {
        u256_sub(&low, &low, &SECP256K1_P);
    }

    u256_set(r, &low);
}

// r = (a * b) mod p
inline void mod_mul(uint256_t *r, const uint256_t *a, const uint256_t *b) {
    uint512_t prod;
    u256_mul_512(&prod, a, b);
    mod_reduce_512(r, &prod);
}

// r = a^2 mod p
inline void mod_sqr(uint256_t *r, const uint256_t *a) {
    mod_mul(r, a, a);
}

// r = a^(-1) mod p using Fermat's little theorem: a^(-1) = a^(p-2) mod p
// This is slow but simple. For production, use extended Euclidean algorithm.
void mod_inv(uint256_t *r, const uint256_t *a) {
    // p - 2 for secp256k1
    // p = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F
    // p - 2 = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2D

    uint256_t base, result;
    u256_set(&base, a);
    u256_set_u32(&result, 1);

    // Binary exponentiation
    // We'll use a precomputed addition chain for better performance
    // For now, use simple binary method

    uint256_t exp = {{
        0xFFFFFC2Du, 0xFFFFFFFEu, 0xFFFFFFFFu, 0xFFFFFFFFu,
        0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu
    }};

    for (int i = 0; i < 256; i++) {
        int word = i / 32;
        int bit = i % 32;
        if ((exp.d[word] >> bit) & 1) {
            mod_mul(&result, &result, &base);
        }
        mod_sqr(&base, &base);
    }

    u256_set(r, &result);
}

// ============================================================================
// Elliptic curve operations (Jacobian coordinates)
// ============================================================================

inline void point_set_infinity(Point256 *p) {
    u256_set_zero(&p->x);
    u256_set_zero(&p->y);
    u256_set_zero(&p->z);
}

inline int point_is_infinity(const Point256 *p) {
    return u256_is_zero(&p->z);
}

// Convert affine (x,y) to Jacobian (X,Y,Z=1)
inline void point_from_affine(Point256 *p, const uint256_t *x, const uint256_t *y) {
    u256_set(&p->x, x);
    u256_set(&p->y, y);
    u256_set_u32(&p->z, 1);
}

// Convert Jacobian to affine: x = X/Z^2, y = Y/Z^3
void point_to_affine(uint256_t *x, uint256_t *y, const Point256 *p) {
    if (point_is_infinity(p)) {
        u256_set_zero(x);
        u256_set_zero(y);
        return;
    }

    uint256_t z_inv, z_inv2, z_inv3;
    mod_inv(&z_inv, &p->z);
    mod_sqr(&z_inv2, &z_inv);
    mod_mul(&z_inv3, &z_inv2, &z_inv);

    mod_mul(x, &p->x, &z_inv2);
    mod_mul(y, &p->y, &z_inv3);
}

// Get only X coordinate in affine
void point_get_x_affine(uint256_t *x, const Point256 *p) {
    if (point_is_infinity(p)) {
        u256_set_zero(x);
        return;
    }

    uint256_t z_inv, z_inv2;
    mod_inv(&z_inv, &p->z);
    mod_sqr(&z_inv2, &z_inv);
    mod_mul(x, &p->x, &z_inv2);
}

// Point doubling: R = 2*P (Jacobian)
void point_double(Point256 *r, const Point256 *p) {
    if (point_is_infinity(p) || u256_is_zero(&p->y)) {
        point_set_infinity(r);
        return;
    }

    // http://hyperelliptic.org/EFD/g1p/auto-shortw-jacobian-0.html#doubling-dbl-2009-l
    uint256_t a, b, c, d, e, f;

    mod_sqr(&a, &p->x);          // a = X^2
    mod_sqr(&b, &p->y);          // b = Y^2
    mod_sqr(&c, &b);             // c = b^2 = Y^4

    uint256_t tmp;
    mod_add(&tmp, &p->x, &b);
    mod_sqr(&d, &tmp);
    mod_sub(&d, &d, &a);
    mod_sub(&d, &d, &c);
    mod_add(&d, &d, &d);         // d = 2*((X+b)^2 - a - c)

    mod_add(&e, &a, &a);
    mod_add(&e, &e, &a);         // e = 3*a = 3*X^2

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

// Point addition: R = P + Q (Jacobian, mixed affine for Q when Qz=1)
void point_add(Point256 *r, const Point256 *p, const Point256 *q) {
    if (point_is_infinity(p)) {
        *r = *q;
        return;
    }
    if (point_is_infinity(q)) {
        *r = *p;
        return;
    }

    // http://hyperelliptic.org/EFD/g1p/auto-shortw-jacobian-0.html#addition-add-2007-bl
    uint256_t z1z1, z2z2, u1, u2, s1, s2, h, i, j, rr, v;

    mod_sqr(&z1z1, &p->z);       // Z1Z1 = Z1^2
    mod_sqr(&z2z2, &q->z);       // Z2Z2 = Z2^2

    mod_mul(&u1, &p->x, &z2z2);  // U1 = X1*Z2Z2
    mod_mul(&u2, &q->x, &z1z1);  // U2 = X2*Z1Z1

    uint256_t tmp;
    mod_mul(&tmp, &q->z, &z2z2);
    mod_mul(&s1, &p->y, &tmp);   // S1 = Y1*Z2*Z2Z2

    mod_mul(&tmp, &p->z, &z1z1);
    mod_mul(&s2, &q->y, &tmp);   // S2 = Y2*Z1*Z1Z1

    mod_sub(&h, &u2, &u1);       // H = U2 - U1

    // Check for doubling case
    if (u256_is_zero(&h)) {
        mod_sub(&tmp, &s2, &s1);
        if (u256_is_zero(&tmp)) {
            point_double(r, p);
            return;
        } else {
            point_set_infinity(r);
            return;
        }
    }

    mod_add(&i, &h, &h);
    mod_sqr(&i, &i);             // I = (2*H)^2

    mod_mul(&j, &h, &i);         // J = H*I

    mod_sub(&rr, &s2, &s1);
    mod_add(&rr, &rr, &rr);      // r = 2*(S2-S1)

    mod_mul(&v, &u1, &i);        // V = U1*I

    mod_sqr(&r->x, &rr);
    mod_sub(&r->x, &r->x, &j);
    mod_sub(&r->x, &r->x, &v);
    mod_sub(&r->x, &r->x, &v);   // X3 = r^2 - J - 2*V

    mod_sub(&tmp, &v, &r->x);
    mod_mul(&r->y, &rr, &tmp);
    mod_mul(&tmp, &s1, &j);
    mod_add(&tmp, &tmp, &tmp);
    mod_sub(&r->y, &r->y, &tmp); // Y3 = r*(V-X3) - 2*S1*J

    mod_mul(&r->z, &p->z, &q->z);
    mod_mul(&r->z, &r->z, &h);
    mod_add(&r->z, &r->z, &r->z); // Z3 = 2*Z1*Z2*H
}

// Point addition with Q in affine (Z=1): more efficient
void point_add_affine(Point256 *r, const Point256 *p, const uint256_t *qx, const uint256_t *qy) {
    if (point_is_infinity(p)) {
        point_from_affine(r, qx, qy);
        return;
    }

    uint256_t z1z1, u2, s2, h, hh, i, j, rr, v;

    mod_sqr(&z1z1, &p->z);        // Z1Z1 = Z1^2
    mod_mul(&u2, qx, &z1z1);      // U2 = X2*Z1Z1 (U1 = X1 since Q.z=1)

    uint256_t tmp;
    mod_mul(&tmp, &p->z, &z1z1);
    mod_mul(&s2, qy, &tmp);       // S2 = Y2*Z1*Z1Z1

    mod_sub(&h, &u2, &p->x);      // H = U2 - U1 = U2 - X1

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
    mod_add(&rr, &rr, &rr);       // r = 2*(S2-S1) = 2*(S2-Y1)

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

// ============================================================================
// Scalar multiplication using precomputed table
// ============================================================================

// Precomputed table: G, 2G, 3G, ..., 255G for each byte position
// Total: 256 * 32 = 8192 points
#define GTABLE_SIZE (256 * 32)

// Scalar multiply: R = k * G using precomputed table
// Note: GTable must be passed as kernel parameter in OpenCL
void point_mul_G(Point256 *r, const uint256_t *k, __global const Point256 *GTable) {
    point_set_infinity(r);

    // Process each byte of k
    for (int i = 0; i < 32; i++) {
        int word = i / 4;
        int byte_in_word = i % 4;
        uchar b = (uchar)(k->d[word] >> (byte_in_word * 8));

        if (b > 0) {
            // Add GTable[i * 256 + (b-1)]
            int idx = i * 256 + (b - 1);
            if (point_is_infinity(r)) {
                *r = GTable[idx];
            } else {
                Point256 tmp;
                point_add(&tmp, r, &GTable[idx]);
                *r = tmp;
            }
        }
    }
}

// ============================================================================
// Key increment: efficient next key computation
// ============================================================================

// Add delta to point P: R = P + delta*G
void point_add_delta(Point256 *r, const Point256 *p, uint delta, __global const Point256 *GTable) {
    if (delta == 0) {
        *r = *p;
        return;
    }

    // For small delta, add G repeatedly or use small table
    // For delta = 1, just add G
    Point256 dG = GTable[delta - 1]; // delta*G from first 256 entries
    if (point_is_infinity(p)) {
        *r = dG;
    } else {
        point_add(r, p, &dG);
    }
}

// ============================================================================
// Example kernel entry points
// ============================================================================

// Example: Compute public key from private key
__kernel void compute_pubkey(
    __global const uint256_t *private_keys,  // Input: array of private keys
    __global uint256_t *pubkey_x,            // Output: public key X coordinates
    __global uint256_t *pubkey_y,            // Output: public key Y coordinates
    __global const Point256 *GTable,         // Precomputed G table
    uint num_keys                            // Number of keys to process
) {
    uint idx = get_global_id(0);
    if (idx >= num_keys) return;

    // Compute pubkey = k * G
    Point256 pubkey;
    point_mul_G(&pubkey, &private_keys[idx], GTable);

    // Convert to affine and store
    point_to_affine(&pubkey_x[idx], &pubkey_y[idx], &pubkey);
}

// Example: Batch point addition (P + delta*G for each P)
__kernel void batch_point_increment(
    __global const Point256 *points_in,      // Input: array of points in Jacobian
    __global Point256 *points_out,           // Output: incremented points
    __global const uint *deltas,             // Delta values for each point
    __global const Point256 *GTable,         // Precomputed G table
    uint num_points                          // Number of points to process
) {
    uint idx = get_global_id(0);
    if (idx >= num_points) return;

    point_add_delta(&points_out[idx], &points_in[idx], deltas[idx], GTable);
}

// Example: Extract X coordinates only (for hash computation)
__kernel void extract_x_coordinates(
    __global const Point256 *points,         // Input: points in Jacobian
    __global uint256_t *x_coords,            // Output: X coordinates in affine
    uint num_points                          // Number of points to process
) {
    uint idx = get_global_id(0);
    if (idx >= num_points) return;

    point_get_x_affine(&x_coords[idx], &points[idx]);
}
