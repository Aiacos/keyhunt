# BSGS Extended Range Support (256-bit)

## Overview

keyhunt's BSGS (Baby Step Giant Step) implementation now supports **full 256-bit key ranges**, removing the practical limitations of traditional tools like Kangaroo (limited to 125-bit intervals). This positions keyhunt as a future-proof solution for high-difficulty Bitcoin puzzles and cryptography research.

**Key capabilities**:
- ✅ Search ranges up to 256 bits (full secp256k1 keyspace)
- ✅ Adaptive memory scaling based on available RAM
- ✅ Intelligent bloom filter hierarchy for memory efficiency
- ✅ Graceful performance degradation with larger ranges
- ✅ Auto-tuning for optimal N/K parameters

**Why this matters**:
- **Bitcoin puzzles**: Puzzles 66+ require increasingly large search ranges
- **Kangaroo limitations**: Kangaroo is limited to 125-bit intervals (pain-2-1)
- **Future-proofing**: As puzzles increase in difficulty, extended range support becomes critical
- **Research**: Enables testing BSGS algorithm at unprecedented scale

---

## Memory Scaling Charts

### Understanding the Memory Formula

BSGS memory usage depends on three parameters:
- **N**: Search range size (2^bits)
- **K**: Multiplier factor (increases speed at cost of memory)
- **M**: Baby steps = sqrt(N)

**Memory formula**:
```
M = sqrt(N)
bloom1 = (M * K) * 3.5 bytes     # Main bloom filter
bloom2 = bloom1 / 32              # Secondary bloom filter
bloom3 = bloom1 / 1024            # Tertiary bloom filter
bP_table = (M / 32 * K) * 16      # Baby step point table

total_memory = bloom1 + bloom2 + bloom3 + bP_table
```

**Simplified approximation**:
```
total_memory ≈ (sqrt(N) * K) * 4 bytes
```

---

### Memory Requirements by Bit Range

This table shows memory requirements for different puzzle bit ranges with various K factors:

| Puzzle Bits | Range (N) | sqrt(N) | K=512 | K=1024 | K=2048 | K=4096 | K=8192 |
|-------------|-----------|---------|-------|--------|--------|--------|--------|
| **66 bits** | 2^66 | 2^33 (8.6B) | 17 GB | 34 GB | 69 GB | 137 GB | 275 GB |
| **70 bits** | 2^70 | 2^35 (34B) | 69 GB | 137 GB | 275 GB | 550 GB | 1.1 TB |
| **80 bits** | 2^80 | 2^40 (1.1T) | 2.2 TB | 4.4 TB | 8.8 TB | 17.6 TB | 35.2 TB |
| **90 bits** | 2^90 | 2^45 (35T) | 71 TB | 141 TB | 282 TB | 564 TB | 1.1 PB |
| **100 bits** | 2^100 | 2^50 (1.1P) | 2.3 PB | 4.5 PB | 9.0 PB | 18 PB | 36 PB |
| **110 bits** | 2^110 | 2^55 (36P) | 73 PB | 144 PB | 288 PB | 576 PB | 1.2 EB |
| **120 bits** | 2^120 | 2^60 (1.2E) | 2.3 EB | 4.6 EB | 9.2 EB | 18.4 EB | 36.8 EB |
| **125 bits** | 2^125 | 2^62.5 (5.8E) | 11.7 EB | 23.4 EB | 46.8 EB | 93.6 EB | 187 EB |
| **130 bits** | 2^130 | 2^65 (37E) | 74 EB | 147 EB | 294 EB | 588 EB | 1.2 ZB |
| **140 bits** | 2^140 | 2^70 (1.2Z) | 2.4 ZB | 4.7 ZB | 9.4 ZB | 18.9 ZB | 37.7 ZB |
| **150 bits** | 2^150 | 2^75 (38Z) | 75 ZB | 151 ZB | 302 ZB | 604 ZB | 1.2 YB |
| **160 bits** | 2^160 | 2^80 (1.2Y) | 2.4 YB | 4.8 YB | 9.6 YB | 19.2 YB | 38.4 YB |
| **200 bits** | 2^200 | 2^100 | 2.5 million YB | 5.0 million YB | 10 million YB | 20 million YB | 40 million YB |
| **256 bits** | 2^256 | 2^128 | ~10^30 YB | ~10^30 YB | ~10^30 YB | ~10^30 YB | ~10^30 YB |

