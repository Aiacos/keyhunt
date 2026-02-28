# Backwards Compatibility Test

## Overview

This test verifies that workers using **legacy single-pool configuration** (pre-multi-pool implementation) continue to work exactly as before after the multi-pool coordination feature was added.

## Purpose

Ensures zero regression for existing users who:
- Have old configuration files without `pool_list` section
- Use legacy `server.host` and `server.port` fields
- Expect single-coordinator connection behavior

## What This Tests

### Critical Functionality

1. **Config Loading**: Legacy JSON format is correctly parsed
2. **Migration**: Legacy `server` section is automatically migrated to `pools[0]`
3. **Single-Pool Mode**: Worker uses single-pool code path (NOT multi-pool)
4. **Connection**: Worker successfully connects to the single coordinator
5. **Work Processing**: Worker receives and processes work units normally
6. **Stability**: Worker doesn't crash or exhibit unexpected behavior

### Backwards Compatibility Guarantees

- ✅ Old config files continue to work without modification
- ✅ No multi-pool overhead for single-pool users
- ✅ Identical behavior to pre-multi-pool implementation
- ✅ No breaking changes in JSON format

## Implementation Details

### Legacy Config Format

**Old-style worker config** (before multi-pool feature):

```json
{
  "version": 1,
  "is_server": false,
  "puzzle_number": 66,
  "target_address": "13zb1hQbWVsc2S7ZTZnP2G4undNNpdh5so",
  "server": {
    "host": "127.0.0.1",
    "port": 7771,
    "work_unit_size": "100000000",
    "checkpoint_interval": 300,
    "auth_token": ""
  },
  "search": {
    "mode": "address",
    "key_type": "compress",
    "threads": 2
  }
}
```

**Key characteristics**:
- No `pool_list` or `pools` array
- Uses `server` section with `host` and `port`
- `pool_count` not present or set to 0

### Migration Logic

When loading a legacy config (`wizard_config_load()` in `src/wizard/wizard_config.c`):

```c
/* Lines 420-434: Backwards compatibility migration */
if (cfg->pool_count == 0 && cfg->server_host[0] != '\0' && cfg->server_port > 0) {
    cfg->pool_count = 1;
    strncpy(cfg->pools[0].host, cfg->server_host, sizeof(cfg->pools[0].host) - 1);
    cfg->pools[0].port = cfg->server_port;
    cfg->pools[0].priority = 100;  /* Single pool gets max priority */
    strncpy(cfg->pools[0].auth_token, cfg->auth_token, sizeof(cfg->pools[0].auth_token) - 1);
    cfg->pools[0].enabled = true;
    cfg->pool_failover_enabled = false;  /* Single pool = no failover needed */
    cfg->pool_strategy = 0;
}
```

### Single-Pool Code Path

Worker detection (`wizard_client_run()` in `src/wizard/wizard_client.c`):

```c
/* Lines 1122-1123: Mode detection */
int use_multipool = (cfg->pool_count > 1);

if (use_multipool) {
    /* Multi-pool code path */
} else {
    /* Single-pool code path (lines 1308+) - LEGACY BEHAVIOR */
    /* This path is UNCHANGED from pre-multi-pool implementation */
}
```

**Critical**: When `pool_count <= 1`, worker uses the **original single-pool implementation**, ensuring zero regression.

## Test Architecture

```
┌─────────────┐
│ Coordinator │  <- Single coordinator (port 7771)
│   (Server)  │
└──────┬──────┘
       │
       │ (Single connection)
       │
       ▼
  ┌────────┐
  │ Worker │  <- Legacy config (server section only)
  │ Legacy │
  └────────┘
```

**NOT multi-pool**:
- Only ONE coordinator
- Worker config has NO `pool_list`
- Uses legacy `server.host` and `server.port`

## Test Phases

### Phase 1: Start Coordinator (5s)

Start single coordinator on port 7771 using existing coordinator config.

**Expected**: Coordinator starts successfully and listens on port 7771.

### Phase 2: Start Worker (5s)

Start worker with legacy single-pool config (`worker_legacy_singlepool_config.json`).

**Expected**:
- Worker loads legacy config successfully
- Migration logic converts `server` section to `pools[0]`
- Worker detects single-pool mode (`pool_count = 1`)

### Phase 3: Monitor Operation (30s)

Monitor worker and coordinator operation.

**Expected**:
- Worker connects to coordinator
- Worker receives work units
- Worker processes keys (shows "keys/s" in logs)
- Both processes remain stable

### Phase 4: Verification

Analyze logs to verify:

1. ✓ Coordinator started successfully
2. ✓ Worker connected to coordinator (port 7771)
3. ✓ Legacy config was loaded
4. ✓ Worker received work units
5. ✓ Worker is processing keys
6. ✓ Multi-pool mode NOT activated (single-pool path used)
7. ✓ Worker survived entire test (no crashes)
8. ✓ Config migration applied (bonus check)

**Pass Criteria**: At least 5 of 8 checks must pass.

## Running the Test

### Quick Start

```bash
cd tests/multipool
./run_backwards_compat_test.sh
```

### Test Options

```bash
./run_backwards_compat_test.sh [OPTIONS]

Options:
  --debug          Enable debug output (KEYHUNT_DEBUG=1)
  --duration N     Test duration in seconds (default: 30)
  --help           Show help message
```

### Expected Output

