# Version Migration Guide

## Overview

This guide helps you upgrade keyhunt from older versions to the latest release. Each major version section documents breaking changes, new features, and step-by-step migration instructions.

---

## Version 0.3.0 - Distributed Mode (Current)

### What's New

Version 0.3.0 introduces distributed computing capabilities and intelligent automation:

#### 1. **Distributed Computing**
- **Coordinator/Worker Architecture**: Run one server to coordinate multiple worker machines
- **TCP Protocol**: JSON-based communication on port 7777 (configurable)
- **Hardware Reporting**: Workers automatically report their CPU/GPU capabilities
- **Heartbeat System**: Server monitors worker health and progress
- **Key Found Notification**: Immediate distributed notification when a key is found

#### 2. **Interactive Wizard** (`--wizard` or `-W`)
- **5-Step Setup**: Puzzle selection → Mode → Server/Client config → Search config → Community sync
- **Auto-Configuration**: Detects hardware and calculates optimal parameters automatically
- **Session Resume**: Saves configuration to `keyhunt_wizard.json` for easy restart
- **Dual Mode**: Run as server (coordinator + worker) or client (worker only)

#### 3. **Community Progress Integration**
- **BTCPuzzle.info**: Downloads latest puzzle database and scanned ranges
- **Privatekeys.pw Cloud Search**: Fetches community scanning progress (24-hour cache)
- **Smart Exclusions**: Automatically skips already-scanned ranges
- **Periodic Sync**: Updates community progress hourly

#### 4. **Hardware Auto-Detection**
- **CPU Detection**: Cores, threads, model name, cache sizes (L1/L2/L3)
- **SIMD Features**: AVX2, AVX-512 (F/DQ/BW/VL), SHA-NI
- **GPU Detection**: NVIDIA via NVML (name, VRAM, compute capability)
- **Memory Detection**: Total, available, and free RAM
- **Auto-Tuning**: Optimal threads, batch size, N/K values calculated automatically

#### 5. **Parameter Validation System**
- **Input Validation**: Checks threads, N/K values, batch sizes against hardware
- **Auto-Correction**: Prevents OOM crashes and excessive thread counts
- **Visual Feedback**: ✓ optimal, i suboptimal, ! corrected, ⚠ warning
- **Safety Guarantees**: No more out-of-memory or resource exhaustion crashes

#### 6. **GPU Performance Optimizations**
- **GPU Autotune**: Runtime kernel parameter optimization
- **Multi-GPU Scheduler**: Adaptive work distribution across multiple GPUs
- **Async Pipeline**: Triple-buffered operations for maximum GPU utilization
- **Memory Pool**: Fast arena-style allocation with cache-line alignment

---

### Breaking Changes

#### 1. **Thread Count Auto-Detection**

**OLD BEHAVIOR (0.2.x):**
- Default thread count was 1
- Required manual `-t` flag for multi-threading

**NEW BEHAVIOR (0.3.0):**
- Auto-detects all logical cores
- Uses all available CPU threads by default
- `-t` flag is now optional

**Migration:**
```bash
# OLD (0.2.x) - Manual thread specification
./keyhunt -m address -f tests/66.txt -b 66 -R -t 8

# NEW (0.3.0) - Auto-detected (uses all cores)
./keyhunt -m address -f tests/66.txt -b 66 -R

# NEW (0.3.0) - Manual override still supported
./keyhunt -m address -f tests/66.txt -b 66 -R -t 8
```

**Action Required:**
- ✅ **None** - Old commands still work
- 💡 **Recommended**: Remove `-t` flag to use auto-tuning

---

#### 2. **BSGS Memory Calculation Changes**

**OLD BEHAVIOR (0.2.x):**
- N and K values could exceed available RAM
- Would crash with OOM error
- No validation before allocation

**NEW BEHAVIOR (0.3.0):**
- Validates N/K against available RAM
- Auto-corrects dangerous values
- Prints warnings and recommendations

**Migration:**
```bash
# OLD (0.2.x) - Could crash if RAM insufficient
./keyhunt -m bsgs -f pubkeys.txt -n 0x100000000 -k 8192

# NEW (0.3.0) - Auto-corrects and warns
./keyhunt -m bsgs -f pubkeys.txt -n 0x100000000 -k 8192
# Output: [!] Auto-corrected to N=0x10000000 and K=2048 (fits in 8GB RAM)

# NEW (0.3.0) - Omit N/K for auto-tuning
./keyhunt -m bsgs -f pubkeys.txt
# Output: [✓] Auto-tuned: N=0x20000000, K=1 (optimal for 16GB RAM)
```