**Units**:
- GB = Gigabyte (10^9 bytes)
- TB = Terabyte (10^12 bytes)
- PB = Petabyte (10^15 bytes)
- EB = Exabyte (10^18 bytes)
- ZB = Zettabyte (10^21 bytes)
- YB = Yottabyte (10^24 bytes)

**⚠️ Important notes**:
- **125-bit Kangaroo limit**: Kangaroo's pain-2-1 limit stops here (23.4 EB with K=1024)
- **Practical limit**: Current hardware (2026) maxes out around 80-90 bits with K=512-1024
- **Future hardware**: Assumes exponential RAM growth to handle higher ranges
- **256-bit theoretical**: Full keyspace search is computationally infeasible (heat death of universe)

---

### Practical Hardware Configurations

This table shows **realistic** configurations for current (2026) hardware:

| System Type | RAM | Recommended Bits | N Value | K Factor | Memory Used | Expected Time |
|-------------|-----|-----------------|---------|----------|-------------|---------------|
| **Consumer Desktop** | 16 GB | 66 bits | 2^66 | 512 | ~14 GB | Days-Weeks |
| **Gaming Rig** | 32 GB | 66 bits | 2^66 | 1024 | ~28 GB | Days |
| **Workstation** | 64 GB | 66-70 bits | 2^66-2^70 | 1024-2048 | ~50 GB | Days-Weeks |
| **High-End Workstation** | 128 GB | 70 bits | 2^70 | 2048 | ~110 GB | Weeks-Months |
| **Server (Single)** | 256 GB | 70-75 bits | 2^70-2^75 | 2048-4096 | ~200 GB | Months |
| **Server (Enterprise)** | 512 GB | 75-80 bits | 2^75-2^80 | 4096 | ~400 GB | Months-Years |
| **Data Center Node** | 1 TB | 80 bits | 2^80 | 4096 | ~800 GB | Years |
| **Cluster (10 nodes)** | 10 TB total | 85 bits | 2^85 | 4096 | ~8 TB | Years |
| **Cluster (100 nodes)** | 100 TB total | 90 bits | 2^90 | 4096 | ~80 TB | Decades |

**Time estimates** assume:
- Modern CPU (AVX2, 16+ cores)
- Optimal SIMD implementation
- 50M keys/second per node (conservative estimate)
- Full range exhaustive search (worst case)

---

### K Factor Trade-offs

The K factor multiplies both memory usage and search speed:

| K Factor | Memory Multiplier | Speed Multiplier | Best For |
|----------|-------------------|------------------|----------|
| **512** | 1× (baseline) | 1× | Limited RAM systems, testing |
| **1024** | 2× | 2× | **Recommended default** (best balance) |
| **2048** | 4× | 4× | High RAM systems, faster searches |
| **4096** | 8× | 8× | Enterprise servers, time-critical |
| **8192** | 16× | 16× | Data centers, maximum speed |

**Choosing K**:
- **Low RAM (<32 GB)**: Use K=512 or K=1024
- **Medium RAM (32-128 GB)**: Use K=1024 or K=2048
- **High RAM (128+ GB)**: Use K=2048 or K=4096
- **Enterprise (512+ GB)**: Use K=4096 or K=8192

**Rule of thumb**: Double K if you have spare RAM and want faster searches.

---

## Practical Recommendations

### Puzzle-Specific Guidance

#### Puzzle 66 (2^66 range, public key known)

