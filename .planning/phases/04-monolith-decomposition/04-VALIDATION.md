---
phase: 4
slug: monolith-decomposition
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-03-06
---

# Phase 4 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Custom test_framework.h + functional smoke tests |
| **Config file** | Makefile test targets + .clang-tidy (Wave 0) |
| **Quick run command** | `make clean all && ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF` |
| **Full suite command** | `make clean all && bash tests/test_e2e_modes.sh` |
| **Estimated runtime** | ~45 seconds |

---

## Sampling Rate

- **After every task commit:** Run `make clean all && ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF`
- **After every plan wave:** Run `make clean all && bash tests/test_e2e_modes.sh`
- **Before `/gsd:verify-work`:** Full suite must be green + clang-tidy + cppcheck + `wc -l` check
- **Max feedback latency:** 60 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 04-01-01 | 01 | 0 | STR-06 | config | `test -f .clang-tidy && echo OK` | W0 | pending |
| 04-01-02 | 01 | 0 | STR-06 | build | `bear -- make clean all && test -f compile_commands.json` | W0 | pending |
| 04-01-03 | 01 | 0 | STR-07 | config | `test -f .cppcheck-suppress && echo OK` | W0 | pending |
| 04-02-01 | 02 | 1 | STR-05 | smoke | `make clean all && ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF` | N/A | pending |
| 04-02-02 | 02 | 1 | STR-01 | smoke | `./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF` | N/A | pending |
| 04-03-01 | 03 | 1 | STR-03 | smoke | `./keyhunt -m xpoint -f tests/120.txt -t 4 -b 125 -R -q` | N/A | pending |
| 04-03-02 | 03 | 1 | STR-03 | smoke | `./keyhunt -m rmd160 -f tests/66.rmd -b 66 -l compress -R -q` | N/A | pending |
| 04-04-01 | 04 | 2 | STR-04 | smoke | `./keyhunt -m vanity -f tests/vanity.txt -r 1:FFFFFFFF -s 5` | N/A | pending |
| 04-04-02 | 04 | 2 | STR-04 | smoke | `./keyhunt -m address -f tests/minikeys.txt -r 1:FFFFFFFF` | N/A | pending |
| 04-05-01 | 05 | 3 | STR-02 | smoke | `./keyhunt -m bsgs -f tests/125.txt -b 125 -q -s 10 -R` | N/A | pending |
| 04-06-01 | 06 | 4 | STR-05 | metric | `wc -l src/keyhunt.cpp` (<= 600) | N/A | pending |
| 04-06-02 | 06 | 4 | STR-06 | static | `clang-tidy -p . src/modes/*.cpp` | .clang-tidy | pending |
| 04-06-03 | 06 | 4 | STR-07 | static | `cppcheck --enable=warning --error-exitcode=1 -Isrc src/` | N/A | pending |
| 04-06-04 | 06 | 4 | STR-08 | build | `make CXXFLAGS+="-D_FORTIFY_SOURCE=3 -fstack-protector-strong -fcf-protection"` | N/A | pending |

*Status: pending / green / red / flaky*

---

## Wave 0 Requirements

- [ ] `.clang-tidy` — config file with check selection (bugprone-*, clang-analyzer-security.*) and suppressions for vendored code
- [ ] `compile_commands.json` — via `bear -- make clean all`
- [ ] `.cppcheck-suppress` — suppressions for vendored secp256k1/Int.h dangerousTypeCast
- [ ] `src/modes/` directory — does not exist yet, created in Wave 1

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Mode files compile in isolation | STR-05 | Requires targeted compilation per file | `g++ -c -std=c++17 -Isrc src/modes/mode_address.cpp -o /dev/null` for each mode file |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 60s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
