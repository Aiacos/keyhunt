/*
 * Optimized OpenCL SHA256 + RIPEMD160 for Bitcoin address generation
 * AMD GPU implementation
 *
 * Key optimizations:
 * 1. Fully unrolled loops - no branching, no index calculations
 * 2. Register-only computation - minimal memory access
 * 3. Fused hash160 - single function for SHA256+RIPEMD160
 * 4. Specialized for 33-byte compressed pubkey input
 * 5. Inline everything for maximum performance
 *
 * Performance target: Match or exceed CUDA implementation on AMD hardware
 * Architecture: Designed for AMD RDNA/GCN compute units
 */

// ============================================================================
// SHA256 - Fully unrolled, register-only
// ============================================================================

#define SHA256_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

#define SHA256_CH(e, f, g) (((e) & (f)) ^ (~(e) & (g)))
#define SHA256_MAJ(a, b, c) (((a) & (b)) ^ ((a) & (c)) ^ ((b) & (c)))
#define SHA256_EP0(a) (SHA256_ROTR(a, 2) ^ SHA256_ROTR(a, 13) ^ SHA256_ROTR(a, 22))
#define SHA256_EP1(e) (SHA256_ROTR(e, 6) ^ SHA256_ROTR(e, 11) ^ SHA256_ROTR(e, 25))
#define SHA256_SIG0(x) (SHA256_ROTR(x, 7) ^ SHA256_ROTR(x, 18) ^ ((x) >> 3))
#define SHA256_SIG1(x) (SHA256_ROTR(x, 17) ^ SHA256_ROTR(x, 19) ^ ((x) >> 10))

// SHA256 round macro
#define SHA256_ROUND(a, b, c, d, e, f, g, h, k, w) \
    do { \
        uint t1 = (h) + SHA256_EP1(e) + SHA256_CH(e, f, g) + (k) + (w); \
        uint t2 = SHA256_EP0(a) + SHA256_MAJ(a, b, c); \
        (h) = (g); (g) = (f); (f) = (e); (e) = (d) + t1; \
        (d) = (c); (c) = (b); (b) = (a); (a) = t1 + t2; \
    } while(0)

// Message schedule extension macro
#define SHA256_EXTEND(w, i) \
    (w)[(i)] = SHA256_SIG1((w)[(i)-2]) + (w)[(i)-7] + SHA256_SIG0((w)[(i)-15]) + (w)[(i)-16]

/*
 * SHA256 for exactly 33 bytes (compressed Bitcoin public key)
 * Input: 33 bytes (1 byte prefix + 32 bytes X coordinate)
 * Output: 32 bytes hash
 */
