# Distributed Protocol Reference

This document describes the TCP+JSON protocol used for keyhunt distributed computing.

## Protocol Overview

| Aspect | Details |
|--------|---------|
| Transport | TCP |
| Port | 2222 (configurable) |
| Format | JSON messages, newline-delimited |
| Encoding | UTF-8 |
| Endianness | Not applicable (text protocol) |

## Message Format

Each message is a JSON object followed by a newline (`\n`):

```json
{"type": "message_type", "field1": "value1", "field2": "value2"}\n
```

## Message Types

### Client -> Server

#### register

Sent when client first connects.

```json
{
  "type": "register",
  "client_name": "worker1",
  "version": "0.2.230731",
  "capabilities": {
    "cpu_cores": 16,
    "cpu_threads": 32,
    "ram_gb": 64,
    "gpu_available": true,
    "gpu_name": "RTX 3080",
    "gpu_memory_gb": 10,
    "simd": ["SSE2", "AVX2", "SHA-NI"],
    "cuda_version": "11.4"
  },
  "config": {
    "threads": 32,
    "gpu_mode": "hybrid",
    "batch_size": 1024
  }
}
```

#### work_request

Request the next work unit.

```json
{
  "type": "work_request",
  "client_name": "worker1"
}
```

#### progress

Report current progress on work unit.

```json
{
  "type": "progress",
  "client_name": "worker1",
  "work_unit_id": 42,
  "keys_checked": 1500000000,
  "speed_mkeys": 85.2,
  "current_position": "0x20002A123456789",
  "percent_complete": 68.5,
  "timestamp": "2025-01-22T10:30:00Z"
}
```

#### work_complete

Report work unit completion.

```json
{
  "type": "work_complete",
  "client_name": "worker1",
  "work_unit_id": 42,
  "keys_checked": 2200000000,
  "duration_seconds": 25.8,
  "avg_speed_mkeys": 85.3
}
```

#### key_found

Report discovered private key.

```json
{
  "type": "key_found",
  "client_name": "worker1",
  "work_unit_id": 42,
  "private_key": "0x2000000000000000000000001234",
  "private_key_decimal": "36893488147419103284",
  "public_key": "02abc123...",
  "address": "13zb1hQbWVsc2S7ZTZnP2G4undNNpdh5so",
  "wif_compressed": "KwDiBf89...",
  "wif_uncompressed": "5HueCGU8...",
  "key_type": "compressed",
  "timestamp": "2025-01-22T10:30:00Z"
}
```

#### heartbeat

Keep connection alive.

```json
{
  "type": "heartbeat",
  "client_name": "worker1",
  "timestamp": "2025-01-22T10:30:00Z"
}
```

#### disconnect

Graceful disconnect notification.

```json
{
  "type": "disconnect",
  "client_name": "worker1",
  "reason": "user_requested",
  "current_work_unit": 42,
  "current_position": "0x20002A123456789"
}
```

### Server -> Client

#### register_ack

Acknowledge client registration.

```json
{
  "type": "register_ack",
  "status": "accepted",
  "server_version": "0.2.230731",
  "puzzle": {
    "number": 66,
    "target_hash": "sha256_of_target_file",
    "search_mode": "address",
    "key_type": "compress"
  },
  "assigned_id": "client_001"
}
```

#### work_unit

Assign work unit to client.

```json
{
  "type": "work_unit",
  "work_unit_id": 43,
  "range_start": "0x20002B00000000000",
  "range_end": "0x20002BFFFFFFFFFFF",
  "search_mode": "address",
  "key_type": "compress",
  "random": false,
  "priority": 1
}
```

#### no_work

No work currently available.

```json
{
  "type": "no_work",
  "reason": "all_assigned",
  "retry_after_seconds": 30
}
```

#### key_found_broadcast

Notify all clients of found key.

```json
{
  "type": "key_found_broadcast",
  "finder": "worker1",
  "private_key": "0x2000000000000000000000001234",
  "address": "13zb1hQbWVsc2S7ZTZnP2G4undNNpdh5so",
  "action": "continue"
}
```

