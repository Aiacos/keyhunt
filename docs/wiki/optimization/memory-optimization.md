# Memory Optimization Guide

This guide covers memory management for keyhunt, particularly for memory-intensive BSGS mode.

## Memory Requirements by Mode

| Mode | Memory Usage | Scalability |
|------|-------------|-------------|
| ADDRESS | Low (bloom filter) | Linear with targets |
| XPOINT | Low | Linear with targets |
| RMD160 | Low | Linear with targets |
| BSGS | **Very High** | Exponential with N value |
| VANITY | Low | Fixed |

## BSGS Memory Deep Dive

### Memory Components

BSGS requires three main data structures:

```
┌────────────────────────────────────────────────────────────────┐
│                     BSGS MEMORY LAYOUT                         │
├────────────────────────────────────────────────────────────────┤
│  Bloom Filter 1 (primary):    M * K * 3.5 bytes               │
│  Bloom Filter 2 (secondary):  M * K * 3.5 / 32 bytes          │
│  Bloom Filter 3 (tertiary):   M * K * 3.5 / 1024 bytes        │
│  bP Table (baby steps):       M / 32 * K * 16 bytes           │
├────────────────────────────────────────────────────────────────┤
│  Where: M = sqrt(range), K = multiplier factor                │
└────────────────────────────────────────────────────────────────┘
```

### Memory Formula

```
Total RAM = M * K * 3.5 * (1 + 1/32 + 1/1024) + (M/32 * K * 16)
         ≈ M * K * 20 bytes (simplified)
```

### Example Calculations

| N Value | K Factor | Baby Steps | Memory |
|---------|----------|------------|--------|
| 2^30 | 1 | 1 billion | ~20 GB |
| 2^32 | 1 | 4 billion | ~80 GB |
| 2^34 | 1 | 16 billion | ~320 GB |
| 2^30 | 2 | 2 billion | ~40 GB |

## Memory Validation

### Automatic Checking

Keyhunt validates memory before allocation:

```
[+] BSGS Memory Check:
    Available RAM: 32.0 GB
    Required RAM: 24.5 GB
    Status: OK (76% utilization)
```

### Insufficient Memory

```
[!] BSGS Memory Check:
    Available RAM: 8.0 GB
    Required RAM: 24.5 GB
    Status: INSUFFICIENT (306%)

[!] Suggestions:
    1. Use N = 0x40000000 (fits in 6.0 GB)
    2. Use K = 1 (lower multiplier)
    3. Add more RAM or use swap
```

## Optimizing N and K Values

### Finding Optimal N

Use 60% of available RAM:

```bash
# Auto-calculate
./keyhunt -m bsgs -f pubkey.txt -b 125

# Manual specification
./keyhunt -m bsgs -f pubkey.txt -b 125 -n 0x100000000000
```

### K Factor Trade-offs

| K Value | Memory | Speed | Iterations |
|---------|--------|-------|------------|
| 1 | Baseline | Normal | Normal |
| 2 | 2x | ~1.5x | 0.5x |
| 4 | 4x | ~2x | 0.25x |

Higher K = more memory, fewer iterations needed.

### Calculation Tool

```python
# Python helper to calculate memory
import math

def bsgs_memory(n_value, k_factor=1):
    m = n_value
    bytes_per_entry = 20  # Approximate
    total_bytes = m * k_factor * bytes_per_entry
    return total_bytes / (1024**3)  # GB

# Example: 2^40 baby steps
n = 2**40
print(f"Memory needed: {bsgs_memory(n):.1f} GB")
```

## Using Swap Space

For memory beyond physical RAM:

### Create Swap File

```bash
# Create 64GB swap file
sudo fallocate -l 64G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile

# Verify
swapon --show
```

### Persistent Swap

Add to `/etc/fstab`:
```
/swapfile none swap sw 0 0
```

### Swap Performance

| Storage | Read Speed | BSGS Impact |
|---------|------------|-------------|
| NVMe SSD | 3000 MB/s | Moderate slowdown |
| SATA SSD | 500 MB/s | Significant slowdown |
| HDD | 100 MB/s | Very slow (not recommended) |

## Saving/Loading Tables

### Save Tables to Disk

```bash
./keyhunt -m bsgs -f pubkey.txt -b 125 -S
```

