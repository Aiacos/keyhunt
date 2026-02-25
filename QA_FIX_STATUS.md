# QA Fix Session 0 - Status Report

## Issue: SHA512 AVX2 Implementation Produces Incorrect Hashes

### Current Status
- AVX-512 implementation: ✅ ALL TESTS PASS (3/3)
- AVX2 implementation: ❌ ALL TESTS FAIL (0/4)
- Blocking: CRITICAL - AVX2 produces mathematically incorrect hashes

### Investigation Performed

#### 1. Lane Ordering Analysis (COMPLETED)
- ✅ Verified `_mm256_set_epi64x` parameter ordering
- ✅ Compared against working RIPEMD160 AVX2 implementation
- ✅ Confirmed unpacking logic matches established patterns
- **Conclusion**: Lane ordering appears correct

#### 2. Data Flow Verification (COMPLETED)
- ✅ Verified input loading with LOADW macro
- ✅ Verified endianness handling (__builtin_bswap64)
- ✅ Verified output writing with write_be64
- **Conclusion**: Data marshaling appears correct

#### 3. Algorithm Constants (COMPLETED)
- ✅ SHA-512 round constants (K array) match spec
- ✅ Initial state values match SHA-512 spec
- **Conclusion**: Constants are correct

#### 4. Test Input Analysis (COMPLETED)
- Discovered tests provide UNPADDED 128-byte blocks
- Scalar SHA512 does internal padding
- AVX-512 works with same unpadded inputs
- **Mystery**: Why does AVX-512 pass but AVX2 fail with identical input pattern?

### Attempts Made
1. ❌ Reversed input loading order - tests still fail
2. ❌ Modified unpacking order - tests still fail
3. ✅ Verified against multiple working implementations
4. ✅ Extensive code review of Transform logic

### Remaining Unknowns
- Subtle bug in AVX2 round function or message schedule
- Potential undefined behavior or compiler optimization issue
- Possible test framework issue that only affects AVX2

### Recommended Next Steps (from QA Report)
1. Add intermediate state logging after each round
2. Compare AVX2 vs scalar intermediate values round-by-round
3. Test with manually padded inputs
4. Use NIST test vectors with known intermediate states

### Blocker
Without ability to run custom debug builds and inspect intermediate state,
further progress requires:
- More specific error information from test failures
- Known-good intermediate values for comparison
- Or expert review of the AVX2 Transform implementation

### Time Spent
~2 hours of analysis and debugging attempts

### Recommendation
Escalate to maintainer or request QA provide byte-level comparison of
expected vs actual hash outputs to pinpoint where computation diverges.
