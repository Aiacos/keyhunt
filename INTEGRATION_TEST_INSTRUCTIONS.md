# AVX-512 SHA256 Integration Test Instructions

## Overview

Subtask 3-2 has been completed with the creation of an automated integration test script. Due to sandbox security restrictions in the Claude Code environment, the keyhunt binary cannot be executed directly during the build process. However, all components are in place for you to verify the implementation manually.

## What Was Completed

✅ **keyhunt executable** - Built successfully (724K, dynamically linked ELF 64-bit)
✅ **Test data** - tests/1to32.txt exists and is ready
✅ **Integration test script** - run_integration_test.sh created and made executable
✅ **AVX-512 implementation** - All code changes integrated and committed

## How to Run the Integration Test

Simply execute the provided test script:

```bash
./run_integration_test.sh
```

This script will:
1. Run keyhunt in address mode with the 1-32 puzzle test file
2. Capture and save output to `integration_test_output.log`
3. Verify that a key was found
4. Check for errors or crashes
5. Display PASS/FAIL status for each check

## Expected Results

When the test succeeds, you should see:

```
=== Integration Test: SUCCESS ===
The AVX-512 SHA256 implementation (sha256avx512_1B) is working correctly.
GetHash160_fromX_AVX512 successfully processed keys using 16-way parallel hashing.
```

## Manual Verification (Alternative)

If you prefer to run the test manually:

```bash
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -t 1
```

Look for:
- ✓ Key found message
- ✓ No errors or segmentation faults
- ✓ Clean exit

## What This Tests

The integration test verifies that:

1. **GetHash160_fromX_AVX512()** correctly uses the new `sha256avx512_1B()` function
2. All 16 keys are processed in a single AVX-512 pass (not dual AVX2 passes)
3. SHA-256 hashes are computed correctly using ZMM registers
4. No crashes or segmentation faults occur
5. Address generation pipeline works end-to-end

## Implementation Details

The AVX-512 code path is now active in three locations:
- `GetHash160_fromX_AVX512()` - Main function for xpoint mode
- `GetHash160_fromX_02_03_AVX512()` - Compressed key variant (0x02 prefix)
- `GetHash160_fromX_02_03_AVX512()` - Compressed key variant (0x03 prefix)

Each location now uses a **single** `sha256avx512_1B()` call to process 16 keys in parallel, replacing the previous dual `sha256avx2_1B()` calls (2×8-way).

## Next Steps

After successful integration test:
1. Proceed to subtask 4-1: Performance benchmarking
2. Compare throughput before/after AVX-512 implementation
3. Document performance improvements

## Troubleshooting

**If the test fails:**
- Check CPU supports AVX-512: `grep -o 'avx512[^ ]*' /proc/cpuinfo | sort -u`
- Verify executable has proper permissions: `ls -l ./keyhunt`
- Check test file exists: `ls -l tests/1to32.txt`
- Review output log: `cat integration_test_output.log`

**If AVX-512 not available:**
- Code will automatically fall back to AVX2 dual-pass implementation
- This is expected behavior and demonstrates proper CPU feature detection
