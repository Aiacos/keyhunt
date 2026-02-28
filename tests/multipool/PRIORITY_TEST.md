# Priority Distribution Test

## Overview

This test validates that the multi-pool worker correctly distributes work requests according to configured pool priorities. It verifies that priority-weighted distribution matches the expected ratio.

## Test Objectives

1. Verify worker can connect to multiple pools with different priorities
2. Measure actual work distribution over time
3. Calculate distribution ratio
4. Verify ratio matches configured priorities (within tolerance)
5. Document implementation status of priority-weighted distribution

## Test Architecture

### Configuration

```
┌─────────────────────────────────────────┐
│  Pool 1 (Priority: 75)                  │
│  localhost:7791                         │
│  Range: 0x20..0x2f                      │
│  Expected: 75% of work                  │
└──────────────┬──────────────────────────┘
               │
               ├─────────────────────┐
               │                     │
               ▼                     ▼
┌──────────────────────────┐   ┌────────────────────────┐
│  Multi-Pool Worker       │   │  Pool 2 (Priority: 25) │
│  Priority Strategy: 0    │◄──┤  localhost:7792        │
│  (weighted distribution) │   │  Range: 0x30..0x3f     │
└──────────────────────────┘   │  Expected: 25% of work │
                               └────────────────────────┘

Expected Ratio: 75:25 = 3:1
```

### Priority Weighting

- **Pool 1**: Priority 75 (75% of total weight)
- **Pool 2**: Priority 25 (25% of total weight)
- **Expected Ratio**: 3:1 (Pool 1 : Pool 2)
- **Tolerance**: ±0.5 (accepts 2.5:1 to 3.5:1)

## Test Phases

### Phase 1: Coordinator Startup (5 seconds)

1. Start Coordinator 1 on port 7791
   - Range: 0x20000000000000000 - 0x2ffffffffffffffff
   - Work unit size: 268435456 (256M keys)
   - Priority: 75

2. Start Coordinator 2 on port 7792
   - Range: 0x30000000000000000 - 0x3ffffffffffffffff
   - Work unit size: 268435456 (256M keys)
   - Priority: 25

**Expected logs:**
```
Coordinator 1: "Server listening on 0.0.0.0:7791"
Coordinator 2: "Server listening on 0.0.0.0:7792"
```

### Phase 2: Worker Connection (5 seconds)

1. Worker reads multi-pool configuration
2. Detects pool_count = 2, pool_strategy = 0 (priority-weighted)
3. Connects to both coordinators
4. Starts background heartbeat and reconnect threads

**Expected logs:**
```
[multipool] Connecting to 2 pool(s)...
[multipool] Successfully connected to pool 0 (127.0.0.1:7791)
[multipool] Successfully connected to pool 1 (127.0.0.1:7792)
[multipool] Connection summary: 2 successful, 0 failed
```

### Phase 3: Work Distribution Monitoring (90 seconds)

1. Worker requests work using `dist_multipool_request_work()`
2. Test script monitors worker log every 10 seconds
3. Counts work assignments from each pool:
   - `grep "Work assigned from pool 0"` → Pool 1 count
   - `grep "Work assigned from pool 1"` → Pool 2 count
4. Calculates running ratio: `Pool1_count / Pool2_count`

**Expected logs (priority-weighted):**
```
[multipool] Work assigned from pool 0 (127.0.0.1:7791): 20... -> 20...
[multipool] Work assigned from pool 0 (127.0.0.1:7791): 21... -> 21...
[multipool] Work assigned from pool 0 (127.0.0.1:7791): 22... -> 22...
[multipool] Work assigned from pool 1 (127.0.0.1:7792): 30... -> 30...
[multipool] Work assigned from pool 0 (127.0.0.1:7791): 23... -> 23...
[multipool] Work assigned from pool 0 (127.0.0.1:7791): 24... -> 24...
[multipool] Work assigned from pool 0 (127.0.0.1:7791): 25... -> 25...
[multipool] Work assigned from pool 1 (127.0.0.1:7792): 31... -> 31...
```
*Note: 3 assignments from Pool 1 for every 1 from Pool 2*

**Expected logs (round-robin, if priority NOT implemented):**
```
[multipool] Work assigned from pool 0 (127.0.0.1:7791): 20... -> 20...
[multipool] Work assigned from pool 1 (127.0.0.1:7792): 30... -> 30...
[multipool] Work assigned from pool 0 (127.0.0.1:7791): 21... -> 21...
[multipool] Work assigned from pool 1 (127.0.0.1:7792): 31... -> 31...
```
*Note: Strict alternation between pools*

### Phase 4: Results Analysis