Creates:
- `keyhunt_bsgs_*.blm` - Bloom filter data
- `keyhunt_bsgs_*.tbl` - Baby step table

### Load Existing Tables

```bash
# Same command automatically loads if files exist
./keyhunt -m bsgs -f pubkey.txt -b 125 -S
```

```
[+] Loading saved tables...
[+] Bloom filter: loaded from keyhunt_bsgs_001.blm
[+] bP table: loaded from keyhunt_bsgs_001.tbl
[+] Load time: 2.3 seconds (vs 45 minutes compute)
```

### Table Storage Requirements

Same as RAM requirements. A 20GB table = 20GB disk space.

## Bloom Filter Tuning

### False Positive Rate

Three-tier bloom filter hierarchy balances memory and accuracy:

| Level | Size | False Positive Rate | Purpose |
|-------|------|---------------------|---------|
| Bloom1 | Full | ~0.1% | Primary check |
| Bloom2 | 1/32 | ~3% | Secondary filter |
| Bloom3 | 1/1024 | ~10% | Quick reject |

### Custom False Positive Rate

Lower false positive = more memory:

```bash
./keyhunt -m bsgs -f pubkey.txt -b 125 --bloom-fp 0.001
```

## RAM-Constrained Strategies

### Multiple Passes

With less RAM, make multiple passes with smaller tables:

```bash
# 8GB RAM available
# Instead of one pass with 24GB table:

# Make 3 passes with 8GB tables each
./keyhunt -m bsgs -f pubkey.txt -b 125 -n 0x40000000000 --bsgs-pass 1/3
./keyhunt -m bsgs -f pubkey.txt -b 125 -n 0x40000000000 --bsgs-pass 2/3
./keyhunt -m bsgs -f pubkey.txt -b 125 -n 0x40000000000 --bsgs-pass 3/3
```

### Distributed BSGS

Split across multiple machines:

```bash
# Machine 1 (32GB RAM)
./keyhunt -m bsgs -f pubkey.txt -b 125 --bsgs-offset 0

# Machine 2 (32GB RAM)
./keyhunt -m bsgs -f pubkey.txt -b 125 --bsgs-offset 1

# Each machine searches different giant step ranges
```

## Memory Profiling

### Check Memory Usage

```bash
# Before running
free -h

# During operation
watch -n 1 free -h

# Detailed per-process
ps aux --sort=-%mem | head
```

### Memory Map

```bash
# Detailed memory breakdown
pmap -x $(pgrep keyhunt) | tail -20
```

## Troubleshooting

### Out of Memory (OOM) Killed

**Symptom**: Process killed by kernel

**Solutions**:
1. Reduce N value
2. Reduce K factor
3. Add swap space
4. Use distributed mode

### Slow Performance with Swap

**Symptom**: High swap usage, slow speed

**Solutions**:
1. Reduce N value to fit in RAM
2. Use NVMe SSD for swap
3. Upgrade RAM

### Memory Fragmentation

**Symptom**: Allocation fails despite free memory

**Solutions**:
1. Restart keyhunt
2. Reduce concurrent allocations
3. Use huge pages

### Enable Huge Pages

For large allocations:

```bash
# Check current
cat /proc/meminfo | grep Huge

# Enable (requires root)
echo 10000 | sudo tee /proc/sys/vm/nr_hugepages

# Verify
cat /proc/meminfo | grep Huge
```

## Memory-Efficient Alternatives

### Use XPOINT Instead of BSGS

If memory is extremely limited:

```bash
# XPOINT uses minimal memory
./keyhunt -m xpoint -f xpoints.txt -b 125 -t 32
```

Trade-off: O(N) complexity instead of O(sqrt(N)).

### Cloud Instances

For one-time large BSGS runs:

| Provider | Instance | RAM | Cost/Hour |
|----------|----------|-----|-----------|
| AWS | r6i.24xlarge | 768 GB | ~$6 |
| GCP | n2-highmem-128 | 864 GB | ~$8 |
| Azure | E96as_v5 | 672 GB | ~$5 |

## See Also

- [BSGS Mode](../modes/bsgs-mode.md) - BSGS algorithm details
- [CPU Tuning](cpu-tuning.md) - CPU optimization
- [Distributed Overview](../distributed/overview.md) - Split work across machines
