# Priority Distribution Test - Quick Start Guide

## TL;DR

Test that multi-pool worker distributes work according to configured priorities (75:25 = 3:1 ratio).

```bash
cd tests/multipool
./run_priority_test.sh
```

**Expected result:** Worker gets ~75% of work from Pool 1, ~25% from Pool 2.

---

## What This Tests

**Objective:** Verify priority-weighted work distribution matches configured pool priorities.

**Test scenario:**
- Pool 1 (priority 75): Should receive ~75% of work requests
- Pool 2 (priority 25): Should receive ~25% of work requests
- Expected ratio: 3:1

**Verifies:**
- Priority weighting algorithm implementation
- Work distribution over time
- Statistical accuracy of weighted selection

---

## Quick Execution

### 1. Run the Test

```bash
cd tests/multipool
./run_priority_test.sh
```

Default settings:
- Duration: 90 seconds
- Pool 1 priority: 75 (port 7791)
- Pool 2 priority: 25 (port 7792)
- Expected ratio: 3.0:1
- Tolerance: ±0.5 (accepts 2.5:1 to 3.5:1)

### 2. Watch Progress

The script monitors work distribution every 10 seconds:

```
[10s/90s] Work distribution: Pool 1=2, Pool 2=1, Total=3, Ratio=2.00:1
[20s/90s] Work distribution: Pool 1=5, Pool 2=2, Total=7, Ratio=2.50:1
[30s/90s] Work distribution: Pool 1=8, Pool 2=3, Total=11, Ratio=2.67:1
[40s/90s] Work distribution: Pool 1=11, Pool 2=4, Total=15, Ratio=2.75:1
[50s/90s] Work distribution: Pool 1=14, Pool 2=5, Total=19, Ratio=2.80:1
[60s/90s] Work distribution: Pool 1=17, Pool 2=6, Total=23, Ratio=2.83:1
[70s/90s] Work distribution: Pool 1=20, Pool 2=7, Total=27, Ratio=2.86:1
[80s/90s] Work distribution: Pool 1=23, Pool 2=8, Total=31, Ratio=2.88:1
[90s/90s] Work distribution: Pool 1=27, Pool 2=9, Total=36, Ratio=3.00:1
```

### 3. Review Results

```
========================================
  TEST SUMMARY
========================================

Checks passed: 8 / 8

✓ PRIORITY DISTRIBUTION TEST PASSED

The multi-pool worker correctly distributes work according to
configured priorities. Pool 1 (priority 75) received ~75.0%
of work, Pool 2 (priority 25) received ~25.0%.
Actual ratio 3.00:1 matches expected 3.0:1 within tolerance.
```

---

## Pass Criteria

**FULL PASS** (exit code 0):
- 6/8 verification checks pass
- Distribution ratio matches expected (within ±0.5)
- Example: Actual ratio = 2.8:1 to 3.2:1 for expected 3.0:1

**PARTIAL PASS** (exit code 1):
- 6/8 verification checks pass
- Distribution ratio does NOT match expected
- Indicates priority weighting not yet implemented (using round-robin)

**FAIL** (exit code 1):
- Fewer than 6/8 checks pass
- Basic multi-pool functionality broken

---

## Expected Output Scenarios

### Scenario 1: Priority-Weighted Distribution (FULL PASS)

```
WORK DISTRIBUTION:
  Pool 1 (priority 75): 27 work units (75.0%)
  Pool 2 (priority 25): 9 work units (25.0%)
  Total: 36 work units

ACTUAL RATIO:
  3.00:1 (expected: 3.0:1)

✓ Check 8/8: Distribution ratio matches priority (3.00:1, expected 3.0±0.5:1)

✓ PRIORITY DISTRIBUTION TEST PASSED
```

### Scenario 2: Round-Robin Distribution (PARTIAL PASS)

```
WORK DISTRIBUTION:
  Pool 1 (priority 75): 18 work units (50.0%)
  Pool 2 (priority 25): 18 work units (50.0%)
  Total: 36 work units

ACTUAL RATIO:
  1.00:1 (expected: 3.0:1)

✗ Check 8/8: Distribution ratio does NOT match priority
  Expected: 3.0±0.5:1 (range: 2.5 to 3.5)
  Actual: 1.00:1
  Deviation: Outside acceptable range

⚠ PRIORITY DISTRIBUTION TEST PARTIAL PASS

IMPLEMENTATION STATUS:
  - Multi-pool connection: ✓ Working
  - Work distribution: ✓ Working
  - Priority weighting: ✗ NOT IMPLEMENTED

TO FIX:
  Implement priority-weighted selection in dist_multipool_request_work()
  in src/distributed/distributed.c.
```

---

## Test Options

### Debug Mode

Enable detailed logging:

```bash
./run_priority_test.sh --debug
```

Sets `KEYHUNT_DEBUG=1` for all processes. Useful for troubleshooting.

### Custom Duration

Run for longer to collect more samples:

```bash
./run_priority_test.sh --duration 180  # 3 minutes
```

Recommended for statistical accuracy with slower work processing.

### Combined Options

```bash
./run_priority_test.sh --debug --duration 120
```

---

## Verification Checks (8 total)

