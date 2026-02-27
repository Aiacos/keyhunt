-- Performance History Database Schema
-- Created: 2026-02-27
-- Purpose: Track historical benchmark performance for trend analysis and regression detection
-- Location: ~/.keyhunt/performance.db

-- Database version metadata
CREATE TABLE IF NOT EXISTS schema_version (
    version INTEGER PRIMARY KEY,
    applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    description TEXT
);

-- Initial schema version
INSERT OR IGNORE INTO schema_version (version, description) VALUES
    (1, 'Initial schema: performance_history table with hardware and benchmark metrics');

-- Main performance history table
-- Stores benchmark results with complete hardware context for trend analysis
CREATE TABLE IF NOT EXISTS performance_history (
    -- Primary key
    id INTEGER PRIMARY KEY AUTOINCREMENT,

    -- Timestamp
    timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    run_date DATE DEFAULT (DATE('now')),  -- For easy grouping by day

    -- Search configuration
    mode TEXT NOT NULL,                    -- 'address', 'bsgs', 'xpoint', 'rmd160', 'vanity', 'pub2rmd'
    bits INTEGER,                          -- Puzzle bit size (if applicable)
    key_type TEXT,                         -- 'compressed', 'uncompressed', 'both'

    -- Performance metrics (Mkeys/s)
    cpu_speed_mkeys REAL NOT NULL,         -- CPU-only speed
    gpu_speed_mkeys REAL DEFAULT 0.0,      -- GPU-only speed (0 if no GPU)
    hybrid_speed_mkeys REAL DEFAULT 0.0,   -- Combined CPU+GPU speed
    efficiency_ratio REAL DEFAULT 1.0,     -- hybrid / (cpu + gpu), measures overhead

    -- CPU hardware context
    cpu_model TEXT NOT NULL,               -- Full CPU model name
    cpu_physical_cores INTEGER NOT NULL,   -- Physical cores (no HT)
    cpu_logical_cores INTEGER NOT NULL,    -- Logical cores (with HT)
    cpu_threads INTEGER NOT NULL,          -- Threads used in benchmark

    -- CPU features (for filtering by capabilities)
    has_avx2 INTEGER DEFAULT 0,            -- 1 if AVX2 available
    has_avx512 INTEGER DEFAULT 0,          -- 1 if any AVX-512 available
    has_avx512f INTEGER DEFAULT 0,         -- AVX-512 Foundation
    has_avx512dq INTEGER DEFAULT 0,        -- AVX-512 DQ (important for ripemd160)
    has_sha_ni INTEGER DEFAULT 0,          -- Intel SHA extensions

    -- Memory context
    ram_total_mb INTEGER,                  -- Total system RAM
    ram_available_mb INTEGER,              -- Available RAM at benchmark time

    -- Cache info (KB)
    cache_l1_kb INTEGER,
    cache_l2_kb INTEGER,
    cache_l3_kb INTEGER,

    -- GPU hardware context (NULL if no GPU)
    gpu_count INTEGER DEFAULT 0,
    gpu_name TEXT,                         -- GPU model name
    gpu_vram_mb INTEGER,                   -- GPU VRAM
    gpu_sm_count INTEGER,                  -- Streaming Multiprocessors
    gpu_compute_capability INTEGER,        -- e.g., 75 for sm_75, 89 for Ada

    -- Hardware fingerprint (for grouping identical configurations)
    hardware_hash TEXT NOT NULL,           -- SHA256 of (cpu_model + features + gpu_name)

    -- Performance scores (computed)
    cpu_score REAL,                        -- Relative CPU performance score
    gpu_score REAL,                        -- Relative GPU performance score

    -- Metadata
    keyhunt_version TEXT,                  -- Version of keyhunt used
    benchmark_duration_seconds INTEGER,    -- How long the benchmark ran
    notes TEXT                             -- Optional user notes
);

-- Indexes for efficient querying

-- Index for time-series queries (most common: "show recent performance")
CREATE INDEX IF NOT EXISTS idx_performance_timestamp
    ON performance_history(timestamp DESC);

-- Index for trend analysis by date
CREATE INDEX IF NOT EXISTS idx_performance_date
    ON performance_history(run_date DESC);

-- Index for filtering by mode (e.g., "show all BSGS benchmarks")
CREATE INDEX IF NOT EXISTS idx_performance_mode
    ON performance_history(mode);

-- Index for hardware-specific queries (e.g., "all results for this CPU")
CREATE INDEX IF NOT EXISTS idx_performance_hardware
    ON performance_history(hardware_hash);

-- Composite index for mode + hardware (e.g., "BSGS performance on this hardware over time")
CREATE INDEX IF NOT EXISTS idx_performance_mode_hardware
    ON performance_history(mode, hardware_hash, timestamp DESC);

-- Index for regression detection (recent results first)
CREATE INDEX IF NOT EXISTS idx_performance_recent
    ON performance_history(mode, timestamp DESC);