inline void sha256_33_optimized(__private const uchar *msg, __private uint *h) {
    // Initialize hash state
    h[0] = 0x6a09e667u; h[1] = 0xbb67ae85u;
    h[2] = 0x3c6ef372u; h[3] = 0xa54ff53au;
    h[4] = 0x510e527fu; h[5] = 0x9b05688cu;
    h[6] = 0x1f83d9abu; h[7] = 0x5be0cd19u;

    // Message schedule array (in registers)
    uint w[64];

    // Load 33 bytes into first 9 words (big-endian) with padding
    w[0] = ((uint)msg[0] << 24) | ((uint)msg[1] << 16) |
           ((uint)msg[2] << 8) | (uint)msg[3];
    w[1] = ((uint)msg[4] << 24) | ((uint)msg[5] << 16) |
           ((uint)msg[6] << 8) | (uint)msg[7];
    w[2] = ((uint)msg[8] << 24) | ((uint)msg[9] << 16) |
           ((uint)msg[10] << 8) | (uint)msg[11];
    w[3] = ((uint)msg[12] << 24) | ((uint)msg[13] << 16) |
           ((uint)msg[14] << 8) | (uint)msg[15];
    w[4] = ((uint)msg[16] << 24) | ((uint)msg[17] << 16) |
           ((uint)msg[18] << 8) | (uint)msg[19];
    w[5] = ((uint)msg[20] << 24) | ((uint)msg[21] << 16) |
           ((uint)msg[22] << 8) | (uint)msg[23];
    w[6] = ((uint)msg[24] << 24) | ((uint)msg[25] << 16) |
           ((uint)msg[26] << 8) | (uint)msg[27];
    w[7] = ((uint)msg[28] << 24) | ((uint)msg[29] << 16) |
           ((uint)msg[30] << 8) | (uint)msg[31];
    // Byte 32 (last byte) + padding bit
    w[8] = ((uint)msg[32] << 24) | 0x00800000u;
    w[9] = 0; w[10] = 0; w[11] = 0; w[12] = 0; w[13] = 0;
    w[14] = 0;
    w[15] = 33 * 8;  // Length in bits

    // Extend message schedule (fully unrolled)
    SHA256_EXTEND(w, 16); SHA256_EXTEND(w, 17); SHA256_EXTEND(w, 18); SHA256_EXTEND(w, 19);
    SHA256_EXTEND(w, 20); SHA256_EXTEND(w, 21); SHA256_EXTEND(w, 22); SHA256_EXTEND(w, 23);
    SHA256_EXTEND(w, 24); SHA256_EXTEND(w, 25); SHA256_EXTEND(w, 26); SHA256_EXTEND(w, 27);
    SHA256_EXTEND(w, 28); SHA256_EXTEND(w, 29); SHA256_EXTEND(w, 30); SHA256_EXTEND(w, 31);
    SHA256_EXTEND(w, 32); SHA256_EXTEND(w, 33); SHA256_EXTEND(w, 34); SHA256_EXTEND(w, 35);
    SHA256_EXTEND(w, 36); SHA256_EXTEND(w, 37); SHA256_EXTEND(w, 38); SHA256_EXTEND(w, 39);
    SHA256_EXTEND(w, 40); SHA256_EXTEND(w, 41); SHA256_EXTEND(w, 42); SHA256_EXTEND(w, 43);
    SHA256_EXTEND(w, 44); SHA256_EXTEND(w, 45); SHA256_EXTEND(w, 46); SHA256_EXTEND(w, 47);
    SHA256_EXTEND(w, 48); SHA256_EXTEND(w, 49); SHA256_EXTEND(w, 50); SHA256_EXTEND(w, 51);
    SHA256_EXTEND(w, 52); SHA256_EXTEND(w, 53); SHA256_EXTEND(w, 54); SHA256_EXTEND(w, 55);
    SHA256_EXTEND(w, 56); SHA256_EXTEND(w, 57); SHA256_EXTEND(w, 58); SHA256_EXTEND(w, 59);
    SHA256_EXTEND(w, 60); SHA256_EXTEND(w, 61); SHA256_EXTEND(w, 62); SHA256_EXTEND(w, 63);

    // Working variables
    uint a = h[0], b = h[1], c = h[2], d = h[3];
    uint e = h[4], f = h[5], g = h[6], hh = h[7];

    // 64 rounds (fully unrolled with inline constants)
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x428a2f98u, w[0]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x71374491u, w[1]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0xb5c0fbcfu, w[2]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0xe9b5dba5u, w[3]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x3956c25bu, w[4]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x59f111f1u, w[5]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x923f82a4u, w[6]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0xab1c5ed5u, w[7]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0xd807aa98u, w[8]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x12835b01u, w[9]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x243185beu, w[10]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x550c7dc3u, w[11]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x72be5d74u, w[12]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x80deb1feu, w[13]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x9bdc06a7u, w[14]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0xc19bf174u, w[15]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0xe49b69c1u, w[16]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0xefbe4786u, w[17]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x0fc19dc6u, w[18]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x240ca1ccu, w[19]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x2de92c6fu, w[20]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x4a7484aau, w[21]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x5cb0a9dcu, w[22]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x76f988dau, w[23]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x983e5152u, w[24]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0xa831c66du, w[25]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0xb00327c8u, w[26]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0xbf597fc7u, w[27]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0xc6e00bf3u, w[28]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0xd5a79147u, w[29]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x06ca6351u, w[30]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x14292967u, w[31]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x27b70a85u, w[32]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x2e1b2138u, w[33]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x4d2c6dfcu, w[34]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x53380d13u, w[35]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x650a7354u, w[36]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x766a0abbu, w[37]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x81c2c92eu, w[38]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x92722c85u, w[39]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0xa2bfe8a1u, w[40]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0xa81a664bu, w[41]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0xc24b8b70u, w[42]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0xc76c51a3u, w[43]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0xd192e819u, w[44]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0xd6990624u, w[45]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0xf40e3585u, w[46]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x106aa070u, w[47]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x19a4c116u, w[48]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x1e376c08u, w[49]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x2748774cu, w[50]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x34b0bcb5u, w[51]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x391c0cb3u, w[52]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x4ed8aa4au, w[53]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x5b9cca4fu, w[54]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x682e6ff3u, w[55]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x748f82eeu, w[56]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x78a5636fu, w[57]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x84c87814u, w[58]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x8cc70208u, w[59]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0x90befffau, w[60]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0xa4506cebu, w[61]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0xbef9a3f7u, w[62]);
    SHA256_ROUND(a,b,c,d,e,f,g,hh, 0xc67178f2u, w[63]);

    // Add to initial hash
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