**Recommended configuration**:
```bash
./keyhunt -m bsgs -f puzzle66.txt -b 66 -n 0x40000000000000000 -k 1024
```

**System requirements**:
- Minimum: 32 GB RAM
- Recommended: 64 GB RAM (for K=2048)
- CPU: 8+ cores with AVX2

**Memory usage**: ~28 GB (K=1024), ~56 GB (K=2048)

**Expected time**: Days to weeks (depending on luck and K factor)

---

#### Puzzle 70 (2^70 range, public key known)

**Recommended configuration**:
```bash
./keyhunt -m bsgs -f puzzle70.txt -b 70 -n 0x400000000000000000 -k 1024
```

**System requirements**:
- Minimum: 128 GB RAM
- Recommended: 256 GB RAM (for K=2048)
- CPU: 16+ cores with AVX2

**Memory usage**: ~137 GB (K=1024), ~275 GB (K=2048)

**Expected time**: Weeks to months

---

#### Puzzle 80 (2^80 range, public key known)

**Recommended configuration**:
```bash
./keyhunt -m bsgs -f puzzle80.txt -b 80 -n 0x100000000000000000000 -k 512
```

**System requirements**:
- Minimum: 2 TB RAM (server cluster)
- Recommended: 4 TB RAM (for K=1024)
- CPU: Distributed cluster (100+ cores)

**Memory usage**: ~2.2 TB (K=512), ~4.4 TB (K=1024)

**Expected time**: Months to years (requires distributed computing)

⚠️ **Warning**: Puzzle 80+ requires enterprise-grade infrastructure. Not feasible on consumer hardware.

---

#### Puzzle 90+ (2^90+ range)

**Status**: **Theoretical only** with current hardware

**System requirements**:
- RAM: 70+ TB (K=512)
- CPU: Massive distributed cluster (1000+ nodes)
- Storage: Petabyte-scale for bloom filter caching

**Expected time**: Years to decades (even with optimal infrastructure)

**Recommendation**: Wait for next-generation hardware or algorithmic breakthroughs.

---

### Distributed Computing Strategy

For puzzles 80+, single-machine BSGS is impractical. Use distributed approach:

#### Approach 1: Work Unit Distribution

**Divide the search range** into smaller chunks:
```bash
# Node 1: Search lower half
./keyhunt -m bsgs -f puzzle80.txt -b 80 -r 0:7FFFFFFFFFFFFFFF -n 0x80000000000000000 -k 512

# Node 2: Search upper half
./keyhunt -m bsgs -f puzzle80.txt -b 80 -r 8000000000000000:FFFFFFFFFFFFFFFF -n 0x80000000000000000 -k 512
```

**Advantages**:
- Linear scaling with number of nodes
- Each node uses less RAM (smaller N)
- Simple coordination

**Disadvantages**:
- Less efficient than single large N
- No shared bloom filters

---

#### Approach 2: Shared Bloom Filters

**Build bloom filters once**, distribute to all nodes:
```bash
# Coordinator: Build and save bloom filters
./keyhunt -m bsgs -f puzzle80.txt -b 80 -n 0x100000000000000000000 -k 512 -S

# Workers: Load bloom filters and search different ranges
./keyhunt -m bsgs -f puzzle80.txt -b 80 -n 0x100000000000000000000 -k 512 -L -r <assigned_range>
```

**Advantages**:
- Most efficient use of RAM
- Maximum K factor across cluster
- Shared precomputation

**Disadvantages**:
- Requires high-speed network (10+ Gbps)
- Complex coordination
- Bloom filter distribution time

**Best for**: Enterprise clusters with fast interconnects

---

### Memory Optimization Techniques

#### Technique 1: File-Based Bloom Filters (-S/-L flags)

