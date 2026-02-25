# SHA512 AVX2 Debugging Notes

## Status
AVX-512 implementation: ✅ PASSING (3/3 tests)
AVX2 implementation: ❌ FAILING (0/4 tests)

## Investigation Summary

### Lane Ordering Analysis
- Verified AVX2 lane ordering matches established patterns from RIPEMD160 AVX2
- First argument to `_mm256_set_epi64x` correctly goes to MSB (lane 3)
- Unpacking correctly extracts from lanes 3,2,1,0 for outputs d0,d1,d2,d3
- This pattern is consistent with working implementations

### Test Input Mystery
- Tests pass UNPADED 128-byte blocks ("abc\0\0\0...")
- Tests compare against scalar SHA512 which DOES internal padding
- Yet AVX-512 passes with these same unpaded inputs
- AVX2 fails with identical input pattern

### Verified Correct
- ✅ SHA-512 constants (K array)
- ✅ Initial state values
- ✅ Endianness handling (__builtin_bswap64)
- ✅ Round function macro structure
- ✅ Message schedule expansion (WMIX)
- ✅ Output byte ordering (write_be64)

### Theories
1. Subtle bug in AVX2 round function that doesn't exist in AVX-512
2. Difference in how AVX2 vs AVX-512 handles the transpose operation
3. Test expectations may be incorrect but AVX-512 accidentally works
4. Some undefined behavior or alignment issue specific to AVX2

### Next Steps
- Compare intermediate state values (round-by-round) between AVX2 and scalar
- Test with properly padded inputs to rule out padding issues
- Check if maybe tests should be fixed rather than implementation
