# Backwards Compatibility Test - Quick Start

## TL;DR

Test that old single-pool configs still work after multi-pool feature was added.

```bash
cd tests/multipool
./run_backwards_compat_test.sh
```

**Pass criteria**: 5+ of 8 checks pass, no multi-pool mode activation.

## What This Tests

✅ Legacy config files (no `pool_list`, uses `server` section) still work
✅ Worker uses single-pool code path (NOT multi-pool)
✅ Zero regression for existing users
✅ Automatic migration from legacy format to new format

## Configuration

### Legacy Config (Pre-Multi-Pool)

```json
{
  "server": {
    "host": "127.0.0.1",
    "port": 7771
  }
}
```

**NO** `pool_list` or `pools` array.

### Migration Behavior

When loaded, automatically converted to:

```json
{
  "pool_count": 1,
  "pools": [
    {
      "host": "127.0.0.1",
      "port": 7771,
      "priority": 100,
      "enabled": true
    }
  ]
}
```

**Result**: Single-pool mode (not multi-pool).

## Expected Results

### Test Output

```
Phase 1: Starting Coordinator
    ✓ Coordinator started successfully

Phase 2: Starting Worker (Legacy Config)
    ✓ Worker started successfully

Phase 3: Monitoring Operation
    ✓ Test duration completed

Phase 4: Verification
  ✓ Check 1: Coordinator started successfully
  ✓ Check 2: Worker connected to coordinator
  ✓ Check 3: Legacy config loaded
  ✓ Check 4: Worker received work unit
  ✓ Check 5: Worker processing keys
  ✓ Confirmed: Multi-pool mode NOT activated
  ✓ Check 7: Worker survived entire test
  ! Check 8: Config migration applied (bonus)

Checks Passed: 7/8

✓ BACKWARDS COMPATIBILITY TEST PASSED
```

### Pass vs Fail

| Checks | Result | Meaning |
|--------|--------|---------|
| 7-8 | ✅ **PASS** | Perfect backwards compatibility |
| 5-6 | ⚠️ **PARTIAL** | Minor issues, investigate |
| <5 | ❌ **FAIL** | Regression detected |

## Verification Checklist

Critical checks:

1. ✓ Coordinator started
2. ✓ Worker connected to coordinator (port 7771)
3. ✓ Worker loaded legacy config
4. ✓ Worker received work
5. ✓ Worker processing keys
6. ✓ **Multi-pool mode NOT activated** (critical!)
7. ✓ Worker didn't crash
8. ⚡ Config migration detected (bonus)

## Test Options

```bash
# Standard test (30 seconds)
./run_backwards_compat_test.sh

# Debug mode (verbose output)
./run_backwards_compat_test.sh --debug

# Custom duration
./run_backwards_compat_test.sh --duration 60

# Help
./run_backwards_compat_test.sh --help
```

## Manual Test (Quick)

### Terminal 1: Start Coordinator
```bash
cd tests/multipool
../../keyhunt --wizard --load coordinator1_config.json
```

### Terminal 2: Start Legacy Worker
```bash
cd tests/multipool
../../keyhunt --wizard --load worker_legacy_singlepool_config.json
```

### Verify

Worker output should show:
```
[+] Connecting to coordinator at 127.0.0.1:7771...
[+] Connected successfully
[+] Received work unit
```

Worker output should **NOT** show:
```
[+] Multi-pool mode detected    <- BAD! Should be single-pool
    Pool 1: ...                 <- BAD! Should be single connection
```

## Log Files

After test:
```
logs/coordinator_legacy.log    - Coordinator activity
logs/worker_legacy.log         - Worker activity
logs/backwards_compat_test_summary.txt - Test results
```

## Common Issues

### ❌ "Multi-pool mode was activated"

**Problem**: Worker using multi-pool code path instead of single-pool.

**Check**:
```bash
grep "pool_count" worker_legacy_singlepool_config.json
```

Should NOT have `pool_count > 1` or `pool_list`.

### ❌ "Worker failed to connect"

**Problem**: Coordinator not ready.

**Fix**: Wait 5 seconds after starting coordinator.

### ❌ "Worker crashed"

**Problem**: Bug or incompatibility.

**Debug**:
```bash
./run_backwards_compat_test.sh --debug
tail -50 logs/worker_legacy.log
```

## What to Look For

### Good Signs ✅

- Only ONE coordinator connection (127.0.0.1:7771)
- No "Multi-pool mode" messages
- Normal work processing (keys/s)
- Stable operation (no crashes)

### Bad Signs ❌

- "Multi-pool mode detected" appears
- Multiple pool connections
- Worker crashes
- No work received

## Success Criteria

**Minimum for PASS**: 5 of 8 checks + no multi-pool activation

**Key requirement**: Worker MUST use single-pool code path, not multi-pool.

## Next Steps

### If Test Passes ✅

✅ Backwards compatibility verified
✅ Safe to release multi-pool feature
✅ Existing users can upgrade seamlessly
✅ Legacy configs work without modification

### If Test Fails ❌

1. Check logs: `logs/worker_legacy.log`
2. Verify config format: `worker_legacy_singlepool_config.json`
3. Debug mode: `--debug`
4. Compare with multi-pool test to identify differences

## Documentation

**Full docs**: [BACKWARDS_COMPAT_TEST.md](BACKWARDS_COMPAT_TEST.md)

**Related tests**:
- Multi-pool basic: `./run_test.sh`
- Failover: `./run_failover_test.sh`
- Deconfliction: `./run_deconfliction_test.sh`
- Priority: `./run_priority_test.sh`

## Implementation Reference

**Migration logic**: `src/wizard/wizard_config.c` (lines 420-434)

```c
/* Automatic migration from legacy format */
if (cfg->pool_count == 0 && cfg->server_host[0] != '\0') {
    cfg->pool_count = 1;
    strncpy(cfg->pools[0].host, cfg->server_host, ...);
    cfg->pools[0].priority = 100;
    cfg->pool_failover_enabled = false;
}
```

**Mode detection**: `src/wizard/wizard_client.c` (line 1123)

```c
int use_multipool = (cfg->pool_count > 1);
```

If `pool_count <= 1`: Uses **original single-pool code path** (zero regression).

## Quick Validation

```bash
# Run test
./run_backwards_compat_test.sh

# Check result
echo $?  # 0 = pass, 1 = fail

# View summary
cat logs/backwards_compat_test_summary.txt
```