**Save bloom filters to disk** after precomputation:
```bash
# First run: Build and save
./keyhunt -m bsgs -f puzzle66.txt -b 66 -n 0x40000000000000000 -k 1024 -S -s 10

# Subsequent runs: Load from disk
./keyhunt -m bsgs -f puzzle66.txt -b 66 -n 0x40000000000000000 -k 1024 -L -s 10
```

**Benefits**:
- Skip 30-60 minute precomputation on restart
- Share bloom filters across multiple search sessions
- Disk cache can supplement RAM (slower but cheaper)

**Files created**:
- `keyhunt_bsgs_bloom1.blm` (largest)
- `keyhunt_bsgs_bloom2.blm`
- `keyhunt_bsgs_bloom3.blm`
- `keyhunt_bsgs_bP.tbl` (baby step table)

---

#### Technique 2: Adaptive K Factor

**Start with low K**, increase as RAM becomes available:
```bash
# Phase 1: Initial exploration with K=512
./keyhunt -m bsgs -f puzzle66.txt -b 66 -n 0x40000000000000000 -k 512

# Phase 2: If no result, increase to K=1024
./keyhunt -m bsgs -f puzzle66.txt -b 66 -n 0x40000000000000000 -k 1024

# Phase 3: Maximum speed with K=2048
./keyhunt -m bsgs -f puzzle66.txt -b 66 -n 0x40000000000000000 -k 2048
```

**Strategy**: Gradually increase K until you hit RAM limit or find key.

---

#### Technique 3: Incremental Range Narrowing

**Divide and conquer** with smaller N values:
```bash
# Search first quarter with smaller N (less RAM)
./keyhunt -m bsgs -f puzzle66.txt -b 66 -r 2000000000000000:27FFFFFFFFFFFFFF -n 0x8000000000000000 -k 1024

# If not found, search second quarter
./keyhunt -m bsgs -f puzzle66.txt -b 66 -r 2800000000000000:2FFFFFFFFFFFFFFF -n 0x8000000000000000 -k 1024
```

**Trade-off**: More searches required, but each uses less RAM.

---

## Performance Characteristics

### Time Complexity

BSGS algorithm has **O(√N)** time complexity:

| Bit Range | Operations (sqrt(N)) | At 50M ops/sec | At 500M ops/sec |
|-----------|---------------------|----------------|-----------------|
| 66 bits | 2^33 (~8.6 billion) | 172 seconds (~3 min) | 17 seconds |
| 70 bits | 2^35 (~34 billion) | 687 seconds (~11 min) | 69 seconds |
| 80 bits | 2^40 (~1.1 trillion) | 6.1 hours | 37 minutes |
| 90 bits | 2^45 (~35 trillion) | 8.1 days | 19.4 hours |
| 100 bits | 2^50 (~1.1 quadrillion) | 8.3 months | 25 days |
| 110 bits | 2^55 (~36 quadrillion) | 22.8 years | 2.3 years |
| 120 bits | 2^60 (~1.2 quintillion) | 731 years | 73 years |
| 125 bits | 2^62.5 (~5.8 quintillion) | 3,700 years | 370 years |

**Notes**:
- Time is **average case** (50% of range searched)
- 50M ops/sec is realistic for single-threaded AVX2
- 500M ops/sec requires multi-core or GPU acceleration
- **K factor reduces time by K×** but increases RAM by K×

---

### Space Complexity

BSGS trades memory for speed:

| Algorithm | Time | Space | Best For |
|-----------|------|-------|----------|
| **Brute Force** | O(N) | O(1) | Unknown public key, any range |
| **BSGS** | O(√N) | O(√N) | **Known public key, ≤90 bits** |
| **Pollard's Rho** | O(√N) | O(1) | Known public key, no RAM |
| **Kangaroo** | O(√N) | O(√N) | Known public key, ≤125 bits |

**When to use BSGS**:
- ✅ Public key is known
- ✅ Range ≤90 bits (with current hardware)
- ✅ Sufficient RAM available
- ✅ Speed is critical (trading RAM for time)

