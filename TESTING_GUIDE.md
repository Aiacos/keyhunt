# Dynamic Worker Management Testing Guide

## Overview

This guide provides comprehensive instructions for testing the enhanced dynamic worker management features in Keyhunt's distributed mode.

## Features Under Test

1. **Configurable Timeouts** (subtask-2-2)
   - Worker timeout (default: 60s)
   - Work redistribution timeout (default: 60s)
   - Connection timeout (default: 30s)

2. **Worker Graceful Leave** (subtask-2-3) ⭐ CURRENT
   - Graceful worker departure with optional reason
   - Work reassignment to pool or other workers
   - Comprehensive audit logging

3. **Worker Timeout and Redistribution** (subtask-2-4)
   - Automatic worker timeout detection
   - Orphaned work redistribution
   - Health check monitoring

## Test Scripts

### 1. Timeout Configuration Test

```bash
./test_dynamic_workers.sh
```

Tests:
- Default timeout values (60s/60s/30s)
- Custom timeout setter functions
- Timeout enforcement in live coordinator

### 2. Worker Graceful Leave Test ⭐ CURRENT

```bash
./test_worker_graceful_leave.sh
```

Tests:
- Worker graceful departure
- Work reassignment logic
- Audit log messages
- Clean worker removal
- No impact on other workers

**Detailed Documentation:** `TEST_RESULTS_GRACEFUL_LEAVE.md`

## Quick Start

### Prerequisites

```bash
# Build the project
make clean && make

# Verify binary
./keyhunt --help

# Check test files exist
ls tests/1to32.txt
```

### Running Tests

#### Full Guided Test (Recommended)

```bash
# For timeout configuration
./test_dynamic_workers.sh

# For graceful leave
./test_worker_graceful_leave.sh
```

#### Manual Quick Test

**Terminal 1 - Coordinator:**
```bash
./keyhunt -m bsgs -f tests/1to32.txt -b 32 -M server -p 7777
```

**Terminal 2 - Worker 1:**
```bash
./keyhunt -M client -H localhost -p 7777
# Press Ctrl+C to trigger graceful leave
```

**Terminal 3 - Worker 2:**
```bash
./keyhunt -M client -H localhost -p 7777
# Should receive reassigned work
```

## Expected Behavior

### Graceful Leave Sequence

1. **Worker sends leave message:**
   ```json
   {"type":"leave","reason":"Manual test"}
   ```

2. **Coordinator responds:**
   - Logs leave request with worker ID and reason
   - Reassigns any active work to pending pool
   - Sends acknowledgment
   - Logs graceful departure
   - Closes connection cleanly

3. **Other workers continue:**
   - No interruption to worker 2
   - Work reassigned to pool or worker 2
   - No data loss

### Log Messages

**Coordinator (Expected):**
```
[SERVER] Worker #1 (hostname) joined [CPU: 8 cores, GPU: none, Perf: 1234.5]
[SERVER] Worker #2 (hostname) joined [CPU: 8 cores, GPU: none, Perf: 1234.5]
[SERVER] [INFO] Worker #1 (hostname) requesting graceful leave: Manual test
[SERVER] [INFO] Work unit #0 reassigned from worker #1 to pool (reason: graceful leave)
[SERVER] [OK] Worker #1 (hostname) left gracefully
[SERVER] Worker #1 disconnected
```

## Acceptance Criteria

### Subtask 2-2: Timeout Configuration
- [ ] Timeout setter functions compile without errors
- [ ] Default values (60s/60s/30s) are set on init
- [ ] Custom timeout values can be applied via setters
- [ ] Coordinator logs show correct timeout values
- [ ] Worker timeout (60s) triggers disconnection
- [ ] Work timeout (60s) triggers redistribution

### Subtask 2-3: Worker Graceful Leave ⭐
- [ ] Worker can leave without affecting others
- [ ] Leave message logged with ID/hostname/reason
- [ ] Work reassignment logged with reason
- [ ] Leave reason included in log
- [ ] Worker removed cleanly (no errors)
- [ ] No errors or crashes during leave
- [ ] Work not lost (reassigned to pool)

### Subtask 2-4: Worker Timeout (Pending)
- [ ] Worker timeout detected after 60 seconds
- [ ] Orphaned work redistributed within 60 seconds
- [ ] Timeout logged with duration
- [ ] Work reassignment logged
- [ ] Health check runs correctly

## Implementation Details

### Leave Message Handler
Location: `src/distributed/distributed.c:1325-1370`

Key features:
- Thread-safe status updates (mutex locking)
- Work preservation (returns to pending pool)
- Optional reason extraction from JSON
- Three audit log messages (request, reassignment, confirmation)
- Clean state transition (ACTIVE → LEAVING → DISCONNECTED)

### Worker Leave API
Location: `src/distributed/distributed.c:2156-2175`

```c
int dist_worker_leave(dist_worker_client_t *client, const char *reason);
```

Features:
- Sends leave message with optional reason
- Waits for acknowledgment
- Calls dist_worker_disconnect()
- Returns 0 on success, -1 on error

### Worker Status Lifecycle
Location: `src/distributed/distributed.h:87-93`

```c
typedef enum {
    WORKER_STATUS_JOINING = 0,       /* Connecting/registering */
    WORKER_STATUS_ACTIVE,            /* Active, can accept work */
    WORKER_STATUS_LEAVING,           /* Graceful departure */
    WORKER_STATUS_DISCONNECTED       /* Disconnected/timed out */
} worker_status_t;
```

## Troubleshooting

### Worker doesn't send leave message
- Check if Ctrl+C handler calls `dist_worker_leave()`
- Use Python helper script: `python3 /tmp/send_leave_message.py`
- Verify worker is connected before sending leave

### Work not reassigned
- Check coordinator logs for reassignment message
- Verify work unit was in ASSIGNED state
- Check work_units_pending counter increases

### No audit logs visible
- Ensure coordinator verbosity is not silent
- Check LOG_INFO and LOG_OK are not filtered
- Verify coordinator mutex locking is correct

## Test Results Documentation

After running tests, document results in:
- `TEST_RESULTS_GRACEFUL_LEAVE.md` (for subtask 2-3)
- Update "Verified" column in Acceptance Criteria tables
- Take screenshots of coordinator logs
- Note any deviations from expected behavior

## Related Documentation

- **Implementation Plan:** `.auto-claude/specs/019-dynamic-worker-management/implementation_plan.json`
- **Build Progress:** `.auto-claude/specs/019-dynamic-worker-management/build-progress.txt`
- **Spec:** `.auto-claude/specs/019-dynamic-worker-management/spec.md`
- **Code:** `src/distributed/distributed.{h,c}`

## Next Steps

1. ✅ Subtask 2-1: Build verification (COMPLETED)
2. ✅ Subtask 2-2: Timeout configuration test (COMPLETED)
3. ⭐ Subtask 2-3: Worker graceful leave test (CURRENT - READY FOR MANUAL EXECUTION)
4. ⏳ Subtask 2-4: Worker timeout test (PENDING)

---

**Last Updated:** 2026-02-26
**Status:** Ready for manual testing
**Test Coverage:** 602 lines of test code