// ============================================================================
// RIPEMD160 - Fully unrolled, register-only
// ============================================================================

#define RMD_ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

// Round functions
#define RMD_F1(x, y, z) ((x) ^ (y) ^ (z))
#define RMD_F2(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define RMD_F3(x, y, z) (((x) | ~(y)) ^ (z))
#define RMD_F4(x, y, z) (((x) & (z)) | ((y) & ~(z)))
#define RMD_F5(x, y, z) ((x) ^ ((y) | ~(z)))

// Round macros for left line
#define RMD_R1L(a,b,c,d,e,w,s) do { \
    (a) = RMD_ROTL((a) + RMD_F1(b,c,d) + (w), s) + (e); \
    (c) = RMD_ROTL(c, 10); \
} while(0)

#define RMD_R2L(a,b,c,d,e,w,s) do { \
    (a) = RMD_ROTL((a) + RMD_F2(b,c,d) + (w) + 0x5a827999u, s) + (e); \
    (c) = RMD_ROTL(c, 10); \
} while(0)

#define RMD_R3L(a,b,c,d,e,w,s) do { \
    (a) = RMD_ROTL((a) + RMD_F3(b,c,d) + (w) + 0x6ed9eba1u, s) + (e); \
    (c) = RMD_ROTL(c, 10); \
} while(0)

#define RMD_R4L(a,b,c,d,e,w,s) do { \
    (a) = RMD_ROTL((a) + RMD_F4(b,c,d) + (w) + 0x8f1bbcdcu, s) + (e); \
    (c) = RMD_ROTL(c, 10); \
} while(0)

#define RMD_R5L(a,b,c,d,e,w,s) do { \
    (a) = RMD_ROTL((a) + RMD_F5(b,c,d) + (w) + 0xa953fd4eu, s) + (e); \
    (c) = RMD_ROTL(c, 10); \
} while(0)

// Round macros for right line
#define RMD_R1R(a,b,c,d,e,w,s) do { \
    (a) = RMD_ROTL((a) + RMD_F5(b,c,d) + (w) + 0x50a28be6u, s) + (e); \
    (c) = RMD_ROTL(c, 10); \
} while(0)

#define RMD_R2R(a,b,c,d,e,w,s) do { \
    (a) = RMD_ROTL((a) + RMD_F4(b,c,d) + (w) + 0x5c4dd124u, s) + (e); \
    (c) = RMD_ROTL(c, 10); \
} while(0)

#define RMD_R3R(a,b,c,d,e,w,s) do { \
    (a) = RMD_ROTL((a) + RMD_F3(b,c,d) + (w) + 0x6d703ef3u, s) + (e); \
    (c) = RMD_ROTL(c, 10); \
} while(0)

#define RMD_R4R(a,b,c,d,e,w,s) do { \
    (a) = RMD_ROTL((a) + RMD_F2(b,c,d) + (w) + 0x7a6d76e9u, s) + (e); \
    (c) = RMD_ROTL(c, 10); \
} while(0)