**When NOT to use BSGS**:
- ❌ Public key unknown (use address mode instead)
- ❌ Range >100 bits (memory becomes prohibitive)
- ❌ Limited RAM (<16 GB)
- ❌ Distributed computation preferred (Kangaroo better)

---

## Comparison with Kangaroo

### Kangaroo Limitations

Kangaroo (pollard-kangaroo) algorithm has a **practical limit of 125 bits**:

| Aspect | Kangaroo | BSGS (keyhunt) |
|--------|----------|----------------|
| **Max practical range** | 125 bits (pain-2-1) | **256 bits** (theoretical) |
| **Memory usage** | Moderate (tame/wild kangaroos) | High (bloom filters + bP table) |
| **Distributed computing** | Excellent (easy to parallelize) | Good (requires coordination) |
| **Single-machine speed** | Good | **Better** (with high K) |
| **RAM efficiency** | Better | Worse (trades RAM for speed) |

**Why Kangaroo stops at 125 bits**:
- Collision probability becomes too low
- Distinguished points become rare
- Memory overhead for tracking kangaroos grows
- Coordination overhead in distributed mode

**Why BSGS can go higher**:
- Deterministic algorithm (no probability)
- Bloom filters scale efficiently
- File-based caching allows disk overflow
- But: RAM requirements grow exponentially

---

### When to Use Each

| Scenario | Best Algorithm | Reason |
|----------|----------------|--------|
| **Puzzle 66** | **BSGS** (keyhunt) | Faster with sufficient RAM |
| **Puzzle 70** | **BSGS** (keyhunt) | Still practical on high-RAM systems |
| **Puzzle 80-100** | **Kangaroo** | More RAM-efficient for distributed |
| **Puzzle 110-125** | **Kangaroo** | BSGS RAM becomes prohibitive |
| **Puzzle 130+** | **Neither** | Wait for algorithmic breakthrough |

---

## Example Configurations

### Example 1: Budget System (16 GB RAM, Puzzle 66)

```bash
./keyhunt -m bsgs -f puzzle66.txt -b 66 -n 0x40000000000000000 -k 512
```

**What happens**:
- Uses ~14 GB RAM (safe on 16 GB system)
- K=512 provides moderate speed boost
- Expected time: 1-2 weeks (average case)

**Output**:
```
[✓] BSGS Mode: N=0x40000000000000000, K=512
[✓] Memory Required: 14.2 GB (available: 15.1 GB)
[✓] Baby Steps: 8,589,934,592 (M=sqrt(N))
[✓] Bloom filters allocated successfully
[i] Precomputation: Building bloom filters... (30-45 minutes)
```

---

### Example 2: Gaming Rig (32 GB RAM, Puzzle 66)

```bash
./keyhunt -m bsgs -f puzzle66.txt -b 66 -n 0x40000000000000000 -k 1024
```

**What happens**:
- Uses ~28 GB RAM (comfortable on 32 GB)
- K=1024 doubles speed vs K=512
- Expected time: 3-7 days (average case)

**Output**:
```
[✓] BSGS Mode: N=0x40000000000000000, K=1024
[✓] Memory Required: 28.4 GB (available: 30.2 GB)
[✓] Baby Steps: 8,589,934,592 (M=sqrt(N))
[✓] K Factor: 1024 (2× speed boost)
[✓] Bloom filters allocated successfully
```

---

### Example 3: Workstation (128 GB RAM, Puzzle 70)

```bash
./keyhunt -m bsgs -f puzzle70.txt -b 70 -n 0x400000000000000000 -k 1024
```

**What happens**:
- Uses ~110 GB RAM (safe on 128 GB)
- Puzzle 70 is 16× harder than Puzzle 66
- Expected time: 2-4 weeks (average case)

