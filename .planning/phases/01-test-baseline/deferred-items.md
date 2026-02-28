# Deferred Items - Phase 01 Test Baseline

## Pre-existing Issues (Out of Scope)

### 1. Test runner linker error: undefined reference to rmd160_check_* functions
- **Found during:** Plan 01-03, Task 2 verification
- **Origin:** Plan 01-02 added `test_search_rmd160.cpp` tests that reference `rmd160_check_single`, `rmd160_check_compressed_simple`, `rmd160_check_uncompressed_simple`, `rmd160_check_uncompressed_endomorphism` -- functions not included in TEST_SHARED_OBJS
- **Impact:** `make test` fails on clean build; `make test-all` fails because it depends on `make test`
- **Fix needed:** Add `search_rmd160.o` to TEST_SHARED_OBJS in Makefile, or add a mock/stub object
- **Workaround:** E2E tests (`make test-e2e`) work independently of unit test build