#define RMD_R5R(a,b,c,d,e,w,s) do { \
    (a) = RMD_ROTL((a) + RMD_F1(b,c,d) + (w), s) + (e); \
    (c) = RMD_ROTL(c, 10); \
} while(0)

/*
 * RIPEMD160 for exactly 32 bytes (SHA256 output)
 * Input: 32 bytes (8 uint in big-endian from SHA256)
 * Output: 20 bytes hash (stored as 5 uint)
 */
inline void ripemd160_32_optimized(__private const uint *sha_hash, __private uint *rmd_hash) {
    // Convert SHA256 output (big-endian) to RIPEMD160 input (little-endian)
    uint w[16];

    // Byte-swap from big-endian to little-endian
    for (int i = 0; i < 8; i++) {
        uint v = sha_hash[i];
        w[i] = ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) |
               ((v >> 8) & 0xFF00) | ((v >> 24) & 0xFF);
    }

    // Padding for 32-byte message
    w[8] = 0x80;  // Padding byte
    w[9] = 0; w[10] = 0; w[11] = 0; w[12] = 0; w[13] = 0;
    w[14] = 32 * 8;  // Length in bits (low)
    w[15] = 0;       // Length in bits (high)

    // Initialize hash state
    uint al = 0x67452301u, bl = 0xefcdab89u, cl = 0x98badcfeu, dl = 0x10325476u, el = 0xc3d2e1f0u;
    uint ar = 0x67452301u, br = 0xefcdab89u, cr = 0x98badcfeu, dr = 0x10325476u, er = 0xc3d2e1f0u;

    // ==================== Round 1 ====================
    // Left line (F1, K=0)
    RMD_R1L(al,bl,cl,dl,el, w[0], 11);
    RMD_R1L(el,al,bl,cl,dl, w[1], 14);
    RMD_R1L(dl,el,al,bl,cl, w[2], 15);
    RMD_R1L(cl,dl,el,al,bl, w[3], 12);
    RMD_R1L(bl,cl,dl,el,al, w[4], 5);
    RMD_R1L(al,bl,cl,dl,el, w[5], 8);
    RMD_R1L(el,al,bl,cl,dl, w[6], 7);
    RMD_R1L(dl,el,al,bl,cl, w[7], 9);
    RMD_R1L(cl,dl,el,al,bl, w[8], 11);
    RMD_R1L(bl,cl,dl,el,al, w[9], 13);
    RMD_R1L(al,bl,cl,dl,el, w[10], 14);
    RMD_R1L(el,al,bl,cl,dl, w[11], 15);
    RMD_R1L(dl,el,al,bl,cl, w[12], 6);
    RMD_R1L(cl,dl,el,al,bl, w[13], 7);
    RMD_R1L(bl,cl,dl,el,al, w[14], 9);
    RMD_R1L(al,bl,cl,dl,el, w[15], 8);

    // Right line (F5, K=0x50a28be6)
    RMD_R1R(ar,br,cr,dr,er, w[5], 8);
    RMD_R1R(er,ar,br,cr,dr, w[14], 9);
    RMD_R1R(dr,er,ar,br,cr, w[7], 9);
    RMD_R1R(cr,dr,er,ar,br, w[0], 11);
    RMD_R1R(br,cr,dr,er,ar, w[9], 13);
    RMD_R1R(ar,br,cr,dr,er, w[2], 15);
    RMD_R1R(er,ar,br,cr,dr, w[11], 15);
    RMD_R1R(dr,er,ar,br,cr, w[4], 5);
    RMD_R1R(cr,dr,er,ar,br, w[13], 7);
    RMD_R1R(br,cr,dr,er,ar, w[6], 7);
    RMD_R1R(ar,br,cr,dr,er, w[15], 8);
    RMD_R1R(er,ar,br,cr,dr, w[8], 11);
    RMD_R1R(dr,er,ar,br,cr, w[1], 14);
    RMD_R1R(cr,dr,er,ar,br, w[10], 14);
    RMD_R1R(br,cr,dr,er,ar, w[3], 12);
    RMD_R1R(ar,br,cr,dr,er, w[12], 6);

    // ==================== Round 2 ====================
    // Left line (F2, K=0x5a827999)
    RMD_R2L(el,al,bl,cl,dl, w[7], 7);
    RMD_R2L(dl,el,al,bl,cl, w[4], 6);
    RMD_R2L(cl,dl,el,al,bl, w[13], 8);
    RMD_R2L(bl,cl,dl,el,al, w[1], 13);
    RMD_R2L(al,bl,cl,dl,el, w[10], 11);
    RMD_R2L(el,al,bl,cl,dl, w[6], 9);
    RMD_R2L(dl,el,al,bl,cl, w[15], 7);
    RMD_R2L(cl,dl,el,al,bl, w[3], 15);
    RMD_R2L(bl,cl,dl,el,al, w[12], 7);
    RMD_R2L(al,bl,cl,dl,el, w[0], 12);
    RMD_R2L(el,al,bl,cl,dl, w[9], 15);
    RMD_R2L(dl,el,al,bl,cl, w[5], 9);
    RMD_R2L(cl,dl,el,al,bl, w[2], 11);
    RMD_R2L(bl,cl,dl,el,al, w[14], 7);
    RMD_R2L(al,bl,cl,dl,el, w[11], 13);
    RMD_R2L(el,al,bl,cl,dl, w[8], 12);

    // Right line (F4, K=0x5c4dd124)
    RMD_R2R(er,ar,br,cr,dr, w[6], 9);
    RMD_R2R(dr,er,ar,br,cr, w[11], 13);
    RMD_R2R(cr,dr,er,ar,br, w[3], 15);
    RMD_R2R(br,cr,dr,er,ar, w[7], 7);
    RMD_R2R(ar,br,cr,dr,er, w[0], 12);
    RMD_R2R(er,ar,br,cr,dr, w[13], 8);
    RMD_R2R(dr,er,ar,br,cr, w[5], 9);
    RMD_R2R(cr,dr,er,ar,br, w[10], 11);
    RMD_R2R(br,cr,dr,er,ar, w[14], 7);
    RMD_R2R(ar,br,cr,dr,er, w[15], 7);
    RMD_R2R(er,ar,br,cr,dr, w[8], 12);
    RMD_R2R(dr,er,ar,br,cr, w[12], 7);
    RMD_R2R(cr,dr,er,ar,br, w[4], 6);
    RMD_R2R(br,cr,dr,er,ar, w[9], 15);
    RMD_R2R(ar,br,cr,dr,er, w[1], 13);
    RMD_R2R(er,ar,br,cr,dr, w[2], 11);

    // ==================== Round 3 ====================
    // Left line (F3, K=0x6ed9eba1)
    RMD_R3L(dl,el,al,bl,cl, w[3], 11);
    RMD_R3L(cl,dl,el,al,bl, w[10], 13);
    RMD_R3L(bl,cl,dl,el,al, w[14], 6);
    RMD_R3L(al,bl,cl,dl,el, w[4], 7);
    RMD_R3L(el,al,bl,cl,dl, w[9], 14);
    RMD_R3L(dl,el,al,bl,cl, w[15], 9);
    RMD_R3L(cl,dl,el,al,bl, w[8], 13);
    RMD_R3L(bl,cl,dl,el,al, w[1], 15);
    RMD_R3L(al,bl,cl,dl,el, w[2], 14);
    RMD_R3L(el,al,bl,cl,dl, w[7], 8);
    RMD_R3L(dl,el,al,bl,cl, w[0], 13);
    RMD_R3L(cl,dl,el,al,bl, w[6], 6);
    RMD_R3L(bl,cl,dl,el,al, w[13], 5);
    RMD_R3L(al,bl,cl,dl,el, w[11], 12);
    RMD_R3L(el,al,bl,cl,dl, w[5], 7);
    RMD_R3L(dl,el,al,bl,cl, w[12], 5);

    // Right line (F3, K=0x6d703ef3)
    RMD_R3R(dr,er,ar,br,cr, w[15], 9);
    RMD_R3R(cr,dr,er,ar,br, w[5], 7);
    RMD_R3R(br,cr,dr,er,ar, w[1], 15);
    RMD_R3R(ar,br,cr,dr,er, w[3], 11);
    RMD_R3R(er,ar,br,cr,dr, w[7], 8);
    RMD_R3R(dr,er,ar,br,cr, w[14], 6);
    RMD_R3R(cr,dr,er,ar,br, w[6], 6);
    RMD_R3R(br,cr,dr,er,ar, w[9], 14);
    RMD_R3R(ar,br,cr,dr,er, w[11], 12);
    RMD_R3R(er,ar,br,cr,dr, w[8], 13);
    RMD_R3R(dr,er,ar,br,cr, w[12], 5);
    RMD_R3R(cr,dr,er,ar,br, w[2], 14);
    RMD_R3R(br,cr,dr,er,ar, w[10], 13);
    RMD_R3R(ar,br,cr,dr,er, w[0], 13);
    RMD_R3R(er,ar,br,cr,dr, w[4], 7);
    RMD_R3R(dr,er,ar,br,cr, w[13], 5);

    // ==================== Round 4 ====================
    // Left line (F4, K=0x8f1bbcdc)
    RMD_R4L(cl,dl,el,al,bl, w[1], 11);
    RMD_R4L(bl,cl,dl,el,al, w[9], 12);
    RMD_R4L(al,bl,cl,dl,el, w[11], 14);
    RMD_R4L(el,al,bl,cl,dl, w[10], 15);
    RMD_R4L(dl,el,al,bl,cl, w[0], 14);
    RMD_R4L(cl,dl,el,al,bl, w[8], 15);
    RMD_R4L(bl,cl,dl,el,al, w[12], 9);
    RMD_R4L(al,bl,cl,dl,el, w[4], 8);
    RMD_R4L(el,al,bl,cl,dl, w[13], 9);
    RMD_R4L(dl,el,al,bl,cl, w[3], 14);
    RMD_R4L(cl,dl,el,al,bl, w[7], 5);
    RMD_R4L(bl,cl,dl,el,al, w[15], 6);
    RMD_R4L(al,bl,cl,dl,el, w[14], 8);
    RMD_R4L(el,al,bl,cl,dl, w[5], 6);
    RMD_R4L(dl,el,al,bl,cl, w[6], 5);
    RMD_R4L(cl,dl,el,al,bl, w[2], 12);

    // Right line (F2, K=0x7a6d76e9)
    RMD_R4R(cr,dr,er,ar,br, w[8], 15);
    RMD_R4R(br,cr,dr,er,ar, w[6], 5);
    RMD_R4R(ar,br,cr,dr,er, w[4], 8);
    RMD_R4R(er,ar,br,cr,dr, w[1], 11);
    RMD_R4R(dr,er,ar,br,cr, w[3], 14);
    RMD_R4R(cr,dr,er,ar,br, w[11], 14);
    RMD_R4R(br,cr,dr,er,ar, w[15], 6);
    RMD_R4R(ar,br,cr,dr,er, w[0], 14);
    RMD_R4R(er,ar,br,cr,dr, w[5], 6);
    RMD_R4R(dr,er,ar,br,cr, w[12], 9);
    RMD_R4R(cr,dr,er,ar,br, w[2], 12);
    RMD_R4R(br,cr,dr,er,ar, w[13], 9);
    RMD_R4R(ar,br,cr,dr,er, w[9], 12);
    RMD_R4R(er,ar,br,cr,dr, w[7], 5);
    RMD_R4R(dr,er,ar,br,cr, w[10], 15);
    RMD_R4R(cr,dr,er,ar,br, w[14], 8);

    // ==================== Round 5 ====================
    // Left line (F5, K=0xa953fd4e)
    RMD_R5L(bl,cl,dl,el,al, w[4], 9);
    RMD_R5L(al,bl,cl,dl,el, w[0], 15);
    RMD_R5L(el,al,bl,cl,dl, w[5], 5);
    RMD_R5L(dl,el,al,bl,cl, w[9], 11);
    RMD_R5L(cl,dl,el,al,bl, w[7], 6);
    RMD_R5L(bl,cl,dl,el,al, w[12], 8);
    RMD_R5L(al,bl,cl,dl,el, w[2], 13);
    RMD_R5L(el,al,bl,cl,dl, w[10], 12);
    RMD_R5L(dl,el,al,bl,cl, w[14], 5);
    RMD_R5L(cl,dl,el,al,bl, w[1], 12);
    RMD_R5L(bl,cl,dl,el,al, w[3], 13);
    RMD_R5L(al,bl,cl,dl,el, w[8], 14);
    RMD_R5L(el,al,bl,cl,dl, w[11], 11);
    RMD_R5L(dl,el,al,bl,cl, w[6], 8);
    RMD_R5L(cl,dl,el,al,bl, w[15], 5);
    RMD_R5L(bl,cl,dl,el,al, w[13], 6);

    // Right line (F1, K=0)
    RMD_R5R(br,cr,dr,er,ar, w[12], 8);
    RMD_R5R(ar,br,cr,dr,er, w[15], 5);
    RMD_R5R(er,ar,br,cr,dr, w[10], 12);
    RMD_R5R(dr,er,ar,br,cr, w[4], 9);
    RMD_R5R(cr,dr,er,ar,br, w[1], 12);
    RMD_R5R(br,cr,dr,er,ar, w[5], 5);
    RMD_R5R(ar,br,cr,dr,er, w[8], 14);
    RMD_R5R(er,ar,br,cr,dr, w[7], 6);
    RMD_R5R(dr,er,ar,br,cr, w[6], 8);
    RMD_R5R(cr,dr,er,ar,br, w[2], 13);
    RMD_R5R(br,cr,dr,er,ar, w[13], 6);
    RMD_R5R(ar,br,cr,dr,er, w[14], 5);
    RMD_R5R(er,ar,br,cr,dr, w[0], 15);
    RMD_R5R(dr,er,ar,br,cr, w[3], 13);
    RMD_R5R(cr,dr,er,ar,br, w[9], 11);
    RMD_R5R(br,cr,dr,er,ar, w[11], 11);

    // Final addition
    uint t = 0xefcdab89u + cl + dr;
    rmd_hash[1] = 0x98badcfeu + dl + er;
    rmd_hash[2] = 0x10325476u + el + ar;
    rmd_hash[3] = 0xc3d2e1f0u + al + br;
    rmd_hash[4] = 0x67452301u + bl + cr;
    rmd_hash[0] = t;
}