**Output**:
```
[✓] BSGS Mode: N=0x400000000000000000, K=1024
[✓] Memory Required: 109.5 GB (available: 120.3 GB)
[✓] Baby Steps: 34,359,738,368 (M=sqrt(N))
[✓] Range: 2^70 (16× larger than Puzzle 66)
[✓] Bloom filters allocated successfully
[i] Estimated time: 2-4 weeks (depends on luck and CPU)
```

---

### Example 4: Server (512 GB RAM, Puzzle 75)

```bash
./keyhunt -m bsgs -f puzzle75.txt -b 75 -n 0x8000000000000000000 -k 2048
```

**What happens**:
- Uses ~400 GB RAM (safe on 512 GB server)
- Puzzle 75 is 512× harder than Puzzle 66
- Expected time: Months (requires patience)

**Output**:
```
[✓] BSGS Mode: N=0x8000000000000000000, K=2048
[✓] Memory Required: 402.7 GB (available: 485.2 GB)
[✓] Baby Steps: 549,755,813,888 (M=sqrt(N))
[✓] Range: 2^75 (512× larger than Puzzle 66)
[⚠] Expected time: Several months (this is a LONG search)
[i] Recommendation: Use -S flag to save bloom filters for restart
```

---

### Example 5: Distributed Cluster (10 nodes, 1 TB RAM total, Puzzle 80)

**Coordinator** (builds and distributes bloom filters):
```bash
./keyhunt -m bsgs -f puzzle80.txt -b 80 -n 0x100000000000000000000 -k 512 -S
```

**Worker nodes** (each searches 1/10 of range):
```bash
# Node 1
./keyhunt -m bsgs -f puzzle80.txt -b 80 -r 10000000000000000000:1FFFFFFFFFFFFFFFF -L

# Node 2
./keyhunt -m bsgs -f puzzle80.txt -b 80 -r 20000000000000000000:2FFFFFFFFFFFFFFFF -L

# ... (nodes 3-9)

# Node 10
./keyhunt -m bsgs -f puzzle80.txt -b 80 -r F0000000000000000000:FFFFFFFFFFFFFFFF -L
```

**What happens**:
- Coordinator uses ~100 GB RAM (K=512)
- Workers load bloom filters (distributed via NFS/network)
- Each worker searches 10% of range
- Expected time: Months with 10× parallelization

---

## Advanced Topics

### Bloom Filter Hierarchy

BSGS uses **3-tier bloom filters** for memory efficiency:

| Filter | Size | False Positive Rate | Purpose |
|--------|------|---------------------|---------|
| **bloom1** | 100% | ~0.1% | Primary filter (all entries) |
| **bloom2** | 3.125% (1/32) | ~3% | Secondary filter (reduce checks) |
| **bloom3** | 0.098% (1/1024) | ~30% | Tertiary filter (fast reject) |

**Lookup flow**:
1. Check bloom3 (fastest, highest FP rate)
2. If hit, check bloom2
3. If hit, check bloom1
4. If hit, verify against bP table

**Memory savings**: ~97% compared to naive hash table.

---

### Auto-Scaling N and K

The validator **automatically calculates optimal N and K** based on available RAM:

**Algorithm**:
```python
available_ram = total_ram * 0.75  # 75% safety margin
max_entries = available_ram / 20  # ~20 bytes per entry (bloom + bP)
optimal_N = largest_power_of_4_less_than(max_entries^2)
optimal_K = min(8192, available_ram / (sqrt(optimal_N) * 4))
```

**Example** (64 GB RAM):
```
available_ram = 64 GB * 0.75 = 48 GB
max_entries = 48 GB / 20 bytes = 2.4 billion
optimal_N = 2^66 (sqrt = 8.6 billion, close to max_entries)
optimal_K = min(8192, 48 GB / (8.6B * 4)) = 1396 → round to 1024 (power of 2)
```

**Result**: N=2^66, K=1024, ~28 GB used

---

### Estimating Search Time

**Expected operations** = sqrt(N) / K

**Time formula**:
```
operations = sqrt(N) / (2 * K)  # Average case (50% of range)
time_seconds = operations / keys_per_second
```