**Action Required:**
- ✅ **None** - Auto-correction prevents crashes
- 💡 **Recommended**: Omit `-n` and `-k` flags to use optimal auto-tuned values

---

#### 3. **New Wizard Mode Flags**

**NEW FLAGS (0.3.0):**
- `--wizard` or `-W`: Launch interactive setup wizard
- `--server`: Start distributed mode server (coordinator)
- `--client <host:port>`: Connect as distributed mode worker

**Examples:**
```bash
# Interactive wizard for easy setup
./keyhunt --wizard

# Start as distributed server
./keyhunt --server --port 7777

# Connect as distributed worker
./keyhunt --client 192.168.1.100:7777
```

**Action Required:**
- ✅ **None** - Old command-line syntax unchanged
- 💡 **New**: Use `--wizard` for first-time or complex setups

---

### Configuration File Changes

#### New Configuration File: `keyhunt_wizard.json`

**Created by**: `./keyhunt --wizard`

**Purpose**: Saves wizard-generated configuration for session resume

**Location**: Current directory (where keyhunt is run)

**Format**:
```json
{
  "version": 1,
  "puzzle": {
    "number": 66,
    "target_address": "13zb1hQbWVsc2S7ZTZnP2G4undNNpdh5so",
    "range_start": "20000000000000000",
    "range_end": "3ffffffffffffffff",
    "bits": 66
  },
  "server": {
    "host": "0.0.0.0",
    "port": 7777,
    "work_unit_size": "100000000",
    "checkpoint_interval": 300,
    "also_worker": true
  },
  "search": {
    "mode": "address",
    "key_type": "compress",
    "random_mode": true,
    "threads": 16,
    "gpu_percent": 95
  },
  "community": {
    "enabled": true,
    "source": "btcpuzzle.info",
    "sync_interval": 3600
  }
}
```

