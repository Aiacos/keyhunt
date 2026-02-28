# Multi-Pool Coordination Tests

Comprehensive integration tests for the multi-pool worker coordination feature.

## Overview

This directory contains tests that verify the multi-pool worker can:
- Connect to multiple coordinators simultaneously
- Distribute work based on priority weighting
- Detect and avoid range conflicts across pools
- Automatically failover when coordinators go offline
- Maintain backwards compatibility with single-pool configurations

## Available Tests

### 1. Basic Multi-Pool Connection Test

**Script**: `run_test.sh`
**Duration**: ~60 seconds
**Purpose**: Verify worker can connect to 2 coordinators and receive work from both

**Usage**:
```bash
./run_test.sh
```

**What it validates**:
- Worker connects to both coordinators
- Work distribution across pools
- Range deconfliction prevents duplicate work
- Priority-weighted work selection
- Multi-pool manager functionality

**Quick start**: See [QUICKSTART.md](QUICKSTART.md)
**Full docs**: See [IMPLEMENTATION_SUMMARY.md](IMPLEMENTATION_SUMMARY.md)

---

### 2. Automatic Failover Test

**Script**: `run_failover_test.sh`
**Duration**: ~55 seconds
**Purpose**: Verify automatic reconnection when coordinator goes offline

**Usage**:
```bash
./run_failover_test.sh
```

**What it validates**:
- Disconnect detection via heartbeat failures
- Worker survives coordinator failure (continues with remaining pools)
- Exponential backoff reconnection (1s → 2s → 4s → 8s → 16s → 32s → 60s)
- Automatic recovery when coordinator restarts
- Background reconnection thread functionality

**Quick start**: See [FAILOVER_QUICKSTART.md](FAILOVER_QUICKSTART.md)
**Full docs**: See [FAILOVER_TEST.md](FAILOVER_TEST.md)

**Test phases**:
1. **Phase 1** (15s): Both coordinators active - baseline
2. **Phase 2** (20s): Kill coordinator 1 - verify failover
3. **Phase 3** (20s): Restart coordinator 1 - verify reconnection

---

### 3. Range Deconfliction Test

**Script**: `run_deconfliction_test.sh`
**Duration**: ~55 seconds
**Purpose**: Verify worker rejects overlapping work from multiple coordinators

**Usage**:
```bash
./run_deconfliction_test.sh
```

**What it validates**:
- `dist_multipool_check_range_conflict()` detects hex string overlaps
- Worker rejects conflicting work from one pool
- Worker requests work from different pool after rejection
- `active_ranges[]` array tracks all assigned ranges
- Thread-safe range management with `range_mutex`
- Range lifecycle: add on assignment, remove on completion

**Quick start**: See [DECONFLICTION_QUICKSTART.md](DECONFLICTION_QUICKSTART.md)
**Full docs**: See [DECONFLICTION_TEST.md](DECONFLICTION_TEST.md)

**Configuration**:
- **Pool 1**: Range `20000000000000000` - `2fffffffffffffff0`
- **Pool 2**: Range `28000000000000000` - `3fffffffffffffff0`
- **Overlap**: 50% overlap in `28..2f` region

---

### 4. Weighted Priority Distribution Test

**Script**: `run_priority_test.sh`
**Duration**: ~90 seconds
**Purpose**: Verify work distribution matches configured pool priorities

**Usage**:
```bash
./run_priority_test.sh
```

**What it validates**:
- Work distribution follows priority weighting
- Pool 1 (priority 75) receives ~75% of work
- Pool 2 (priority 25) receives ~25% of work
- Actual ratio matches expected 3:1 ratio (±0.5 tolerance)
- Detects if using round-robin instead of priority-weighted

**Quick start**: See [PRIORITY_QUICKSTART.md](PRIORITY_QUICKSTART.md)
**Full docs**: See [PRIORITY_TEST.md](PRIORITY_TEST.md)