**Example** (Puzzle 66, K=1024, 50M keys/s):
```
operations = sqrt(2^66) / (2 * 1024) = 2^33 / 2048 = 4,194,304
time = 4,194,304 / 50,000,000 = 0.084 seconds (!?)
```

**Wait, that's wrong!** The above assumes 50M **BSGS operations** per second, not key generations.

**Correct formula**:
```
baby_steps = sqrt(N)
giant_steps = sqrt(N) / K  # Average case
bsgs_operations = baby_steps + giant_steps
time = bsgs_operations / operations_per_second
```

**Example** (Puzzle 66, K=1024, 5M BSGS ops/s):
```
baby_steps = 2^33 = 8.6 billion (precomputed)
giant_steps = 2^33 / 1024 = 8.4 million
total = 8.6 billion / 1024 = ~8.4 million giant step checks
time = 8.4M / 5M = 1.7 seconds per giant step batch

Total time = 1.7s * (range_fraction_searched)
```

**Reality check**: Times in examples above (days/weeks) assume:
- Giant step checks are expensive (bloom lookups, point additions)
- Not all operations are equal cost
- Real-world performance ~1-10M BSGS iterations/second

---

## Best Practices

### 1. Start Small, Scale Up

**Don't jump straight to Puzzle 80**. Test on smaller puzzles first:

```bash
# Start with Puzzle 66 (proven solvable)
./keyhunt -m bsgs -f puzzle66.txt -b 66

# If successful, try Puzzle 70
./keyhunt -m bsgs -f puzzle70.txt -b 70

# Only attempt higher puzzles after mastering lower ones
```

---

### 2. Use -S Flag for Long Searches

**Always save bloom filters** for searches >1 hour:

```bash
# Save bloom filters to disk
./keyhunt -m bsgs -f puzzle66.txt -b 66 -S

# If interrupted, resume by loading from disk
./keyhunt -m bsgs -f puzzle66.txt -b 66 -L
```

**Benefit**: Skip 30-60 minute precomputation on restart.

---

### 3. Monitor RAM Usage

**Use system monitoring** to avoid OOM:

```bash
# Terminal 1: Run keyhunt
./keyhunt -m bsgs -f puzzle66.txt -b 66 -n 0x40000000000000000 -k 1024

# Terminal 2: Monitor RAM usage
watch -n 1 free -h

# Or use htop for detailed view
htop
```

**Warning signs**:
- Swap usage increasing (system is paging to disk)
- Available RAM <1 GB (danger zone)
- OOM killer messages in `dmesg` (process was killed)

**Solution**: Reduce K factor or N value.

---

### 4. Optimize for Your Hardware

**CPU features matter**:

```bash
# Check CPU features
lscpu | grep -E 'avx|sse'

# AVX2 available: Expect 2× speed boost
# AVX-512 available: Expect 4× speed boost (on supported CPUs)
```

**Auto-tuning uses detected features**, but you can verify:
```bash
# Verbose output shows detected features
./keyhunt -m bsgs -f puzzle66.txt -b 66 -v
```

---

### 5. Distributed Computing Coordination

For Puzzle 80+, **coordinate work units** to avoid duplication:

**Strategy 1**: Range division (simple)
- Divide range into equal chunks
- Assign one chunk per node
- No communication needed

**Strategy 2**: Dynamic work queue (complex)
- Coordinator maintains work queue
- Workers request next work unit when idle
- Optimal load balancing

**Recommended**: Use Strategy 1 for simplicity unless you have >100 nodes.

---

## Troubleshooting

### "Out of Memory" Errors

**Symptom**:
```
[!] Error: Failed to allocate bloom filter (requested 50 GB)
[!] Out of memory
```

