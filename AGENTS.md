# Repository Guidelines

## Project Structure & Module Organization
- Core executables live in `keyhunt.cpp`, `keyhunt_legacy.cpp`, and `bsgsd.cpp`; shared utilities sit in `util.*`, `hash/`, `sha3/`, and `secp256k1/`.
- RIPEMD-160 optimizations reside in `hash/ripemd160*.cpp` and legacy fallbacks in `rmd160/`.
- Test vectors and sample assets are under `tests/`; bloom filter data and tables are generated at runtime and stored beside the binaries.

## Build, Test, and Development Commands
- `make keyhunt` – builds the optimized default binary (C++17, LTO, native flags).
- `make keyhunt_legacy` – builds the legacy/GMP variant; triggers `hashing.c` recompilation.
- `make bsgsd` – optional server-mode tool if you need distributed BSGS helpers.
- `make clean` – prunes binaries and object files.
- For local timing/profiling, prefer `/usr/bin/time ./keyhunt …` to capture throughput.

## Coding Style & Naming Conventions
- Keep existing indentation: tabs inside legacy files, mixed tab + spaces in newer sections—match the surrounding style.
- Stick with C++17 for `.cpp` files and C99 for `.c`.
- Prefer descriptive camelCase for functions (`initialize_range_progress_tracker`) and ALL_CAPS for flags/macros.
- Avoid introducing additional dependencies; use the in-tree RIPEMD-160 and SHA implementations before OpenSSL.

## Testing Guidelines
- No automated test harness exists; use the vectors in `tests/` and run targeted commands such as `./keyhunt -m rmd160 -f tests/unsolvedpuzzles.rmd -t 1`.
- When adding hash logic, compare against known-good outputs (e.g., OpenSSL `openssl dgst -rmd160 file`).
- Document any new manual test scenario in `README.md` or `CHANGELOG.md`.

## Commit & Pull Request Guidelines
- Follow concise, imperative commit messages: `Optimize rmd160 fast path`, `Add progress bar flag`.
- Group related changes (code + docs + build files) into a single commit when feasible.
- Pull requests should include:
  - Summary of changes and performance impact (keys/s delta, memory cost).
  - New runtime flags or configuration documented.
  - Confirmation that `make keyhunt` and `make keyhunt_legacy` succeed locally.
- Attach profiling data or screenshots when UI/CLI output changes (e.g., segmented progress indicator).