**Expected results**:
- **PASS**: Ratio 2.5:1 to 3.5:1 (priority-weighted working)
- **PARTIAL**: Ratio ~1:1 (round-robin, priority not implemented)
- **FAIL**: Other ratios or insufficient samples

---

### 5. Backwards Compatibility Test ⭐ NEW

**Script**: `run_backwards_compat_test.sh`
**Duration**: ~30 seconds
**Purpose**: Verify old single-pool configs work without modification

**Usage**:
```bash
./run_backwards_compat_test.sh
```

**What it validates**:
- Legacy config files (no `pool_list`, uses `server` section) still work
- Worker uses single-pool code path (NOT multi-pool)
- Automatic migration from legacy format to new format
- Zero regression for existing users
- Single coordinator connection behavior

**Quick start**: See [BACKWARDS_COMPAT_QUICKSTART.md](BACKWARDS_COMPAT_QUICKSTART.md)
**Full docs**: See [BACKWARDS_COMPAT_TEST.md](BACKWARDS_COMPAT_TEST.md)

**Critical check**: Worker must NOT activate multi-pool mode for legacy configs.

---

## Test Summary

| Test | Duration | Coordinators | Purpose | Pass Criteria |
|------|----------|--------------|---------|---------------|
| **Basic** | 60s | 2 | Multi-pool connection | 6/8 checks |
| **Failover** | 55s | 2 (1 killed) | Auto-reconnection | 6/8 checks |
| **Deconfliction** | 55s | 2 (overlapping) | Range conflict detection | 6/8 checks |
| **Priority** | 90s | 2 (different priorities) | Weighted distribution | 6/8 checks + ratio |
| **Backwards Compat** | 30s | 1 | Legacy config support | 5/8 checks + no multi-pool |

## Configuration Files

### Coordinator Configs

- `coordinator1_config.json` - Basic coordinator (port 7771)
- `coordinator2_config.json` - Basic coordinator (port 7772)
- `coordinator1_overlap_config.json` - Coordinator with overlapping range (port 7781)
- `coordinator2_overlap_config.json` - Coordinator with overlapping range (port 7782)
- `coordinator1_priority_config.json` - High priority coordinator (port 7791, priority 75)
- `coordinator2_priority_config.json` - Low priority coordinator (port 7792, priority 25)

### Worker Configs

- `worker_multipool_config.json` - Multi-pool worker (2 coordinators)
- `worker_deconfliction_config.json` - Multi-pool with deconfliction
- `worker_priority_config.json` - Multi-pool with priority weighting
- `worker_legacy_singlepool_config.json` - **Legacy single-pool format** (backwards compatibility)

## Running All Tests

```bash
# Run all tests sequentially
./run_test.sh
./run_failover_test.sh
./run_deconfliction_test.sh
./run_priority_test.sh
./run_backwards_compat_test.sh
```

**Total time**: ~290 seconds (~5 minutes)

## Test Options

All test scripts support:

```bash
--debug          Enable debug output (KEYHUNT_DEBUG=1)
--duration N     Override test duration in seconds
--help           Show help message
```

Example:
```bash
./run_failover_test.sh --debug --duration 120
```

## Log Files

All tests write to `logs/` directory:

```
logs/
├── coordinator1.log              # Basic test coordinator 1
├── coordinator2.log              # Basic test coordinator 2
├── worker.log                    # Basic test worker
├── coordinator1_overlap.log      # Deconfliction test coordinator 1
├── coordinator2_overlap.log      # Deconfliction test coordinator 2
├── worker_deconfliction.log      # Deconfliction test worker
├── coordinator_legacy.log        # Backwards compat test coordinator
├── worker_legacy.log             # Backwards compat test worker
└── *_test_summary.txt            # Test result summaries
```

## Pass Criteria

Each test has 8 verification checks. **Minimum 6 of 8 must pass** for overall PASS.

### Critical Checks (Must Pass)

1. ✅ Coordinators start successfully
2. ✅ Worker connects to coordinator(s)
3. ✅ Worker receives work
4. ✅ Worker processes keys (shows progress)
5. ✅ Worker survives entire test (no crashes)