1. ✓ **Coordinator 1 started** - Server listening on 0.0.0.0:7791
2. ✓ **Coordinator 2 started** - Server listening on 0.0.0.0:7792
3. ✓ **Worker connected to Pool 1** - Connection to 127.0.0.1:7791
4. ✓ **Worker connected to Pool 2** - Connection to 127.0.0.1:7792
5. ✓ **Work from Pool 1** - At least 1 work unit received
6. ✓ **Work from Pool 2** - At least 1 work unit received
7. ✓ **Sufficient sample size** - At least 10 work units total
8. ✓ **Distribution ratio matches** - Actual within expected ± tolerance

**Note:** Check 7 may show warning if sample size < 10. This doesn't fail the test but indicates results may not be statistically significant.

---

## Log Files

Saved to `tests/multipool/logs/`:

- `coordinator1_priority.log` - Pool 1 activity
- `coordinator2_priority.log` - Pool 2 activity
- `worker_priority.log` - Worker activity, work assignments
- `priority_test_summary.txt` - Final results

### Useful Log Patterns

**Check work distribution:**
```bash
grep "Work assigned from pool" logs/worker_priority.log | tail -20
```

**Count assignments per pool:**
```bash
grep -c "Work assigned from pool 0" logs/worker_priority.log  # Pool 1
grep -c "Work assigned from pool 1" logs/worker_priority.log  # Pool 2
```

**Check pool strategy:**
```bash
grep "pool_strategy" logs/worker_priority.log
```

---

## Manual Test (Alternative)

If you prefer to run processes manually:

### Terminal 1: Coordinator 1 (Priority 75)
```bash
KEYHUNT_DEBUG=1 ./keyhunt --wizard-server \
    --config tests/multipool/coordinator1_priority_config.json
```

### Terminal 2: Coordinator 2 (Priority 25)
```bash
KEYHUNT_DEBUG=1 ./keyhunt --wizard-server \
    --config tests/multipool/coordinator2_priority_config.json
```

### Terminal 3: Worker
```bash
KEYHUNT_DEBUG=1 ./keyhunt --wizard-client \
    --config tests/multipool/worker_priority_config.json
```

### Terminal 4: Monitor
```bash
# Watch work distribution in real-time
watch -n 2 'grep "Work assigned from pool" tests/multipool/logs/worker_priority.log | tail -20'

# Or count manually every 30 seconds
while true; do
  echo "Pool 1: $(grep -c 'Work assigned from pool 0' tests/multipool/logs/worker_priority.log 2>/dev/null || echo 0)"
  echo "Pool 2: $(grep -c 'Work assigned from pool 1' tests/multipool/logs/worker_priority.log 2>/dev/null || echo 0)"
  sleep 30
done
```

**What to observe:**
- **Priority-weighted**: ~3 assignments from Pool 1 for every 1 from Pool 2
- **Round-robin**: Strict alternation (1 from Pool 1, 1 from Pool 2, repeat)

---

## Troubleshooting

### Only one pool receiving work

**Check both coordinators are running:**
```bash
lsof -i :7791  # Coordinator 1
lsof -i :7792  # Coordinator 2
```

**Check worker connection:**
```bash
grep "Connected to" logs/worker_priority.log
grep "Failed to connect" logs/worker_priority.log
```

### Sample size too small (< 10 work units)

**Causes:**
- Work units are large (slow processing)
- Test duration too short
- Worker threads set too low (2 threads in config)

**Solutions:**
```bash
# Run longer
./run_priority_test.sh --duration 180

# Or edit coordinator configs to reduce work unit size:
# coordinator1_priority_config.json: "work_unit_size": 67108864  # 64M
```

### Ratio doesn't match (1:1 instead of 3:1)

**This is expected if priority weighting is not yet implemented.**

The test will show:
```
⚠ PRIORITY DISTRIBUTION TEST PARTIAL PASS

Current behavior is simple round-robin distribution.
Priority-weighted distribution is NOT YET IMPLEMENTED.
```

See `PRIORITY_TEST.md` for implementation details.

---

## What Gets Tested

### Pool Configuration
- Pool 1: Priority 75, Port 7791, Range 0x20..0x2f
- Pool 2: Priority 25, Port 7792, Range 0x30..0x3f

### Distribution Strategy
- `pool_strategy: 0` = Priority-weighted
- `pool_strategy: 1` = Round-robin (equal)
- `pool_strategy: 2` = Failover-only (priority order)

### Work Unit Tracking
- Monitors `grep "Work assigned from pool X"`
- Counts assignments per pool
- Calculates ratio and percentages
- Compares with expected values

---

## Next Steps

After running this test:

1. **If PASSED:**
   - Priority-weighted distribution is working correctly
   - Move on to backwards compatibility test (subtask-7-5)

2. **If PARTIAL PASS:**
   - Multi-pool connection works
   - Work distribution works
   - But priority weighting needs implementation
   - See `PRIORITY_TEST.md` → "Implementation Details"

3. **If FAILED:**
   - Check logs in `tests/multipool/logs/`
   - Run with `--debug` flag
   - Review connection errors
   - Check coordinator ranges don't overlap

---

## Related Tests

- `run_test.sh` - Basic multi-pool connection (subtask-7-1)
- `run_failover_test.sh` - Automatic failover (subtask-7-2)
- `run_deconfliction_test.sh` - Range deconfliction (subtask-7-3)

## Documentation

- `PRIORITY_TEST.md` - Full test documentation
- `README.md` - All available tests
- `IMPLEMENTATION_SUMMARY.md` - Multi-pool architecture

---

**Test Created:** 2026-02-28
**Subtask:** 7-4 (Integration Testing - Priority Distribution)
**Status:** Ready for execution
