# External Integrations

**Analysis Date:** 2026-02-28

## APIs & External Services

**Community Puzzle Databases:**
- BTCPuzzle.info - Puzzle definitions and scanned ranges
  - Module: `src/wizard/wizard_community.c`
  - Function: `wizard_community_fetch_puzzles()`
  - Protocol: HTTPS via curl
  - Response format: HTML scraped or JSON API
  - Cache: 24-hour TTL stored in `~/.keyhunt/puzzles_cache.txt`
  - Timeout: 30 seconds
  - Purpose: Download puzzle metadata and track community progress

- Privatekeys.pw Cloud Search - Community scanning progress
  - Module: `src/wizard/wizard_community.c`
  - Function: `wizard_community_check_progress()`
  - Protocol: HTTPS via curl
  - Endpoint: `https://privatekeys.pw/cloud-search`
  - Cache: 24-hour cache in `~/.keyhunt/privatekeys_progress.json`
  - Cache Format:
    ```json
    {
      "puzzle_number": 71,
      "percent_scanned": 0.022477,
      "keys_scanned": 1234567890,
      "fetch_time": 1706000000
    }
    ```
  - Purpose: Avoid redundant searches by calculating offsets for already-scanned ranges

**HTTP Client:**
- Module: `src/wizard/wizard_http.h`, `src/wizard/wizard_http.c`
- Implementation: Shell `curl` command via `popen()` (no libcurl dependency)
- API Functions:
  - `wizard_http_get()` - GET request with 30-second timeout
  - `wizard_http_post()` - POST with custom Content-Type
  - `wizard_http_post_json()` - Convenience wrapper for JSON
  - `wizard_http_json_escape()` - JSON string escaping
- Features:
  - Maximum response size: 10 MB
  - User-Agent: Chrome 120.0 on Linux
  - Shell injection prevention (URL validation)
  - Response buffering with dynamic allocation
- Timeout: `WIZARD_HTTP_TIMEOUT` = 30 seconds
- Max response: `WIZARD_HTTP_MAX_RESPONSE_SIZE` = 10 MB

## Data Storage

**Databases:**
- SQLite3 (embedded)
  - Provider: SQLite3.x (included as `src/database/sqlite3.c` - amalgamation)
  - Location: `~/.keyhunt/performance.db`
  - Client: Custom C API via `src/database/perfdb.h`
  - Schema: `src/database/schema.sql`
  - Purpose: Performance history, trend analysis, hardware fingerprinting
  - Tables:
    - `performance_history` - Benchmark results with hardware context
    - `community_submissions` - Anonymized performance submissions (staging)
    - `schema_version` - Database versioning
  - Views:
    - `v_latest_benchmarks` - Most recent result per mode
    - `v_performance_trends` - 30-day rolling averages
    - `v_hardware_summary` - Unique hardware configurations
    - `v_regression_check` - Performance regression detection

**File Storage:**
- Distributed state: JSON files in project root
  - `coordinator_state_*.json` - Coordinator work distribution state
  - `keyhunt_wizard.json` - Wizard configuration (persistent session)
  - Example: `coordinator_state_puzzle71.json`
- Progress tracking: `~/.keyhunt/progress/` (JSON per puzzle)
- Cache: `~/.keyhunt/puzzles_cache.txt`, `~/.keyhunt/privatekeys_progress.json`
- Bloom filter caching: `keyhunt_bsgs_*.blm`, `keyhunt_bsgs_*.tbl` (binary)

**Caching:**
- File-based caching (no external cache service)
- Cache control: 24-hour TTL for community data
- Cache validation: Fetch time tracked in JSON cache files
- Automatic invalidation: Older than 24 hours re-fetched on next query

## Authentication & Identity

**Auth Provider:**
- Custom internal token-based authentication
  - Environment variable: `KEYHUNT_AUTH_TOKEN` (wizard internal use only)
  - Purpose: Subprocess worker authentication in wizard server mode
  - Token scope: Limited to session, not persistent

**Distributed Coordinator Auth:**
- Simple token-based authentication (optional)
  - Field: `auth_token` in distributed work messages
  - Max length: `DIST_AUTH_TOKEN_MAX` = 64 bytes
  - Use case: Cluster coordinator/worker validation
  - Implementation: `src/distributed/distributed.h`
  - No public key infrastructure - shared secret per deployment

**TLS/SSL (Optional):**
- Provider: OpenSSL 1.1+
- Enabled with: `make ENABLE_TLS=1`
- Usage: Distributed coordinator encrypted communication
  - TLS server context: `src/distributed/distributed.h`
  - Functions: `dist_tls_init()`, `dist_tls_accept()` (stub if disabled)
  - Certificates: Self-signed, per-deployment (not embedded)
  - Purpose: Encrypt coordinator-to-worker communication over network

## Monitoring & Observability

**Error Tracking:**
- Custom error reporting system - `src/error/enhanced_error.h`
- No external error tracking (Sentry, Datadog, etc.)
- Error categories: Memory, IO, GPU, Validation, Security
- Error severity: NOTICE, WARNING, ERROR, CRITICAL
- Output: stderr with color coding

**Logs:**
- Approach: stderr and stdout logging
  - Output module: `src/output.h`, `src/output.cpp`
  - Verbosity levels:
    - `OUTPUT_SILENT` - No output except errors
    - `OUTPUT_MINIMAL` - Progress only
    - `OUTPUT_NORMAL` - Standard logging (default)
    - `OUTPUT_VERBOSE` - Debug output
  - Color-coded output (platform-aware)
