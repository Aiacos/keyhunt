# Security Scan Report — 2026-03-08

## Consolidated Findings (3-agent parallel scan + diff review)

### CRITICAL (3 real issues)

| # | Finding | File | Lines | Status |
|---|---------|------|-------|--------|
| C1 | Private key memory not zeroed before free() | io/io.cpp | 86-120, 145-165 | **TODO** |
| C2 | Auth token exposed via environment variable | wizard/wizard_server.c | 241 | **TODO** |
| C3 | Non-atomic counters in bsgsd.cpp (race conditions) | globals.h, bsgsd.cpp | 138-143, 983 | **FIXED** (globals now std::atomic) |

### HIGH (4 issues)

| # | Finding | File | Lines | Status |
|---|---------|------|-------|--------|
| H1 | Signal handler calls non-async-signal-safe functions | gpu/gpu_dispatch.cpp | 350-363 | **TODO** |
| H2 | TLS verification can be disabled (VERIFY_NONE) | distributed/distributed.c | 474 | **TODO** |
| H3 | popen() with URL in wizard_http.c | wizard/wizard_http.c | 67 | **TODO** |
| H4 | popen() in wizard_client.c (gpu_arg not validated) | wizard/wizard_client.c | 818-834 | **TODO** |

### MEDIUM (5 issues)

| # | Finding | File | Lines | Status |
|---|---------|------|-------|--------|
| M1 | TOCTOU temp file race in wizard_http.c | wizard/wizard_http.c | 152-168 | **TODO** |
| M2 | Missing recv() timeout in distributed.c | distributed/distributed.c | 947-968 | **TODO** |
| M3 | Unsafe getenv("HOME") fallback to /tmp | perfdb.c, progress.cpp | 18-24, 52 | **TODO** |
| M4 | Unchecked calloc in Int.cpp (GetBaseN, GetBase2, GetBlockStr, GetC64Str) | secp256k1/Int.cpp | 874,888,934,983 | **TODO** |
| M5 | TOCTOU in progress file check-then-open | progress.cpp | 124-143 | **TODO** |

### LOW (4 issues)

| # | Finding | File | Lines | Status |
|---|---------|------|-------|--------|
| L1 | Unencrypted localhost TCP in wizard_server | wizard/wizard_server.c | 217 | Accept risk |
| L2 | Weak URL char validation (no URL-encoding check) | wizard/wizard_http.c | 24-37 | Accept risk |
| L3 | strcpy in bech32 error paths (safe bounds but bad practice) | io/io.cpp | 97, 101 | Accept risk |
| L4 | g_gpu_multi_workers_local not volatile for signal | gpu/gpu_dispatch.cpp | 346 | Accept risk |

### NEW CONCERN (from diff review)

| # | Finding | File | Status |
|---|---------|------|--------|
| N1 | Possible wrong unlink target in 2nd/3rd bloom cache error paths | mode_bsgs.cpp | **VERIFY** |

## Priority Fix Order
1. C1 — memset key material before free (trivial, high impact)
2. M4 — NULL checks on Int.cpp calloc (trivial)
3. C2 — auth token via fd instead of env var (moderate effort)
4. H1 — signal handler safety (moderate effort)
5. H2-H4 — network/wizard hardening (larger effort)
6. M1-M5 — medium issues (lower priority)

## Diff Security Improvements (11 hardening changes in current diff)
1. getrandom() partial read rejection (keyhunt.cpp)
2. Mutex init error checking (keyhunt.cpp)
3. NULL check on GetBase16() (keyhunt.cpp)
4. Key write fprintf/fclose error handling + stderr fallback (io.cpp)
5. sha256 bounds check for length > 55 (sha256.cpp)
6. Thread creation failure recovery (mode_bsgs.cpp)
7. Corrupted bloom cache cleanup via unlink (mode_bsgs.cpp)
8. Borrow-aware subtraction fix (monitoring.cpp)
9. PRNG fallback warning to stderr (Random.cpp)
10. Consistent MAX_DEVICES derivation (multi_gpu_scheduler.h)
11. processOneVanity error propagation (io.cpp)