**Migration:**
- ✅ **None** - This is a new file format (doesn't affect existing usage)
- 💡 **New**: Run wizard to generate this file for easy distributed setup

---

#### New Cache Files

**0.3.0 introduces several cache files:**

| File | Purpose | Location | Lifespan |
|------|---------|----------|----------|
| `puzzles_cache.txt` | Puzzle database cache | Current directory | Updated on wizard run |
| `~/.keyhunt/privatekeys_progress.json` | Community progress cache | User home directory | 24 hours |
| `wizard_progress.dat` | Wizard progress state | Current directory | Session lifetime |
| `wizard_excluded.dat` | Excluded ranges list | Current directory | Persistent |

**Migration:**
- ✅ **None** - Cache files are created automatically
- 💡 **Cleanup**: These files can be safely deleted if disk space is needed

---

### Command-Line Migration Examples

#### Example 1: Basic Address Search

```bash
# 0.2.x syntax
./keyhunt -m address -f tests/66.txt -b 66 -R -t 8

# 0.3.0 equivalent (auto-tuned)
./keyhunt -m address -f tests/66.txt -b 66 -R

# 0.3.0 with parameter validation
./keyhunt -m address -f tests/66.txt -b 66 -R
# Output: [✓] Threads: Using auto-tuned value: 16 threads
```

---

#### Example 2: BSGS Mode

```bash
# 0.2.x syntax (manual N/K)
./keyhunt -m bsgs -f pubkeys.txt -n 0x8000000 -k 1024 -t 4

# 0.3.0 equivalent (auto-tuned)
./keyhunt -m bsgs -f pubkeys.txt

# 0.3.0 with manual override
./keyhunt -m bsgs -f pubkeys.txt -n 0x8000000 -k 1024
# Output: [✓] N=0x8000000 validated (fits in 2048 MB)
```

---

#### Example 3: GPU-Accelerated Search

```bash
# 0.2.x syntax (manual GPU mode)
./keyhunt -m address -f tests/66.txt -b 66 -R -g 1

# 0.3.0 equivalent (auto-detect)
./keyhunt -m address -f tests/66.txt -b 66 -R -g auto

# 0.3.0 output
# [✓] GPU Device: NVIDIA RTX 3080 (10240 MB VRAM)
# [✓] GPU Batch Size: 8192 (optimal for 10 GB VRAM)
```

---

#### Example 4: Distributed Mode (NEW in 0.3.0)

**Server (coordinator + local worker):**
```bash
./keyhunt --wizard
# Select puzzle #66
# Choose "SERVER" mode
# Accept defaults or customize
# Configuration saved to keyhunt_wizard.json
```

**Client (remote worker):**
```bash
./keyhunt --client 192.168.1.100:7777
# Auto-detects hardware: 8 cores, 16GB RAM, GTX 1080
# Connects to server and requests work
# Reports progress via heartbeat
```

**Manual distributed server start:**
```bash
./keyhunt --server --port 7777 -m address -f tests/66.txt -b 66 -R
```

---

### File Format Compatibility

#### BSGS Bloom Filter Files

**File naming convention (unchanged):**
- `keyhunt_bsgs_4_*.blm` - Primary bloom filter
- `keyhunt_bsgs_5_*.blm` - Secondary bloom filter
- `keyhunt_bsgs_*.tbl` - bP table

**Migration:**
- ✅ **0.2.x bloom files are compatible with 0.3.0**
- ✅ **No conversion needed**
- 💡 **Regeneration**: Optional, for potential performance improvements

**Regeneration (optional):**
```bash
# Delete old bloom filters
rm keyhunt_bsgs_*.blm keyhunt_bsgs_*.tbl

# Regenerate with 0.3.0 (auto-tuned parameters)
./keyhunt -m bsgs -f pubkeys.txt -S
```

---

#### Progress Files

**0.2.x progress files:**
- Not officially supported in 0.2.x

**0.3.0 progress files:**
- `~/.keyhunt/progress/<session_id>.json` (NEW)
- Auto-saves every 60 seconds
- JSON format for human inspection

**Migration:**
- ✅ **N/A** - No previous progress format to migrate from

---

### Environment Variables

**New environment variables in 0.3.0:**

| Variable | Purpose | Default | Example |
|----------|---------|---------|---------|
| `KEYHUNT_SKIP_SYSINFO` | Skip hardware auto-detection | `0` | `KEYHUNT_SKIP_SYSINFO=1 ./keyhunt ...` |
| `KEYHUNT_WIZARD_CONFIG` | Custom wizard config path | `./keyhunt_wizard.json` | `KEYHUNT_WIZARD_CONFIG=/etc/keyhunt.json` |
| `KEYHUNT_CACHE_DIR` | Custom cache directory | `~/.keyhunt/` | `KEYHUNT_CACHE_DIR=/tmp/keyhunt/` |

**Migration:**
- ✅ **None** - These are optional overrides
- 💡 **Use case**: Set `KEYHUNT_SKIP_SYSINFO=1` on systems where hardware detection fails

**See**: [docs/ENV_VARIABLES.md](docs/ENV_VARIABLES.md) for complete reference

---

### Common Migration Scenarios

#### Scenario 1: Single-Machine User → Wizard Mode

**Previous workflow (0.2.x):**
```bash
# Manually calculate parameters
# Search online for "Bitcoin puzzle 66"
# Copy address: 13zb1hQbWVsc2S7ZTZnP2G4undNNpdh5so
./keyhunt -m address -f puzzle66.txt -b 66 -R -t 8 -r 20000000000000000:3ffffffffffffffff
```

**New workflow (0.3.0):**
```bash
./keyhunt --wizard
# Step 1: Select "Puzzle 66" from list
# Step 2: Choose "Single Machine" mode
# Step 3: Accept auto-detected hardware (16 cores, 16GB RAM)
# Step 4: Enable community exclusions
# Done! Configuration saved to keyhunt_wizard.json
```

**Benefits:**
- ✅ No manual parameter calculation
- ✅ Optimal settings for your hardware
- ✅ Avoids already-scanned ranges
- ✅ Easy to restart (reuses keyhunt_wizard.json)

---

#### Scenario 2: Multi-Machine User → Distributed Mode

**Previous workflow (0.2.x):**
```bash
# Machine 1: Search range 20000000000000000:27ffffffffffffff
./keyhunt -m address -f puzzle66.txt -b 66 -R -t 16 -r 20000000000000000:27ffffffffffffff

# Machine 2: Search range 28000000000000000:2fffffffffffffff
./keyhunt -m address -f puzzle66.txt -b 66 -R -t 8 -r 28000000000000000:2fffffffffffffff

# Machine 3: Search range 30000000000000000:37ffffffffffffff
./keyhunt -m address -f puzzle66.txt -b 66 -R -t 12 -r 30000000000000000:37ffffffffffffff

# Problem: Manual range splitting, no coordination, possible overlaps
```

**New workflow (0.3.0):**
```bash
# Server (Machine 1):
./keyhunt --wizard
# Select "Puzzle 66" → "SERVER" mode → Port 7777
# Server auto-starts and begins coordinating

# Worker (Machine 2):
./keyhunt --client 192.168.1.100:7777
# Auto-detects hardware, connects, requests work

# Worker (Machine 3):
./keyhunt --client 192.168.1.100:7777
# Auto-detects hardware, connects, requests work

# Benefits:
# - Automatic range splitting (no overlaps)
# - Server tracks progress across all workers
# - Heartbeat monitoring detects dead workers
# - Immediate key-found notification to all workers
```

---

#### Scenario 3: Custom Script Users → Parameter Validation

**Previous workflow (0.2.x):**
```bash
#!/bin/bash
# Script that runs keyhunt with various parameters
# Problem: Could crash with OOM if parameters too large

THREADS=32  # Might be too many
N_VALUE=0x100000000000  # Might exceed RAM
K_FACTOR=8192  # Might be too large

./keyhunt -m bsgs -f pubkeys.txt -n $N_VALUE -k $K_FACTOR -t $THREADS
# Result: Out of memory crash or context switching overhead
```

**New workflow (0.3.0):**
```bash
#!/bin/bash
# Script that leverages auto-tuning and validation

./keyhunt -m bsgs -f pubkeys.txt
# Auto-detects optimal N, K, and thread count
# Validates against available RAM
# Prints warnings if manual values would be dangerous

# OR: Use manual values with auto-correction
./keyhunt -m bsgs -f pubkeys.txt -n 0x100000000000 -k 8192
# Output: [!] Auto-corrected to N=0x20000000, K=2048
# Result: Safe execution, no crash
```

**Benefits:**
- ✅ No more OOM crashes
- ✅ Optimal performance without manual tuning
- ✅ Clear feedback on parameter safety

---

#### Scenario 4: BSGS Users with Saved Bloom Files

**Previous workflow (0.2.x):**
```bash
# Generate bloom filters (takes 10-60 minutes)
./keyhunt -m bsgs -f pubkeys.txt -n 0x8000000 -k 1024 -S

# Files created:
# keyhunt_bsgs_4_8000000.blm
# keyhunt_bsgs_5_8000000.blm
# keyhunt_bsgs_8000000.tbl

# Reuse bloom files in subsequent runs
./keyhunt -m bsgs -f pubkeys.txt -n 0x8000000 -k 1024
```

**New workflow (0.3.0):**
```bash
# Auto-tune parameters and save bloom files
./keyhunt -m bsgs -f pubkeys.txt -S
# Output: [✓] Auto-tuned: N=0x10000000, K=1 (optimal for 16GB RAM)
# Files created:
# keyhunt_bsgs_4_10000000.blm
# keyhunt_bsgs_5_10000000.blm
# keyhunt_bsgs_10000000.tbl

# Reuse bloom files (auto-detected)
./keyhunt -m bsgs -f pubkeys.txt
# Output: [✓] Found existing bloom files, loading...
```

**Benefits:**
- ✅ Optimal N value calculated automatically
- ✅ Bloom files auto-detected and reused
- ✅ No need to remember exact N value

---

### Performance Expectations

#### Single-Machine Performance

**0.2.x baseline:**
- Address mode: ~50 MKey/s (8 cores, no GPU)
- BSGS mode: ~20 MKey/s (8 cores)

**0.3.0 improvements:**
- Address mode: ~50 MKey/s CPU + ~500 MKey/s GPU (hybrid mode)
- BSGS mode: ~25 MKey/s (5-10% improvement from auto-tuning)
- Parameter validation prevents suboptimal configurations

**Expected speedup: 1.1x - 11x** (depending on GPU availability)

---

#### Distributed Mode Performance

**Linear scaling (approximate):**
- 1 machine: 50 MKey/s
- 4 machines: 200 MKey/s (4x)
- 10 machines: 500 MKey/s (10x)

**Overhead:**
- Coordinator: <1% CPU usage
- Network: <10 KB/s per worker (heartbeat + work assignment)

---

### Troubleshooting

#### Issue 1: "Auto-tuning failed, hardware detection unavailable"

**Cause:** Hardware detection requires `/proc/cpuinfo` and `/sys/devices/system/cpu/` (Linux only)

**Solution:**
```bash
# Option 1: Skip auto-tuning and use manual parameters
./keyhunt -m address -f tests/66.txt -b 66 -R -t 8

# Option 2: Set environment variable
export KEYHUNT_SKIP_SYSINFO=1
./keyhunt -m address -f tests/66.txt -b 66 -R -t 8
```

---

#### Issue 2: Wizard fails to download puzzle database

**Cause:** Network unavailable or BTCPuzzle.info down

**Solution:** Wizard automatically falls back to built-in database
```bash
./keyhunt --wizard
# Output: [i] Network unavailable, using built-in puzzle database
```

---

#### Issue 3: "GPU mode requested but no CUDA device found"

**Cause:** CUDA not installed or GPU not detected

**Solution:**
```bash
# Check CUDA installation
nvidia-smi

# If CUDA installed but not detected, check driver version
./keyhunt -m address -f tests/66.txt -b 66 -R -g off
# Output: [i] GPU disabled, using CPU-only mode
```

---

#### Issue 4: Old bloom files not detected

**Cause:** N value changed between runs

**Solution:**
```bash
# Option 1: Regenerate with current N value
rm keyhunt_bsgs_*.blm keyhunt_bsgs_*.tbl
./keyhunt -m bsgs -f pubkeys.txt -S

# Option 2: Use exact N value from old bloom files
./keyhunt -m bsgs -f pubkeys.txt -n 0x8000000
# Output: [✓] Found keyhunt_bsgs_4_8000000.blm, loading...
```

---

### Rollback Instructions

If you need to revert to 0.2.x:

1. **Download 0.2.x release:**
```bash
git checkout v0.2.230519
make clean && make
```

2. **Remove 0.3.0 cache files:**
```bash
rm keyhunt_wizard.json
rm puzzles_cache.txt
rm -rf ~/.keyhunt/
```

3. **Use 0.2.x command syntax:**
```bash
# Always specify threads manually
./keyhunt -m address -f tests/66.txt -b 66 -R -t 8
```

---

### Migration Checklist

Use this checklist when upgrading to 0.3.0:

- [ ] **Backup existing scripts** that use keyhunt (optional, for safety)
- [ ] **Download and compile 0.3.0** (`git pull && make clean && make`)
- [ ] **Test with auto-tuning** (omit `-t`, `-n`, `-k` flags)
- [ ] **Review parameter validation output** (look for ✓, i, !, ⚠ symbols)
- [ ] **Try wizard mode** (`./keyhunt --wizard`) for new projects
- [ ] **Optional: Regenerate bloom files** for BSGS mode with auto-tuned N
- [ ] **Update scripts** to remove manual parameter calculations
- [ ] **Consider distributed mode** if you have multiple machines

---

### Getting Help

- **Documentation**: See [README.md](README.md), [WIZARD.md](docs/WIZARD.md), [PARAMETER_VALIDATION.md](docs/PARAMETER_VALIDATION.md)
- **Environment Variables**: See [docs/ENV_VARIABLES.md](docs/ENV_VARIABLES.md)
- **GitHub Issues**: https://github.com/albertobsd/keyhunt/issues
- **Community**: Bitcoin puzzle solving forums

---

## Version 0.2.x → 0.3.0 Quick Reference

| Feature | 0.2.x | 0.3.0 |
|---------|-------|-------|
| **Thread count** | Manual `-t` required | Auto-detected (all cores) |
| **BSGS N/K values** | Manual calculation | Auto-tuned to RAM |
| **GPU mode** | `-g 0` or `-g 1` | `-g auto` (auto-detect) |
| **Parameter validation** | None | Full validation + auto-correction |
| **Distributed mode** | Not available | Server + client architecture |
| **Interactive wizard** | Not available | `--wizard` or `-W` |
| **Community integration** | Not available | BTCPuzzle.info + privatekeys.pw |
| **Hardware detection** | None | CPU, RAM, GPU, SIMD features |
| **Progress tracking** | None | Auto-save to JSON every 60s |

---

## Future Versions

**Planned for 0.4.x:**
- macOS and Windows support for distributed mode
- Web-based dashboard for coordinator
- Advanced work-stealing scheduler
- Checkpoint resume from network failures

---

**Last Updated:** 2026-02-28
**Version:** 0.3.0