// ============================================================================
// Fused HASH160 = RIPEMD160(SHA256(pubkey))
// ============================================================================

/*
 * Compute HASH160 for a 33-byte compressed public key
 * This is the main function for Bitcoin address generation
 *
 * Input: 33-byte compressed public key (prefix + X coordinate)
 * Output: 20-byte HASH160 (as 5 uint in little-endian)
 */
inline void hash160_33_optimized(__private const uchar *pubkey, __private uchar *hash160) {
    uint sha_hash[8];
    uint rmd_hash[5];

    // SHA256(pubkey)
    sha256_33_optimized(pubkey, sha_hash);

    // RIPEMD160(sha256_result)
    ripemd160_32_optimized(sha_hash, rmd_hash);

    // Output as bytes (little-endian)
    for (int i = 0; i < 5; i++) {
        hash160[i*4]     = rmd_hash[i] & 0xFF;
        hash160[i*4 + 1] = (rmd_hash[i] >> 8) & 0xFF;
        hash160[i*4 + 2] = (rmd_hash[i] >> 16) & 0xFF;
        hash160[i*4 + 3] = (rmd_hash[i] >> 24) & 0xFF;
    }
}

/*
 * Compute HASH160 and return only the first 4 bytes (for bloom filter)
 * This allows early exit optimization
 */