- Performance profiling: Lightweight internal profiler in `src/search/search_utils.h`
  - Optional: Enabled via `KEYHUNT_PROFILE=1` environment variable
  - Tracks: EC operations, hashing, bloom checks, binary search, file I/O

**Diagnostics:**
- Module: `src/diagnostics/diagnostics.h`
- GPU diagnostics: Device enumeration, memory availability
- Hardware analysis: CPU feature detection, RAM profiling
- No external telemetry or phone-home

## CI/CD & Deployment

**Hosting:**
- GitHub (source repository)
- GitHub Actions - CI/CD pipeline (`.github/workflows/`)
- Container registries: Optional (no official Docker image)

**CI Pipeline:**
- GitHub Actions:
  - Build matrix: Linux GCC/Clang, Windows MinGW, macOS
  - Test coverage: codecov.io integration (`codecov.yml`)
  - Sanitizers: AddressSanitizer, ThreadSanitizer
  - Compiler checks: Warning-as-error validation

**Deployment:**
- Binary distribution: Release artifacts on GitHub
- Package managers: Manual (no apt, brew, or yum packages)
- Containerization: Users build their own Docker images (no pre-built)
- Multi-platform: Executables for Windows (MSVC/MinGW), Linux (glibc x86_64), macOS

## Environment Configuration

**Required env vars (optional, all have defaults):**
- `KEYHUNT_PROFILE=1` - Enable internal profiler
- `KEYHUNT_SKIP_SYSINFO=1` - Skip hardware detection
- `KEYHUNT_GPU_SELFTEST=1` - Test GPU on startup
- `KEYHUNT_HYBRID_GPU_PERCENT=80` - GPU allocation in hybrid mode
- `KEYHUNT_HYBRID_WORK_STEAL=1` - Enable work-stealing scheduler
- `KEYHUNT_HYBRID_BLOCK_SIZE=0x100000000` - Block size for stealing
- `KEYHUNT_CPU_USE_Y=0` - Skip Y computation (optimization)
- `KEYHUNT_AUTH_TOKEN=<token>` - Wizard subprocess authentication (internal)
- `KEYHUNT_DEBUG=1` - Distributed worker debug logging
- `KEYHUNT_DEBUG_SUBPROCESS=1` - Subprocess line reader debug

**Secrets location:**
- No secrets stored in codebase
- TLS certificates: User-provisioned, runtime supplied
- API keys: Not used (public APIs or local only)
- Passwords: Not applicable (no user authentication)

**System Requirements (Environment):**
- CPU: x86_64 with SSSE3 minimum (AVX2 highly recommended)
- RAM: 1GB minimum, 8GB+ for BSGS mode
- Storage: SSD for bloom filter caching, ~100MB for SQLite history per year
- Network: Required only for wizard mode (curl must be available on PATH)
- Graphics: Optional NVIDIA CUDA or AMD ROCm for GPU acceleration

## Webhooks & Callbacks

**Incoming:**
- Distributed worker callback - `src/distributed/distributed.h`
  - Protocol: TCP with JSON messages
  - Listener: Coordinator on port `DIST_DEFAULT_PORT` = 7777
  - Message types: `WORKER_REGISTER`, `WORK_COMPLETE`, `HEARTBEAT`
  - Security: Optional TLS, optional token auth

**Outgoing:**
- Webhook support (proposed) - `src/wizard/wizard_webhooks.h`
  - Module: `src/wizard/wizard_webhooks.c`
  - Purpose: Notify external systems of key findings
  - Implementation: Stub functions present but not integrated
  - Format: JSON POST to configurable webhooks
  - Status: Infrastructure present, feature incomplete

**Network Protocols:**
- TCP (Distributed mode)
  - Port: 7777 (configurable)
  - Message format: JSON over raw TCP
  - Max message size: `DIST_MAX_MSG_SIZE` = 8192 bytes
  - Worker capacity: `DIST_MAX_WORKERS` = 64
  - Federation: `DIST_MAX_FEDERATION` = 8 coordinators

- HTTP/HTTPS (Community integration)
  - Via curl command (shell)
  - Timeout: 30 seconds
  - User-Agent: Mozilla/5.0 Chrome 120.0
  - Connection pooling: Per-curl-invocation (stateless)

**Rate Limiting (Distributed):**
- Connection rate limit: `DIST_RATE_LIMIT_MAX_CONNECTIONS` = 10 per IP per 60 seconds
- Message rate limit: `DIST_RATE_LIMIT_MAX_MESSAGES` = 100 per connection per 60 seconds
- Window: `DIST_RATE_LIMIT_WINDOW_SEC` = 60 seconds

## Third-Party Components

**Bundled Libraries:**
- SQLite3 (public domain) - `src/database/sqlite3.c/h`
- xxHash (2-Clause BSD) - `src/xxhash/xxhash.c/h`
- Base58 (public domain) - `src/base58/base58.c`
- Bech32 (MIT) - `src/bech32/bech32.c`

**No External Binary Dependencies:**
- No requirement to install crypto libraries (custom secp256k1)
- No requirement to install hash libraries (custom RIPEMD160, SHA256)
- Optional: libssl/libcrypto for TLS only (build-time flag)
- Optional: CUDA toolkit for NVIDIA GPU only
- Optional: ROCm for AMD GPU only

---

*Integration audit: 2026-02-28*
