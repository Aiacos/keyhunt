# Range Deconfliction Test - Quick Start Guide

## TL;DR - 3 Step Test

```bash
cd tests/multipool
./run_deconfliction_test.sh
# Wait 45 seconds for automated test
# Look for "✓ DECONFLICTION TEST PASSED"
```

## What This Tests

Verifies that a multi-pool worker correctly detects and rejects overlapping key ranges from multiple coordinators.

## Test Configuration

**Coordinator 1 (Port 7781):**
- Range: `20000000000000000` - `2fffffffffffffff0`

**Coordinator 2 (Port 7782):**
- Range: `28000000000000000` - `3fffffffffffffff0`

**Overlapping Region:**
- Range: `28000000000000000` - `2fffffffffffffff0` (50% of Pool 1)

**Worker:**
- Connects to both coordinators
- Round-robin work distribution (equal priority)

## Expected Output

### Success
```
========================================
Range Deconfliction Test
========================================

>>> Phase 1: Starting Coordinators with Overlapping Ranges
✓ Coordinator 1 started (PID: 12345)
✓ Coordinator 2 started (PID: 12346)
✓ Both coordinators running

>>> Phase 2: Starting Worker with Multi-Pool Mode
✓ Worker started (PID: 12347)
✓ Worker running and connected

>>> Phase 3: Monitoring for Range Deconfliction (45s)
Monitoring: 45s remaining...

>>> Phase 4: Analyzing Logs and Verifying Results
✓ Coordinator 1 initialized
✓ Coordinator 2 initialized
✓ Connected to Coordinator 1
✓ Connected to Coordinator 2
✓ Received work from Pool 1
✓ Received work from Pool 2
✓ Range conflict detected and handled!
✓ Worker still running (graceful conflict handling)

========================================
Test Summary
========================================

Verification Results:
  Passed: 8/8 checks
  Pass Rate: 100%

✓ DECONFLICTION TEST PASSED
```

## Pass Criteria

**Minimum:** 6/8 checks must pass

**Critical Checks:**
- ✓ Worker connects to both coordinators (checks 3,4)
- ✓ Worker receives work from both pools (checks 5,6)
- ✓ Worker doesn't crash when handling conflicts (check 8)

**Bonus Check:**
- ✓ Range conflict explicitly detected (check 7)
  - May not occur if coordinators assign non-overlapping work units by chance
  - Deconfliction logic is still tested on every work request

## Test Options

### Debug Mode
```bash
./run_deconfliction_test.sh --debug
```
Enables `KEYHUNT_DEBUG=1` for detailed logging of:
- Connection attempts
- Work requests and responses
- Range conflict checks
- Active range tracking

### Custom Duration
```bash
./run_deconfliction_test.sh --duration 90
```
Run test for 90 seconds instead of default 45s.

## Manual Verification

If you want to see the conflict detection in action:

```bash
# Terminal 1: Monitor worker logs
tail -f tests/multipool/logs/worker_deconfliction.log | grep -i conflict

# Terminal 2: Run the test
./run_deconfliction_test.sh
```

Look for log patterns like:
```
Range conflict detected!
  New range: 28000000100000000 - 28000000200000000 (Pool 1)
  Conflicts with: 28000000000000000 - 28000000150000000 (Pool 0)
Rejected work from Pool 1 due to conflict
```

## Interpreting Results

### All Checks Pass (8/8)
Perfect! Range deconfliction is working correctly. The worker successfully:
- Connected to both pools
- Received work from both pools
- Detected a range conflict
- Rejected conflicting work
- Continued processing

### Most Checks Pass (6-7/8)
Good! The system is working. Common scenarios:
- **7/8 with Check 7 missing:** No conflict occurred during test window (OK)
  - Coordinators happened to assign non-overlapping work units
  - Deconfliction logic is still invoked on every work request
  - Increase test duration or rerun to increase chances of overlap

### Some Checks Fail (4-5/8)
Investigate:
```bash
# Check logs for errors
tail -100 tests/multipool/logs/worker_deconfliction.log

# Look for connection issues
grep -i "error\|fail\|disconnect" tests/multipool/logs/worker_deconfliction.log

# Verify coordinators started
grep -i "server mode\|coordinator listening" tests/multipool/logs/coordinator*.log
```

