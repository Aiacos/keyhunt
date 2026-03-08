# Session TODOs — 2026-03-08

## From Security Scan

| # | Priority | Description | File | Status |
|---|----------|-------------|------|--------|
| T1 | CRITICAL | Zero key material (hextemp, hexrmd) before free() | io/io.cpp:127,183 | TODO |
| T2 | HIGH | Auth token via fd instead of env var | wizard/wizard_server.c:241 | TODO |
| T3 | HIGH | Signal handler calls non-async-signal-safe functions | gpu/gpu_dispatch.cpp:350-363 | TODO |
| T4 | MEDIUM | Unchecked calloc in Int.cpp (4 locations) | secp256k1/Int.cpp:874,888,934,983 | TODO |
| T5 | MEDIUM | Missing recv() timeout in distributed.c | distributed/distributed.c:947-968 | TODO |

## From Predictive Analysis

| # | Priority | Description | File | Status |
|---|----------|-------------|------|--------|
| T6 | LOW | Replace sprintf with snprintf in sha256.cpp and Int.cpp | sha256.cpp:507, Int.cpp:878+ | TODO |
| T7 | LOW | mode_bsgs.cpp at 1684 lines — candidate for bloom cache I/O extraction | modes/mode_bsgs.cpp | BACKLOG |

## From Code Review (pending agent results)

_To be updated when review agents complete._

## Verified Non-Issues

| # | Description | Reason |
|---|-------------|--------|
| N1 | Unlink wrong bloom file in mode_bsgs.cpp | FALSE POSITIVE — buffer_bloom_file re-assigned before each fopen/unlink pair |
| P2 | Integer overflow on malloc in io.cpp | ALREADY FIXED — SIZE_MAX check present on lines 512, 630, 730 |