1. Final count of work units from each pool
2. Calculate percentages and ratio
3. Compare with expected distribution
4. Report pass/fail based on tolerance

## Verification Checks (8 total)

The test performs 8 verification checks:

1. ✓ **Coordinator 1 started** - Server listening on port 7791
2. ✓ **Coordinator 2 started** - Server listening on port 7792
3. ✓ **Worker connected to Pool 1** - Connection to 127.0.0.1:7791
4. ✓ **Worker connected to Pool 2** - Connection to 127.0.0.1:7792
5. ✓ **Work from Pool 1** - At least 1 work unit received
6. ✓ **Work from Pool 2** - At least 1 work unit received
7. ✓ **Sufficient sample size** - At least 10 work units total
8. ✓ **Distribution ratio matches** - Actual ratio within expected ± tolerance

**Pass criteria:** 6/8 checks pass AND check #8 (ratio match) passes

## Expected Results

### With Priority-Weighted Implementation

```
WORK DISTRIBUTION:
  Pool 1 (priority 75): 15 work units (75.0%)
  Pool 2 (priority 25): 5 work units (25.0%)
  Total: 20 work units

EXPECTED DISTRIBUTION:
  Pool 1: 75.0%
  Pool 2: 25.0%

ACTUAL RATIO:
  3.00:1 (expected: 3.0:1)

✓ PRIORITY DISTRIBUTION TEST PASSED
```

### Without Priority-Weighted Implementation (Round-Robin)

```
WORK DISTRIBUTION:
  Pool 1 (priority 75): 10 work units (50.0%)
  Pool 2 (priority 25): 10 work units (50.0%)
  Total: 20 work units

EXPECTED DISTRIBUTION:
  Pool 1: 75.0%
  Pool 2: 25.0%

ACTUAL RATIO:
  1.00:1 (expected: 3.0:1)

⚠ PRIORITY DISTRIBUTION TEST PARTIAL PASS

Basic connectivity passed, but distribution ratio does NOT match
configured priorities. This suggests priority-weighted distribution
is NOT YET IMPLEMENTED. Current behavior is simple round-robin.
```

## Implementation Details

### Priority-Weighted Distribution Algorithm

**Current implementation** (src/distributed/distributed.c):
```c
// Simple round-robin (line 3892)
multipool->current_pool_index = (pool_index + 1) % multipool->pool_count;
```

**Expected implementation** (priority-weighted):
```c
// Calculate total weight
int total_weight = 0;
for (int i = 0; i < multipool->pool_count; i++) {
    if (multipool->clients[i].connected) {
        total_weight += multipool->pool_priorities[i];  // New field needed
    }
}

// Weighted random selection
int random_value = rand() % total_weight;
int cumulative_weight = 0;
int selected_pool = 0;

for (int i = 0; i < multipool->pool_count; i++) {
    if (!multipool->clients[i].connected) continue;

    cumulative_weight += multipool->pool_priorities[i];
    if (random_value < cumulative_weight) {
        selected_pool = i;
        break;
    }
}
```

**Required changes:**

1. Add priority field to dist_multipool_client_t:
   ```c
   int pool_priorities[DIST_MAX_POOLS];
   ```

2. Pass priority in dist_multipool_add_pool():
   ```c
   int dist_multipool_add_pool(dist_multipool_client_t *multipool,
                                const char *coordinator_host,
                                int coordinator_port,
                                double perf_score,
                                int priority);  // New parameter
   ```

3. Update wizard_client.c to pass cfg->pools[i].priority:
   ```c
   int pool_idx = dist_multipool_add_pool(&multipool,
                                          cfg->pools[i].host,
                                          cfg->pools[i].port,
                                          sysinfo.cpu_score,
                                          cfg->pools[i].priority);  // Pass priority
   ```

4. Implement weighted selection in dist_multipool_request_work()

## Running the Test

### Quick Start

```bash
cd tests/multipool
./run_priority_test.sh
```

### With Options

```bash
# Run with debug logging (KEYHUNT_DEBUG=1)
./run_priority_test.sh --debug

# Run for custom duration (default: 90 seconds)
./run_priority_test.sh --duration 120

# Debug mode + custom duration
./run_priority_test.sh --debug --duration 60
```

### Expected Output

