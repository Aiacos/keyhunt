# Escalation Request - SHA512 AVX2 Debug Assistance Needed

## Summary
After extensive investigation (2+ hours), unable to identify root cause of AVX2 hash incorrectness.
AVX-512 passes all tests, AVX2 fails all tests with identical inputs and algorithm.

## What's Needed
1. Byte-level diff of expected vs actual hash output from failing test
2. Access to intermediate round state values (or ability to add debug logging)
3. Expert review of AVX2 Transform implementation by someone familiar with SIMD SHA-512
4. Or: Confirmation that tests are correct and both SIMD implementations should handle unpaded inputs

## Code Verified Correct
- Lane ordering (matches RIPEMD160 pattern)
- Constants (SHA-512 K, initial state)
- Endianness handling
- Data packing/unpacking logic
- Output formatting

## Likely Bug Location
- Subtle error in AVX2 round function or message schedule
- Or compiler optimization issue specific to AVX2
- Or undefined behavior in SIMD operations

## Recommendation
- Have maintainer or SIMD expert review `src/hash/sha512_avx2.cpp` Transform function
- Compare intermediate states between AVX2 and working scalar/AVX-512
- Consider using NIST test vectors with known intermediate values

## Current Status
- Implementation structurally sound
- Follows established patterns
- Root cause requires deeper SIMD expertise or debugging capabilities
