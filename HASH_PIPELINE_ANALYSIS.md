# Hash Pipeline Analysis: SHA256-AVX2 → RIPEMD160-AVX2

## Subtask 1.1: Register State Format Study

This document analyzes the AVX2 implementations of SHA256 and RIPEMD160 to understand the register state formats required for a fused hash pipeline.

---

## SHA256 AVX2 State Format

### Register Layout
- **State representation**: 8 × `__m256i` registers (256-bit AVX2 registers)
- **State array**: `__m256i s[8]` (aligned to 32-byte boundary)
- **Parallelism**: 8-way parallel processing (8 independent SHA256 hashes)
- **Each register**: Holds 8 × 32-bit values (total 256 bits)

### State Register Mapping
```
s[0] = a (first word of SHA256 state)
s[1] = b (second word)
s[2] = c (third word)
s[3] = d (fourth word)
s[4] = e (fifth word)
s[5] = f (sixth word)
s[6] = g (seventh word)
s[7] = h (eighth word)
```

### Data Organization
- **Transposed format**: Each register contains the same word position from 8 different hashes
- **Example**: `s[0]` contains word 0 from hash 0-7
- **Byte order**: Uses `_mm256_set_epi32()` which loads in reverse order (MSB first)
- **Output size**: 32 bytes per hash (8 words × 4 bytes)

### Initialization
- State initialized from `_init[]` array containing 8 copies of SHA256 initial constants:
  - `0x6a09e667` (repeated 8 times for s[0])
  - `0xbb67ae85` (repeated 8 times for s[1])
  - ... (continuing for all 8 state words)

### Post-Transform State
After `Transform()` completes:
- State contains final SHA256 hash values
- Still in transposed format (needs unpacking)
- Each register holds one word position from 8 parallel hashes

---

## RIPEMD160 AVX2 Input Requirements

### State Format
- **State representation**: 5 × `__m256i` registers
- **State array**: `__m256i s[5]` (aligned to 32-byte boundary)
- **Parallelism**: 8-way parallel processing
- **Each register**: Holds 8 × 32-bit values

### Input Format
- **Input method**: Array of 8 pointers `const uint8_t *blk[8]`
- **Input size**: 32 bytes per message block (8 words)
- **Processing**: `transpose_and_load()` loads and transposes input data

### Message Schedule (16 words required)
For 32-byte input, RIPEMD160 expects 16 words:
```
w[0-7]   = Input data (8 words from message)
w[8]     = 0x00000080 (padding byte 0x80 in little-endian)
w[9-13]  = 0x00000000 (zero padding)
w[14]    = 256 (0x100) (bit length = 32 bytes × 8 = 256 bits)
w[15]    = 0x00000000 (high word of bit length)
```

### Data Loading Pattern
```c
// Loads word i from each of 8 message blocks
#define LOADW(i) _mm256_set_epi32( \
    *((const uint32_t *)blk[0]+i), \
    *((const uint32_t *)blk[1]+i), \
    *((const uint32_t *)blk[2]+i), \
    *((const uint32_t *)blk[3]+i), \
    *((const uint32_t *)blk[4]+i), \
    *((const uint32_t *)blk[5]+i), \
    *((const uint32_t *)blk[6]+i), \
    *((const uint32_t *)blk[7]+i))
```

---

## Format Compatibility Analysis

### ✅ Compatible Aspects
1. **Word size**: Both use 32-bit words
2. **Endianness**: Both use little-endian
3. **Output size**: SHA256 produces 32 bytes (8 words)
4. **Input size**: RIPEMD160 accepts 32 bytes (8 words)
5. **Parallelism**: Both process 8-way parallel
6. **Register count**: SHA256 has 8 registers for 8 words of output

### ⚠️ Incompatible Aspects (Requires Conversion)
1. **Data layout**:
   - SHA256 output: Transposed (each register = same word from 8 hashes)
   - RIPEMD160 input: Non-transposed (8 pointers to sequential message blocks)

2. **Output format**:
   - SHA256: In-register state (`__m256i s[8]`)
   - RIPEMD160: Expects memory pointers (`const uint8_t *blk[8]`)

3. **Byte order conversion**:
   - SHA256 output uses `__builtin_bswap32()` for big-endian output
   - RIPEMD160 expects little-endian input (native format)

---

## Fused Pipeline Requirements

### Key Challenge: Transpose Operation
**Problem**: SHA256 state is transposed, but RIPEMD160 expects non-transposed input.

