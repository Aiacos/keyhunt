# BSGS Integration Test Issue

## Problem
BSGS mode is hanging/timing out when running the integration test:
```bash
./keyhunt -m bsgs -f tests/125.txt -b 125 -q -s 1 -R
```

## Investigation

### Tested
1. ✓ keyhunt executable builds successfully
2. ✓ keyhunt help command works
3. ✗ BSGS mode times out after 10 seconds

### Fix Attempted
Added `grp->Set(dx)` call in `bsgs_batch_compute_points()` before `grp->ModInvOptimized()` (line 238 of bsgs_ops.cpp).

However, this was redundant because the IntGroup is already initialized with the dx array in `bsgs_batch_init()` at line 95:
```cpp
((IntGroup*)ctx->grp)->Set(ctx->dx);
```

### Possible Causes
1. Infinite loop in ModInvOptimized() or batch compute function
2. Memory access issue causing segfault caught silently
3. BSGS mode may require additional initialization before running
4. Test file or parameters may be invalid

### Next Steps
1. Check if BSGS mode works without the batch optimization (git stash changes)
2. Add debug logging to identify where it hangs
3. Run with gdb or strace to see system calls
4. Check if the issue existed before the batch optimization was added

## Test Commands
```bash
# Quick test (1 second timeout in Python)
python3 quick_bsgs_test.py

# Full test as specified
./keyhunt -m bsgs -f tests/125.txt -b 125 -q -s 10 -R
```

## Status
- Subtask: subtask-3-2 (Run existing BSGS integration tests)
- Current state: BLOCKED - BSGS mode hangs
- Date: 2026-02-26
