# TODO Fix Plan — COMPLETED

## CRITICAL (5 items) — ALL RESOLVED
| # | File | Line | Issue | Status |
|---|------|------|-------|--------|
| 1 | ripemd160_avx512.cpp | 92 | Wrong f4 ternarylogic imm8 (0xE2→0xE4) | RESOLVED |
| 2 | ripemd160_avx512.cpp | 98 | Wrong f5 ternarylogic imm8 (0x36→0x2D) | RESOLVED |
| 3 | wizard_client.c | 623 | Shell injection via popen() | RESOLVED |
| 4 | distributed.c | 2229 | Auth token in plaintext state file | RESOLVED |
| 5 | gpu_backend_cuda.cu | 2064,2483 | Missing cudaGetLastError() after kernel launch | RESOLVED |

## HIGH (10 items) — ALL RESOLVED
| # | File | Line | Issue | Status |
|---|------|------|-------|--------|
| 6 | keyhunt.cpp | 562 | THREADOUTPUT data race (plain int, no atomic) | RESOLVED |
| 7 | search_address.cpp | 558 | IntGroup memory leak | RESOLVED |
| 8 | search_bsgs_threads.cpp | 258 | exit(EXIT_FAILURE) should be EXIT_SUCCESS + memory leaks | RESOLVED |
| 9 | bloom.h | 276 | Missing static_assert on bloom_legacy_header | RESOLVED |
| 10 | sha256.h | 27 | In-place padding API undocumented | RESOLVED (added API docs) |
| 11 | Int.cpp | 228 | Unaligned uint64_t cast UB | RESOLVED |
| 12 | search_context.h | 112 | 157 extern declarations coupling | RESOLVED (removed 63 dead externs, reorganized) |
| 13 | wizard_community.c | 819 | 128-bit to double precision loss | RESOLVED |
| 14 | distributed.c | 285 | JSON parser strstr substring matching | RESOLVED |
| 15 | distributed.c | 1207 | send_msg_ex return value ignored | RESOLVED |
| 16 | distributed.c | 1770 | No connect timeout | RESOLVED |

## MEDIUM (4 items) — ALL RESOLVED
| # | File | Line | Issue | Status |
|---|------|------|-------|--------|
| 17 | keyhunt.cpp | 163 | Shadow config via undocumented env vars | RESOLVED (env var registry + docs/ENV_VARIABLES.md) |
| 18 | search_context.h | 47 | CPU_GRP_SIZE defined 5 times | RESOLVED (reduced to 3: canonical + 2 standalone monoliths) |
| 19 | search_address.cpp | 126 | sub_u64_if_fits duplicated | RESOLVED (consolidated to int_sub_to_u64 in search_utils.h) |
| 20 | search_minikeys.cpp | 108 | profile_set_thread 4 conflicting defs | RESOLVED (replaced no-op stub with extern decl) |

## Summary
- **Total TODOs**: 20
- **Resolved**: 20 (100%)
- **Session**: 2026-02-26