inline uint hash160_33_prefix(__private const uchar *pubkey) {
    uint sha_hash[8];
    uint rmd_hash[5];

    sha256_33_optimized(pubkey, sha_hash);
    ripemd160_32_optimized(sha_hash, rmd_hash);

    return rmd_hash[0];  // First 4 bytes for quick bloom check
}

/*
 * Compute full HASH160 with separate prefix return for bloom filtering
 */
inline void hash160_33_with_prefix(
    __private const uchar *pubkey,
    __private uchar *hash160,
    __private uint *prefix
) {
    uint sha_hash[8];
    uint rmd_hash[5];

    sha256_33_optimized(pubkey, sha_hash);
    ripemd160_32_optimized(sha_hash, rmd_hash);

    *prefix = rmd_hash[0];

    for (int i = 0; i < 5; i++) {
        hash160[i*4]     = rmd_hash[i] & 0xFF;
        hash160[i*4 + 1] = (rmd_hash[i] >> 8) & 0xFF;
        hash160[i*4 + 2] = (rmd_hash[i] >> 16) & 0xFF;
        hash160[i*4 + 3] = (rmd_hash[i] >> 24) & 0xFF;
    }
}

// ============================================================================
// Example Kernel Entry Points
// ============================================================================

/*
 * Example kernel: Compute HASH160 for a batch of public keys
 * This demonstrates how to use the optimized hash functions
 */