-- Index for GPU-enabled results (for GPU performance analysis)
CREATE INDEX IF NOT EXISTS idx_performance_gpu
    ON performance_history(gpu_count, timestamp DESC)
    WHERE gpu_count > 0;

-- View: Latest benchmark for each mode
CREATE VIEW IF NOT EXISTS v_latest_benchmarks AS
SELECT
    mode,
    MAX(timestamp) as latest_timestamp,
    cpu_speed_mkeys,
    gpu_speed_mkeys,
    hybrid_speed_mkeys,
    cpu_model,
    gpu_name
FROM performance_history
GROUP BY mode, hardware_hash;

-- View: Performance trends (daily averages for last 30 days)
CREATE VIEW IF NOT EXISTS v_performance_trends AS
SELECT
    run_date,
    mode,
    hardware_hash,
    AVG(cpu_speed_mkeys) as avg_cpu_speed,
    AVG(gpu_speed_mkeys) as avg_gpu_speed,
    AVG(hybrid_speed_mkeys) as avg_hybrid_speed,
    COUNT(*) as run_count
FROM performance_history
WHERE run_date >= DATE('now', '-30 days')
GROUP BY run_date, mode, hardware_hash
ORDER BY run_date DESC;

-- View: Hardware summary (unique configurations)
CREATE VIEW IF NOT EXISTS v_hardware_summary AS
SELECT
    hardware_hash,
    cpu_model,
    cpu_physical_cores,
    cpu_logical_cores,
    has_avx2,
    has_avx512,
    has_sha_ni,
    gpu_name,
    gpu_count,
    COUNT(*) as benchmark_count,
    MAX(timestamp) as last_used
FROM performance_history
GROUP BY hardware_hash
ORDER BY last_used DESC;

-- View: Regression detection (compare recent vs historical average)
CREATE VIEW IF NOT EXISTS v_regression_check AS
SELECT
    h1.mode,
    h1.hardware_hash,
    h1.timestamp as recent_timestamp,
    h1.cpu_speed_mkeys as recent_cpu_speed,
    AVG(h2.cpu_speed_mkeys) as historical_avg_cpu_speed,
    (h1.cpu_speed_mkeys - AVG(h2.cpu_speed_mkeys)) / AVG(h2.cpu_speed_mkeys) * 100 as cpu_change_percent,
    h1.gpu_speed_mkeys as recent_gpu_speed,
    AVG(h2.gpu_speed_mkeys) as historical_avg_gpu_speed,
    CASE
        WHEN AVG(h2.gpu_speed_mkeys) > 0
        THEN (h1.gpu_speed_mkeys - AVG(h2.gpu_speed_mkeys)) / AVG(h2.gpu_speed_mkeys) * 100
        ELSE 0
    END as gpu_change_percent
FROM performance_history h1
JOIN performance_history h2
    ON h1.mode = h2.mode
    AND h1.hardware_hash = h2.hardware_hash
    AND h2.timestamp < h1.timestamp
    AND h2.timestamp >= DATE(h1.timestamp, '-30 days')
WHERE h1.timestamp IN (
    SELECT MAX(timestamp)
    FROM performance_history
    GROUP BY mode, hardware_hash
)
GROUP BY h1.id, h1.mode, h1.hardware_hash;

-- Community benchmarks table (for optional anonymous submissions)
-- This is a local staging area before manual upload
CREATE TABLE IF NOT EXISTS community_submissions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    submission_timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

    -- Anonymized hardware info (hashed, not exact model names)
    cpu_family_hash TEXT NOT NULL,         -- Hash of CPU family (e.g., "Intel_Xeon_E5" -> hash)
    cpu_cores_category TEXT NOT NULL,      -- e.g., "4-8", "8-16", "16-32"
    has_avx2 INTEGER,
    has_avx512 INTEGER,

    gpu_family_hash TEXT,                  -- Hash of GPU family (e.g., "RTX_3000" -> hash)
    gpu_vram_category TEXT,                -- e.g., "8GB", "12GB", "24GB"

    -- Performance data (not anonymized, this is the valuable part)
    mode TEXT NOT NULL,
    cpu_speed_mkeys REAL NOT NULL,
    gpu_speed_mkeys REAL,
    hybrid_speed_mkeys REAL,

    -- Privacy flags
    user_consented INTEGER DEFAULT 0,      -- User must explicitly consent
    uploaded INTEGER DEFAULT 0,            -- Track if submitted to community DB

    -- Reference to original benchmark
    original_benchmark_id INTEGER,
    FOREIGN KEY (original_benchmark_id) REFERENCES performance_history(id)
);

-- Index for community data aggregation
CREATE INDEX IF NOT EXISTS idx_community_cpu_family
    ON community_submissions(cpu_family_hash, mode);

CREATE INDEX IF NOT EXISTS idx_community_gpu_family
    ON community_submissions(gpu_family_hash, mode);