### Test-Specific Checks

6. **Failover**: Disconnect detected + reconnection successful
7. **Deconfliction**: Range conflict detected + worker continues
8. **Priority**: Ratio matches expected (3:1 ± 0.5)
9. **Backwards Compat**: Multi-pool mode NOT activated (critical!)

## Implementation Reference

### Multi-Pool Manager

**File**: `src/distributed/distributed.c`, `src/distributed/distributed.h`

**Key functions**:
- `dist_multipool_init()` - Initialize multi-pool manager
- `dist_multipool_add_pool()` - Add pool to manager
- `dist_multipool_connect_all()` - Connect to all pools
- `dist_multipool_request_work()` - Weighted round-robin work requests
- `dist_multipool_check_range_conflict()` - Detect overlapping ranges
- `dist_multipool_mark_range_done()` - Release completed range
- `dist_multipool_reconnect()` - Reconnect with exponential backoff
- `dist_multipool_heartbeat_all()` - Health check all pools

### Worker Integration

**File**: `src/wizard/wizard_client.c`

**Mode detection** (line 1123):
```c
int use_multipool = (cfg->pool_count > 1);
```

- `pool_count > 1`: Multi-pool mode
- `pool_count <= 1`: Single-pool mode (backwards compatible)

### Configuration Migration

**File**: `src/wizard/wizard_config.c` (lines 420-434)

Automatic migration from legacy format:
```c
if (cfg->pool_count == 0 && cfg->server_host[0] != '\0') {
    cfg->pool_count = 1;
    strncpy(cfg->pools[0].host, cfg->server_host, ...);
    cfg->pools[0].priority = 100;
    cfg->pool_failover_enabled = false;
}
```

## Troubleshooting

### All Tests Fail: "keyhunt binary not found"

**Solution**: Build keyhunt first
```bash
cd ../..
make clean && make
```

### Test Fails: Port Already in Use

**Check**:
```bash
netstat -tuln | grep 7771
```

**Solution**: Kill existing processes
```bash
pkill -f "keyhunt.*7771"
```

### Test Fails: Worker Crashed

**Debug**:
```bash
./run_test.sh --debug
tail -100 logs/worker.log
```

### Test Fails: Checks Don't Pass

**Review logs**:
```bash
cat logs/*_test_summary.txt
grep -i error logs/*.log
grep -i fail logs/*.log
```

## Manual Testing

For interactive testing:

1. **Terminal 1** - Coordinator 1:
   ```bash
   ../../keyhunt --wizard --load coordinator1_config.json
   ```

2. **Terminal 2** - Coordinator 2:
   ```bash
   ../../keyhunt --wizard --load coordinator2_config.json
   ```

3. **Terminal 3** - Worker:
   ```bash
   KEYHUNT_DEBUG=1 ../../keyhunt --wizard --load worker_multipool_config.json
   ```

4. **Terminal 4** - Monitor:
   ```bash
   tail -f logs/worker.log
   ```

## Success Metrics

All tests should achieve:

- ✅ **Connection**: Worker connects to all configured pools
- ✅ **Distribution**: Work is distributed based on strategy (round-robin or priority-weighted)
- ✅ **Deconfliction**: Overlapping ranges are detected and rejected
- ✅ **Failover**: Worker survives coordinator failures and auto-reconnects
- ✅ **Stability**: No crashes, memory leaks, or unexpected errors
- ✅ **Backwards Compat**: Legacy configs work without modification

## Related Documentation

- [IMPLEMENTATION_SUMMARY.md](IMPLEMENTATION_SUMMARY.md) - Full implementation details
- [TEST_VALIDATION.md](TEST_VALIDATION.md) - Test validation methodology
- [../../CLAUDE.md](../../CLAUDE.md) - Project overview and architecture

## Support

For issues or questions:
1. Check test documentation in this directory
2. Review implementation code in `src/distributed/` and `src/wizard/`
3. Run tests with `--debug` flag for detailed output
4. Check logs in `logs/` directory