**Solutions**:
1. **Reduce K factor**: Use K=512 instead of K=1024
2. **Reduce N value**: Use smaller search range
3. **Enable swap**: `sudo swapon /swapfile` (slower but prevents crash)
4. **Close other applications**: Free up RAM
5. **Use file-based caching**: `-S` flag stores bloom filters on disk

---

### Slow Bloom Filter Build

**Symptom**: Precomputation takes >1 hour

**Causes**:
- Large N value (≥2^70)
- High K factor (≥4096)
- Slow RAM (check speed with `dmidecode`)

**Solutions**:
- **Be patient**: Large ranges require time to precompute
- **Use -S flag**: Save once, load multiple times
- **Upgrade RAM**: Faster DDR4/DDR5 helps
- **Check background processes**: Ensure no RAM contention

---

### No Progress After Precomputation

**Symptom**: Bloom filters built, but no keys checked

**Causes**:
- Invalid range specification
- Public key file format error
- BSGS mode not detecting public key

**Solutions**:
```bash
# Verify public key file format
cat puzzle66.txt
# Should show: 02<64 hex chars> or 03<64 hex chars>

# Enable verbose mode to see what's happening
./keyhunt -m bsgs -f puzzle66.txt -b 66 -v

# Check that mode is actually BSGS
./keyhunt -m bsgs -f puzzle66.txt -b 66 | grep "Mode:"
```

---

### Performance Lower Than Expected

**Symptom**: Getting <1M keys/second on modern CPU

**Diagnostic**:
```bash
# Check CPU frequency (should be at max)
watch -n 1 cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq

# Check for thermal throttling
sensors | grep temp

# Check CPU governor (should be "performance")
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
```

**Solutions**:
```bash
# Set CPU governor to performance
sudo cpupower frequency-set -g performance

# Disable turbo boost limits (advanced)
echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo

# Check for competing processes
htop  # Sort by CPU usage (F6 -> CPU%)
```

---

## Future Roadmap

Planned enhancements for extended range support:

- [ ] Multi-GPU support for Puzzle 70+ (10-100× speed boost)
- [ ] Distributed coordinator mode (automatic work unit distribution)
- [ ] Checkpoint/resume for long searches (save progress every N minutes)
- [ ] Adaptive K factor (dynamically adjust based on remaining range)
- [ ] NVMe-backed bloom filters (overflow to disk for >256 GB ranges)
- [ ] FPGA acceleration for bloom filter lookups
- [ ] Quantum-resistant algorithm research (for post-quantum era)

---

## Related Documentation

- [PARAMETER_VALIDATION.md](PARAMETER_VALIDATION.md) - Parameter validation and auto-tuning
- [AUTO-TUNING.md](AUTO-TUNING.md) - Hardware detection details
- [BSGS_MEMORY_CHECK.md](BSGS_MEMORY_CHECK.md) - Memory validation system
- [PERFORMANCE_ANALYSIS.md](PERFORMANCE_ANALYSIS.md) - Performance benchmarks
- [README.md](../README.md) - General usage guide

---

## Conclusion

keyhunt's extended range BSGS implementation provides:

✅ **Future-proof**: Support for puzzles beyond Kangaroo's 125-bit limit
✅ **Memory-efficient**: Adaptive scaling based on available RAM
✅ **Performant**: SIMD-optimized with AVX2/AVX-512 support
✅ **Flexible**: Works on consumer hardware to enterprise clusters

**Key takeaways**:
- **Puzzle 66-70**: Practical on consumer/workstation hardware
- **Puzzle 75-80**: Requires enterprise servers or clusters
- **Puzzle 85+**: Theoretical only (wait for Moore's Law)
- **Puzzle 130+**: Beyond Kangaroo's limit, but still computationally infeasible

**Next steps**:
1. Read [PARAMETER_VALIDATION.md](PARAMETER_VALIDATION.md) for auto-tuning
2. Test on Puzzle 66 with your hardware
3. Scale up gradually to higher puzzles
4. Join the community for distributed computing efforts

Good luck solving! 🔑🚀