__kernel void compute_hash160_batch(
    __global const uchar *pubkeys,     // Input: array of 33-byte pubkeys
    __global uchar *hash160s,          // Output: array of 20-byte HASH160s
    uint count                         // Number of public keys
) {
    uint idx = get_global_id(0);
    if (idx >= count) return;

    __private uchar pubkey[33];
    __private uchar hash160[20];

    // Load public key to private memory
    for (int i = 0; i < 33; i++) {
        pubkey[i] = pubkeys[idx * 33 + i];
    }

    // Compute HASH160
    hash160_33_optimized(pubkey, hash160);

    // Store result
    for (int i = 0; i < 20; i++) {
        hash160s[idx * 20 + i] = hash160[i];
    }
}

/*
 * Example kernel: Compute HASH160 prefix for bloom filter check
 */
__kernel void compute_hash160_prefix_batch(
    __global const uchar *pubkeys,     // Input: array of 33-byte pubkeys
    __global uint *prefixes,           // Output: array of 4-byte prefixes
    uint count                         // Number of public keys
) {
    uint idx = get_global_id(0);
    if (idx >= count) return;

    __private uchar pubkey[33];

    // Load public key to private memory
    for (int i = 0; i < 33; i++) {
        pubkey[i] = pubkeys[idx * 33 + i];
    }

    // Compute and store prefix
    prefixes[idx] = hash160_33_prefix(pubkey);
}