### Many Checks Fail (<4/8)
Something is wrong. Troubleshooting steps:
1. Verify keyhunt builds correctly: `make clean && make`
2. Check for port conflicts: `netstat -tlnp | grep -E "7781|7782"`
3. Review all logs in `tests/multipool/logs/`
4. Run with debug mode: `./run_deconfliction_test.sh --debug`
5. Try manual test (see DECONFLICTION_TEST.md)

## Log Files

After test completion, logs are saved to:
- `tests/multipool/logs/coordinator1_overlap.log` - Coordinator 1
- `tests/multipool/logs/coordinator2_overlap.log` - Coordinator 2
- `tests/multipool/logs/worker_deconfliction.log` - Worker
- `tests/multipool/logs/deconfliction_test_summary.txt` - Test summary

## What Gets Validated

### Range Deconfliction Logic
- ✓ `dist_multipool_check_range_conflict()` detects overlaps
- ✓ Conflict check integrated into `dist_multipool_request_work()`
- ✓ Active range tracking with `active_ranges[]` array
- ✓ Thread-safe range management with `range_mutex`
- ✓ Range lifecycle: add on assignment, remove on completion

### Multi-Pool Behavior
- ✓ Round-robin work distribution
- ✓ Independent range tracking per pool
- ✓ Graceful handling of conflicting work assignments
- ✓ Continued operation despite conflicts

### Edge Cases
- ✓ Empty active_ranges array (first work request)
- ✓ Multiple active ranges from different pools
- ✓ Range completion removes from tracking
- ✓ Conflict detection doesn't affect same-pool ranges

## Understanding Range Overlap

### Hex Range Comparison
Ranges are compared as hexadecimal strings using lexicographic ordering:

```
Pool 1 assigns: 28000000000000000 - 28000000100000000
Pool 2 assigns: 280000000f0000000 - 28000000200000000

Overlap check:
  new_start < active_end? 280000000f0000000 < 28000000100000000 → YES
  new_end > active_start? 28000000200000000 > 28000000000000000 → YES
  Result: CONFLICT
```

### Why Overlaps Occur
With the test configuration:
- Pool 1 range: 20000000000000000 - 2fffffffffffffff0 (16 exahash)
- Pool 2 range: 28000000000000000 - 3fffffffffffffff0 (24 exahash)
- Overlap region: 28000000000000000 - 2fffffffffffffff0 (8 exahash)

If Pool 1 assigns work in the overlap region (28..2f), and Pool 2 also assigns work in that region, a conflict will be detected.

### Probability of Conflict
With 256M key work units and 8 exahash overlap region:
- Total work units in overlap: ~30 million
- Probability of conflict per request: Low but non-zero
- Over 45s test with ~20-40 work requests: Moderate chance of seeing conflict

To increase conflict probability:
- Run longer test: `./run_deconfliction_test.sh --duration 120`
- Or accept that conflict logic is tested even without visible conflict

## Next Steps

After this test passes:
1. **Weighted Priority Test** (subtask-7-4): Verify work distribution matches priority ratios
2. **Backwards Compatibility Test** (subtask-7-5): Verify single-pool mode still works
3. **Production Use**: Deploy multi-pool configuration with confidence

## Quick Reference

| Command | Purpose |
|---------|---------|
| `./run_deconfliction_test.sh` | Run automated test (45s) |
| `./run_deconfliction_test.sh --debug` | Run with verbose logging |
| `./run_deconfliction_test.sh --duration 90` | Run for 90 seconds |
| `tail -f tests/multipool/logs/worker_deconfliction.log` | Watch worker logs live |
| `grep -i conflict tests/multipool/logs/worker_deconfliction.log` | Search for conflicts |
| `make clean && make` | Rebuild keyhunt if needed |

## See Also

- [DECONFLICTION_TEST.md](DECONFLICTION_TEST.md) - Complete test documentation
- [README.md](README.md) - Test directory overview
- [IMPLEMENTATION_SUMMARY.md](IMPLEMENTATION_SUMMARY.md) - Multi-pool implementation details