```
========================================
  Priority Distribution Test
========================================

Test Configuration:
  - Coordinator 1: localhost:7791 (priority: 75)
  - Coordinator 2: localhost:7792 (priority: 25)
  - Expected Ratio: 3.0:1 (Pool 1:Pool 2)
  - Tolerance: ±0.5 (3.0 ± 0.5)
  - Worker: Priority-weighted distribution mode
  - Test Duration: 90s
  - Log Directory: tests/multipool/logs

[Progress updates every 10 seconds...]

[90s/90s] Work distribution: Pool 1=15, Pool 2=5, Total=20, Ratio=3.00:1

========================================
  VERIFICATION CHECKS
========================================

✓ Check 1/8: Coordinator 1 started successfully
✓ Check 2/8: Coordinator 2 started successfully
✓ Check 3/8: Worker connected to Pool 1 (port 7791)
✓ Check 4/8: Worker connected to Pool 2 (port 7792)
✓ Check 5/8: Worker received work from Pool 1 (15 units)
✓ Check 6/8: Worker received work from Pool 2 (5 units)
✓ Check 7/8: Sufficient sample size (20 work units)
✓ Check 8/8: Distribution ratio matches priority (3.00:1, expected 3.0±0.5:1)

Checks passed: 8 / 8

✓ PRIORITY DISTRIBUTION TEST PASSED
```

## Manual Test

For manual testing and observation:

### Terminal 1: Coordinator 1
```bash
cd /path/to/keyhunt
KEYHUNT_DEBUG=1 ./keyhunt --wizard-server \
    --config tests/multipool/coordinator1_priority_config.json
```

### Terminal 2: Coordinator 2
```bash
cd /path/to/keyhunt
KEYHUNT_DEBUG=1 ./keyhunt --wizard-server \
    --config tests/multipool/coordinator2_priority_config.json
```

### Terminal 3: Worker
```bash
cd /path/to/keyhunt
KEYHUNT_DEBUG=1 ./keyhunt --wizard-client \
    --config tests/multipool/worker_priority_config.json
```

### Terminal 4: Monitor Distribution
```bash
cd tests/multipool/logs
watch -n 5 'grep "Work assigned from pool" worker_priority.log | tail -20'
```

**What to observe:**

- **Priority-weighted**: ~3 assignments from Pool 0 for every 1 from Pool 1
- **Round-robin**: Strict alternation between Pool 0 and Pool 1

## Troubleshooting

### Issue: All work from one pool

**Symptoms:**
```
Pool 1: 20 work units (100%)
Pool 2: 0 work units (0%)
```

**Possible causes:**
1. Pool 2 coordinator not running (check port 7792)
2. Pool 2 connection failed (check worker log for connection errors)
3. Pool 2 has no work units (check coordinator 2 range configuration)

**Fix:**
```bash
# Check if both coordinators are running
lsof -i :7791
lsof -i :7792

# Check worker connection log
grep "Failed to connect" tests/multipool/logs/worker_priority.log
```

### Issue: Sample size too small

**Symptoms:**
```
⚠ Check 7/8: Small sample size (3 work units, expected ≥10)
```

**Possible causes:**
1. Work units are very large (take long time to process)
2. Test duration too short
3. Worker threads set too low

**Fix:**
```bash
# Increase test duration
./run_priority_test.sh --duration 180

# Or reduce work unit size in coordinator configs
# Edit coordinator1_priority_config.json:
"work_unit_size": 67108864  # 64M instead of 256M
```

### Issue: Ratio doesn't match (round-robin detected)

**Symptoms:**
```
Actual ratio: 1.00:1 (expected: 3.0±0.5:1)
✗ Check 8/8: Distribution ratio does NOT match priority
```

**Meaning:**
Priority-weighted distribution is NOT YET IMPLEMENTED. The worker is using simple round-robin distribution instead of respecting pool priorities.

**To implement:**
See "Implementation Details" section above for required code changes.

## Success Criteria

Test **PASSES** if:
- At least 6 out of 8 verification checks pass
- Check #8 (distribution ratio) passes
- Actual ratio is within expected ± tolerance (2.5:1 to 3.5:1)

Test **PARTIAL PASS** if:
- At least 6 out of 8 verification checks pass
- Check #8 (distribution ratio) FAILS
- This indicates priority weighting is not yet implemented

Test **FAILS** if:
- Fewer than 6 verification checks pass
- Indicates basic multi-pool functionality is broken

## Related Tests

- **run_test.sh** - Basic multi-pool connection test
- **run_failover_test.sh** - Automatic failover test
- **run_deconfliction_test.sh** - Range deconfliction test

## Configuration Files

- `coordinator1_priority_config.json` - Pool 1 config (priority 75, port 7791)
- `coordinator2_priority_config.json` - Pool 2 config (priority 25, port 7792)
- `worker_priority_config.json` - Worker multi-pool config with priorities

## Log Files

After running the test, logs are saved to `tests/multipool/logs/`:

- `coordinator1_priority.log` - Coordinator 1 activity
- `coordinator2_priority.log` - Coordinator 2 activity
- `worker_priority.log` - Worker activity and work assignments
- `priority_test_summary.txt` - Test results summary