/*
 * Hash-only mode kernel: Compute HASH160 from X-coordinates
 * Given 32-byte X coordinates, compute HASH160 for both compressed formats (02 and 03)
 *
 * Input: x32_be - array of 32-byte X coordinates (big-endian)
 * Output: out02 - array of 20-byte HASH160s for prefix 0x02
 * Output: out03 - array of 20-byte HASH160s for prefix 0x03
 */
__kernel void kernel_hash160_fromX(
    __global const uchar *x32_be,      // Input: X coordinates (32 bytes each, big-endian)
    ulong count,                       // Number of X coordinates
    __global uchar *out02,             // Output: HASH160 for 02-prefix pubkeys
    __global uchar *out03              // Output: HASH160 for 03-prefix pubkeys
) {
    ulong idx = get_global_id(0);
    if (idx >= count) return;

    __private uchar pubkey[33];
    __private uchar hash160[20];

    // Load X coordinate
    __global const uchar *x = x32_be + idx * 32;

    // Compute HASH160 for compressed pubkey with prefix 0x02
    pubkey[0] = 0x02;
    for (int i = 0; i < 32; i++) {
        pubkey[i + 1] = x[i];
    }
    hash160_33_optimized(pubkey, hash160);

    // Store result for 0x02 prefix
    __global uchar *out02_ptr = out02 + idx * 20;
    for (int i = 0; i < 20; i++) {
        out02_ptr[i] = hash160[i];
    }

    // Compute HASH160 for compressed pubkey with prefix 0x03
    pubkey[0] = 0x03;
    hash160_33_optimized(pubkey, hash160);

    // Store result for 0x03 prefix
    __global uchar *out03_ptr = out03 + idx * 20;
    for (int i = 0; i < 20; i++) {
        out03_ptr[i] = hash160[i];
    }
}