Action can be:
- `continue`: Keep searching for other targets
- `stop`: Stop all clients (single target found)

#### server_shutdown

Server is shutting down.

```json
{
  "type": "server_shutdown",
  "reason": "user_requested",
  "checkpoint_saved": true
}
```

#### error

Error response.

```json
{
  "type": "error",
  "code": "INVALID_MESSAGE",
  "message": "Unknown message type: foo",
  "fatal": false
}
```

Error codes:
| Code | Description | Fatal |
|------|-------------|-------|
| `INVALID_MESSAGE` | Malformed or unknown message | No |
| `INVALID_CLIENT` | Unknown client name | No |
| `WORK_UNIT_NOT_FOUND` | Invalid work unit ID | No |
| `TARGET_MISMATCH` | Client has wrong target file | Yes |
| `VERSION_MISMATCH` | Incompatible protocol version | Yes |

## Connection Lifecycle

```
Client                              Server
  │                                    │
  │──── TCP connect ─────────────────>│
  │                                    │
  │──── register ────────────────────>│
  │<─── register_ack ─────────────────│
  │                                    │
  │──── work_request ────────────────>│
  │<─── work_unit ────────────────────│
  │                                    │
  │     [... searching ...]            │
  │                                    │
  │──── progress ────────────────────>│  (every 10s)
  │                                    │
  │──── heartbeat ───────────────────>│  (every 30s)
  │<─── heartbeat ────────────────────│
  │                                    │
  │──── work_complete ───────────────>│
  │──── work_request ────────────────>│
  │<─── work_unit ────────────────────│
  │                                    │
  │     [... more work ...]            │
  │                                    │
  │──── disconnect ──────────────────>│
  │<─── TCP close ────────────────────│
```

## Timeouts

| Timeout | Duration | Action |
|---------|----------|--------|
| Connection | 30s | Fail connection |
| Heartbeat | 60s | Assume client dead |
| Work unit | 10min | Reassign work unit |
| Progress | 30s | Request status |

## Work Unit States

```
┌─────────┐
│ pending │
└────┬────┘
     │ assign to client
     ▼
┌────────────┐
│ in_progress│
└────┬───────┘
     │
     ├─────────────────────┐
     │ complete            │ timeout/disconnect
     ▼                     ▼
┌───────────┐        ┌─────────┐
│ completed │        │ pending │ (reassigned)
└───────────┘        └─────────┘
```

## Numeric Format

All numbers are sent as:
- **Hexadecimal strings** for large values (ranges, keys): `"0x20000000000000000"`
- **Decimal numbers** for small values (counts, percentages): `85.2`

## Example Session

```
-> {"type":"register","client_name":"worker1","capabilities":{"cpu_cores":16}}
<- {"type":"register_ack","status":"accepted","puzzle":{"number":66}}
-> {"type":"work_request","client_name":"worker1"}
<- {"type":"work_unit","work_unit_id":0,"range_start":"0x20000000000000000","range_end":"0x200000FFFFFFFFFFF"}
-> {"type":"progress","client_name":"worker1","work_unit_id":0,"keys_checked":500000000,"speed_mkeys":83.2}
-> {"type":"work_complete","client_name":"worker1","work_unit_id":0,"keys_checked":1099511627776}
-> {"type":"work_request","client_name":"worker1"}
<- {"type":"work_unit","work_unit_id":1,"range_start":"0x20001000000000000","range_end":"0x200010FFFFFFFFFFF"}
...
```

## Extending the Protocol

When adding new message types:

1. Add `type` field with unique identifier
2. Include `version` field for compatibility
3. Unknown fields are ignored (forward compatibility)
4. Unknown types return `INVALID_MESSAGE` error

## See Also

- [Server Setup](server-setup.md) - Configure coordinator
- [Client Setup](client-setup.md) - Connect workers
- [Troubleshooting](troubleshooting.md) - Common issues