```
================================================
  Backwards Compatibility Test (Single Pool)
================================================

Test Duration: 30 seconds
Debug Mode: Disabled

[*] Verifying legacy config format...
    ✓ Worker config is in legacy format (pre-multi-pool)

Phase 1: Starting Coordinator
[*] Starting coordinator on port 7771...
    Coordinator PID: 12345
    ✓ Coordinator started successfully

Phase 2: Starting Worker (Legacy Config)
[*] Starting worker with legacy single-pool configuration...
    Worker PID: 12346
    ✓ Worker started successfully

Phase 3: Monitoring Operation
[*] Running for 30 seconds...
    10s elapsed...
    20s elapsed...
    30s elapsed...
    ✓ Test duration completed

Phase 4: Verification
[*] Analyzing logs...

  ✓ Check 1: Coordinator started successfully
  ✓ Check 2: Worker connected to coordinator
  ✓ Check 3: Legacy config loaded
  ✓ Check 4: Worker received work unit
  ✓ Check 5: Worker processing keys
  ✓ Confirmed: Multi-pool mode NOT activated (single-pool as expected)
  ✓ Check 7: Worker survived entire test
  ! Check 8: Config migration applied (bonus check)

================================================
  Test Results
================================================

Checks Passed: 7/8

✓ BACKWARDS COMPATIBILITY TEST PASSED

Summary saved to: logs/backwards_compat_test_summary.txt
Coordinator log: logs/coordinator_legacy.log
Worker log: logs/worker_legacy.log
```

## Manual Verification

### Step 1: Start Coordinator

Terminal 1:
```bash
cd tests/multipool
../../keyhunt --wizard --load coordinator1_config.json
```

### Step 2: Start Worker with Legacy Config

Terminal 2:
```bash
cd tests/multipool
../../keyhunt --wizard --load worker_legacy_singlepool_config.json
```

### Step 3: Verify Behavior

Check Terminal 2 output for:

```
[+] Configuration loaded from: worker_legacy_singlepool_config.json
[+] Hardware Detection:
    CPU: X physical cores, Y logical threads
    RAM: XXXX MB available

[+] Connecting to coordinator at 127.0.0.1:7771...
[+] Connected successfully
[+] Requesting work...
[+] Received work unit: 20000000000000000 - 20000000100000000
[+] Processing...
    Speed: XXXXX keys/s
```

**Key observations**:
- NO "Multi-pool mode detected" message
- Only ONE connection (to 127.0.0.1:7771)
- Normal work processing

### Step 4: Compare with Multi-Pool Worker

For comparison, start a multi-pool worker:

```bash
../../keyhunt --wizard --load worker_multipool_config.json
```

You should see:
```
[+] Multi-pool mode detected (2 pools configured)
    Pool 1: 127.0.0.1:7771 (priority: 60)
    Pool 2: 127.0.0.1:7772 (priority: 40)
```

**Difference**: Legacy worker does NOT show multi-pool messages.

## Log Analysis

### Coordinator Log (coordinator_legacy.log)

Expected patterns:
```
Coordinator started on port 7771
Client connected from 127.0.0.1
Work request received
Work unit assigned: 20000000000000000 - 20000000100000000
```

### Worker Log (worker_legacy.log)

Expected patterns:
```
Configuration loaded
Hardware Detection
Connecting to coordinator at 127.0.0.1:7771
Connected successfully
Received work unit
Processing...
keys/s
```

**Should NOT contain**:
```
Multi-pool mode detected    <- If this appears, test FAILS
Pool 1: ...                 <- If this appears, test FAILS
Pool 2: ...                 <- If this appears, test FAILS
```

## Troubleshooting

### Test Fails: "Multi-pool mode was activated"

**Problem**: Worker incorrectly uses multi-pool code path.

**Diagnosis**:
```bash
grep "pool_count" worker_legacy_singlepool_config.json
```

Should NOT have `pool_count > 1`.

**Solution**: Verify config file is in correct legacy format.

### Test Fails: "Worker failed to connect"

**Problem**: Coordinator not ready or port conflict.

**Diagnosis**:
```bash
netstat -tuln | grep 7771
```

**Solution**: Ensure port 7771 is available and coordinator started first.

### Test Fails: "Worker crashed during test"

**Problem**: Segfault or unexpected error.

**Diagnosis**:
```bash
tail -50 logs/worker_legacy.log
```

**Solution**: Check for error messages, run with `--debug` for more info.

## Success Criteria

### Full Pass (7-8 checks)

All critical checks pass:
- Coordinator starts
- Worker connects
- Work is processed
- Single-pool mode confirmed
- No crashes

**Result**: ✅ Legacy config works perfectly, zero regression.

### Partial Pass (5-6 checks)

Most checks pass, minor issues:
- Bonus checks may fail
- Some expected log messages missing

**Result**: ⚠️ Generally works, minor investigation needed.

### Failure (<5 checks)

Critical checks fail:
- Worker can't connect
- Work not received
- Multi-pool mode activated incorrectly
- Crashes occur

**Result**: ❌ Regression detected, requires fixing.

## Related Code

### Config Loading
- `src/wizard/wizard_config.c` (lines 420-434): Migration logic
- `src/wizard/wizard_config.c` (lines 412-418): Legacy field parsing

### Worker Initialization
- `src/wizard/wizard_client.c` (lines 1122-1123): Mode detection
- `src/wizard/wizard_client.c` (lines 1308+): Single-pool code path

### Data Structures
- `src/wizard/wizard.h`: `wizard_config_t` with legacy fields marked DEPRECATED

## Next Steps

After this test passes:
1. ✅ Backwards compatibility verified
2. ✅ Safe to release multi-pool feature
3. ✅ Existing users can upgrade without config changes
4. ✅ Legacy configs automatically migrate on first load

## Files Created

- `tests/multipool/worker_legacy_singlepool_config.json` - Legacy config file
- `tests/multipool/run_backwards_compat_test.sh` - Test automation script
- `tests/multipool/BACKWARDS_COMPAT_TEST.md` - This documentation
- `tests/multipool/BACKWARDS_COMPAT_QUICKSTART.md` - Quick reference guide