**Current approach (separate functions)**:
```
SHA256 Transform → Transpose & Store → Memory → Load & Transpose → RIPEMD160 Transform
                   (expensive)              (cache miss)     (expensive)
```

**Fused approach goal**:
```
SHA256 Transform → Direct Register Transfer → RIPEMD160 Transform
                   (in-register transpose or format adaptation)
```

### Option 1: In-Register Transpose
- Use AVX2 shuffle/permute instructions to transpose 8×8 matrix
- Instructions: `_mm256_unpacklo_epi32()`, `_mm256_unpackhi_epi32()`, `_mm256_permute2f128_si256()`
- Complexity: ~12-16 instructions for full 8×8 transpose
- Benefit: No memory access

### Option 2: Adapt RIPEMD160 Input Loading
- Modify `transpose_and_load()` to accept transposed input
- Load directly from SHA256 state registers
- Effectively skip the transpose in RIPEMD160 input
- Requires new RIPEMD160 Transform variant

### Option 3: Hybrid Approach
- Keep SHA256 output in transposed format
- Modify RIPEMD160 to work with transposed message schedule
- Adjust word indexing in RIPEMD160 rounds
- Most code changes but potentially fastest

---

## Register State Summary

### SHA256 Final State (8 registers)
```
Register  Content                         Size
------------------------------------------------------
s[0]      a0, a1, a2, a3, a4, a5, a6, a7  8×32-bit (word 0 of 8 hashes)
s[1]      b0, b1, b2, b3, b4, b5, b6, b7  8×32-bit (word 1 of 8 hashes)
s[2]      c0, c1, c2, c3, c4, c5, c6, c7  8×32-bit (word 2 of 8 hashes)
s[3]      d0, d1, d2, d3, d4, d5, d6, d7  8×32-bit (word 3 of 8 hashes)
s[4]      e0, e1, e2, e3, e4, e5, e6, e7  8×32-bit (word 4 of 8 hashes)
s[5]      f0, f1, f2, f3, f4, f5, f6, f7  8×32-bit (word 5 of 8 hashes)
s[6]      g0, g1, g2, g3, g4, g5, g6, g7  8×32-bit (word 6 of 8 hashes)
s[7]      h0, h1, h2, h3, h4, h5, h6, h7  8×32-bit (word 7 of 8 hashes)
```

### RIPEMD160 Expected Input (8 message blocks)
```
Block  Content                              Size
------------------------------------------------------
blk[0] w0, w1, w2, w3, w4, w5, w6, w7      8×32-bit (hash 0 words 0-7)
blk[1] w0, w1, w2, w3, w4, w5, w6, w7      8×32-bit (hash 1 words 0-7)
blk[2] w0, w1, w2, w3, w4, w5, w6, w7      8×32-bit (hash 2 words 0-7)
blk[3] w0, w1, w2, w3, w4, w5, w6, w7      8×32-bit (hash 3 words 0-7)
blk[4] w0, w1, w2, w3, w4, w5, w6, w7      8×32-bit (hash 4 words 0-7)
blk[5] w0, w1, w2, w3, w4, w5, w6, w7      8×32-bit (hash 5 words 0-7)
blk[6] w0, w1, w2, w3, w4, w5, w6, w7      8×32-bit (hash 6 words 0-7)
blk[7] w0, w1, w2, w3, w4, w5, w6, w7      8×32-bit (hash 7 words 0-7)
```

---

## Conclusions

1. **Format is fundamentally compatible** - both use 32-bit little-endian words
2. **Size matches perfectly** - SHA256 output (32 bytes) = RIPEMD160 input (32 bytes)
3. **Main barrier is data layout** - transposed vs. non-transposed
4. **Solution requires efficient transpose or format adaptation**
5. **All data fits in AVX2 registers** - no memory access theoretically needed
6. **Fused pipeline is feasible** with proper transpose implementation

---

## Next Steps (Subsequent Subtasks)

1. **Subtask 1.2**: Implement in-register 8×8 transpose using AVX2 shuffle instructions
2. **Subtask 1.3**: Create fused SHA256→RIPEMD160 function that bypasses memory
3. **Subtask 1.4**: Benchmark performance vs. separate function calls
4. **Subtask 1.5**: Verify correctness with test vectors

---

**Study completed**: 2026-02-25
**Subtask**: 1-1 (Phase 1: Implement Fused Hash Functions)
**Status**: ✓ Complete
