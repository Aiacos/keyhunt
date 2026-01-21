# Bitcoin Puzzle Strategy Guide

## Table of Contents

1. [Introduction](#1-introduction)
2. [Brute Force Strategies](#2-brute-force-strategies)
3. [Public Key Exploitation (CRITICAL)](#3-public-key-exploitation-critical)
4. [Pattern-Based Optimization](#4-pattern-based-optimization)
5. [Transaction/ECDSA Vulnerabilities](#5-transactionecdsa-vulnerabilities)
6. [Brainwallet Dictionary Attacks](#6-brainwallet-dictionary-attacks)
7. [Strategy Selection Matrix](#7-strategy-selection-matrix)
8. [Realistic Time Estimates](#8-realistic-time-estimates)
9. [Security Considerations](#9-security-considerations)

---

## 1. Introduction

### What Are Bitcoin Puzzles?

The Bitcoin Puzzle Challenge, also known as the "1000 BTC Puzzle" or "Bitcoin Challenge," is a cryptographic challenge created in 2015 by an anonymous user. The creator generated 256 Bitcoin addresses with private keys in progressively larger bit ranges and funded them with increasing amounts of BTC.

**The Challenge Structure:**

| Puzzle Range | Private Key Bits | Address Count | Original Funding |
|--------------|------------------|---------------|------------------|
| #1 - #50     | 1-50 bits        | 50            | Small amounts    |
| #51 - #100   | 51-100 bits      | 50            | 0.001-0.01 BTC   |
| #101 - #160  | 101-160 bits     | 60            | 0.01-1+ BTC      |
| #161+        | 161+ bits        | Remaining     | Larger amounts   |

**Current Status (as of 2025):**
- Puzzles #1 through #70 have been solved
- Puzzle #71 (71-bit range) remains unsolved with approximately 0.71 BTC
- Puzzle #135 has 1.35 BTC with an EXPOSED PUBLIC KEY
- Total remaining bounty: approximately 1000+ BTC

### Why Some Puzzles Are Easier: The Public Key Factor

This is the **single most important concept** for puzzle hunters to understand:

**Without Public Key (e.g., Puzzle #71):**
- You must compute: Private Key -> Public Key -> SHA256 -> RIPEMD160 -> Address
- Time complexity: O(N) where N = 2^(bits-1)
- You cannot skip any step in the chain

**With Public Key Exposed (e.g., Puzzles #135, #140, #145, #150, #155, #160):**
- You can use mathematical shortcuts (BSGS, Kangaroo)
- Time complexity: O(sqrt(N)) = O(2^(bits/2))
- This is an EXPONENTIAL improvement

**Example Impact:**
```
Puzzle #135 (135-bit private key range):

Without public key: O(2^134) operations
With public key:    O(2^67)  operations (BSGS/Kangaroo)

Speedup factor: 2^67 = 147,573,952,589,676,412,928x faster
```

### Time Complexity Reality Check

Before investing time and resources, understand the mathematical reality:

| Bits | Keyspace Size | At 100 Mkeys/s | At 100 Gkeys/s | At 100 Tkeys/s |
|------|---------------|----------------|----------------|----------------|
| 50   | 2^49          | 89 years       | 32 days        | 47 minutes     |
| 60   | 2^59          | 91,000 years   | 91 years       | 33 days        |
| 70   | 2^69          | 93 million yrs | 93,000 years   | 93 years       |
| 80   | 2^79          | Heat death     | 95 million yrs | 95,000 years   |

**With BSGS/Kangaroo (public key available):**

| Bits | Effective Ops | At 1 Mops/s  | At 1 Gops/s  |
|------|---------------|--------------|--------------|
| 100  | 2^50          | 35 years     | 13 days      |
| 120  | 2^60          | 36,000 years | 36 years     |
| 135  | 2^67.5        | 5.7M years   | 5,700 years  |
| 160  | 2^80          | Heat death   | 38 million   |

---

## 2. Brute Force Strategies

### Linear Search (ADDRESS Mode)

Brute force is the straightforward approach: generate private keys sequentially, compute their addresses, and check against targets.

**When to Use:**
- Puzzles WITHOUT exposed public keys (e.g., #71)
- Small bit ranges (< 55 bits) for practice
- When you have limited RAM (BSGS requires significant memory)

### Basic Brute Force Command

```bash
# Search puzzle #66 range (solved puzzle, good for testing)
./keyhunt -m address -f targets/puzzle66.txt -b 66 -t 8 -R -q

# Parameters explained:
# -m address   : Address search mode (brute force)
# -f <file>    : File containing target addresses (one per line)
# -b 66        : Bit range (searches 2^65 to 2^66 - 1)
# -t 8         : Use 8 threads
# -R           : Random starting points within range
# -q           : Quiet mode (reduce output verbosity)
```

### Optimization: Compressed-Only Mode (2x Speed)

Bitcoin addresses can be derived from compressed or uncompressed public keys. If you KNOW your target uses compressed keys (most modern wallets do), you can skip uncompressed computation:

```bash
# Compressed-only mode (doubles throughput)
./keyhunt -m address -f targets/puzzle71.txt -b 71 -l compress -t 8 -R

# -l compress  : Only check compressed public keys
# This effectively doubles your search speed
```

**Why This Works:**
- Standard mode computes both compressed and uncompressed addresses
- Puzzle addresses are known to use compressed keys
- Skip uncompressed = 2x faster

### Parallel Processing with Multiple Threads

Keyhunt automatically detects CPU cores and optimizes thread allocation:

```bash
# Let keyhunt auto-detect optimal thread count
./keyhunt -m address -f targets/puzzle71.txt -b 71 -l compress -R

# Manual thread specification
./keyhunt -m address -f targets/puzzle71.txt -b 71 -l compress -t 16 -R

# With status updates every 30 seconds
./keyhunt -m address -f targets/puzzle71.txt -b 71 -l compress -t 16 -R -s 30
```

### Expected Throughput

**ADDRESS Mode Performance (per thread):**

| CPU Generation | SIMD Support | Keys/Second/Thread |
|----------------|--------------|-------------------|
| Pre-2015       | SSE2         | ~3-5 Mkeys/s      |
| 2015-2019      | AVX2         | ~6-10 Mkeys/s     |
| 2020+          | AVX-512      | ~10-15 Mkeys/s    |

**Total System Throughput:**
```
8-core modern CPU (AVX2):  ~50-80 Mkeys/s
16-core workstation:       ~100-150 Mkeys/s
High-end Xeon (AVX-512):   ~200-400 Mkeys/s
```

### Advanced: RMD160 Mode (Faster Lookups)

If you have target RIPEMD160 hashes instead of addresses:

```bash
# RMD160 mode (skips Base58 encoding overhead)
./keyhunt -m rmd160 -f targets/puzzle71.rmd -b 71 -l compress -t 8 -R

# This is slightly faster as it skips address encoding
# Target file format: raw 40-character hex RIPEMD160 hashes
```

### Dividing Work Across Multiple Machines

For distributed brute force across multiple computers:

```bash
# Machine 1: Search first quarter of range
./keyhunt -m address -f targets/puzzle71.txt -r 4000000000000000:5000000000000000 -l compress

# Machine 2: Search second quarter
./keyhunt -m address -f targets/puzzle71.txt -r 5000000000000000:6000000000000000 -l compress

# Machine 3: Search third quarter
./keyhunt -m address -f targets/puzzle71.txt -r 6000000000000000:7000000000000000 -l compress

# Machine 4: Search fourth quarter
./keyhunt -m address -f targets/puzzle71.txt -r 7000000000000000:8000000000000000 -l compress
```

---

## 3. Public Key Exploitation (CRITICAL)

### The Game-Changer: Exposed Public Keys

When a Bitcoin address has made at least one OUTGOING transaction, the public key is exposed on the blockchain. This fundamentally changes the attack complexity:

**Puzzles with EXPOSED PUBLIC KEYS:**
- Puzzle #135 (135-bit): Public key revealed
- Puzzle #140 (140-bit): Public key revealed
- Puzzle #145 (145-bit): Public key revealed
- Puzzle #150 (150-bit): Public key revealed
- Puzzle #155 (155-bit): Public key revealed
- Puzzle #160 (160-bit): Public key revealed

**Why This Matters:**

With an exposed public key Q = k*G (where k is the private key and G is the generator point), we can use discrete logarithm algorithms that operate in O(sqrt(N)) time instead of O(N).

```
Original complexity:     O(2^N)     = brute force every key
With public key:         O(2^(N/2)) = square root of keyspace

For 135-bit puzzle:
  Brute force: 2^134 operations
  BSGS/Kangaroo: 2^67 operations

  That's a 2^67 speedup (147 quintillion times faster)
```

### 3.1 BSGS (Baby Step Giant Step)

BSGS is a time-memory tradeoff algorithm invented by Daniel Shanks in 1971.

**How It Works:**

1. **Baby Steps**: Pre-compute a table of m points: {G, 2G, 3G, ..., mG}
2. **Giant Steps**: For each multiple of m, check if Q - j*m*G matches any baby step
3. If Q - j*m*G = i*G, then k = i + j*m

**Complexity:**
- Time: O(sqrt(N))
- Memory: O(sqrt(N))

For a 71-bit range: sqrt(2^71) = 2^35.5 operations and ~2^35.5 stored points

**BSGS with Keyhunt:**

```bash
# Basic BSGS search for puzzle #125 (testing with known public key)
./keyhunt -m bsgs -f targets/puzzle125_pubkey.txt -b 125 -q

# Parameters:
# -m bsgs      : Baby Step Giant Step mode
# -f <file>    : File containing target PUBLIC KEYS (not addresses!)
# -b 125       : Bit range to search
```

**Target File Format for BSGS:**
```
# File: puzzle125_pubkey.txt
# Format: compressed public key (33 bytes hex)
02145d2611c823a396ef6712ce0f712f09b9b4f3135e3e0aa3230fb9b6d08d1e16
```

**Memory Configuration:**

BSGS requires substantial RAM. Configure based on your system:

```bash
# Let keyhunt auto-tune based on available RAM
./keyhunt -m bsgs -f pubkeys.txt -b 125

# Manual memory configuration
./keyhunt -m bsgs -f pubkeys.txt -b 125 -n 0x100000000 -k 128

# Parameters:
# -n : Number of baby steps (determines table size)
# -k : K factor (multiplier for search range coverage)
```

**Memory Requirements Formula:**

```
N = baby step count (-n parameter)
K = multiplier factor (-k parameter)
M = sqrt(search_range / K)

RAM = (M * 3.5 bytes)           # bloom filter 1
    + (M * 3.5 / 32 bytes)      # bloom filter 2
    + (M * 3.5 / 1024 bytes)    # bloom filter 3
    + (M / 32 * 16 bytes)       # bP table

Approximate: RAM ≈ 4 * sqrt(search_range) bytes for default settings
```

**Memory Examples:**

| Search Range | Approx. RAM Needed | Notes                    |
|--------------|-------------------|--------------------------|
| 2^60         | ~4 GB             | Fits most systems        |
| 2^70         | ~130 GB           | Requires workstation     |
| 2^80         | ~4 TB             | Requires specialized HW  |

**Saving/Loading BSGS Tables:**

Pre-computation is expensive. Save tables for reuse:

```bash
# First run: compute and save tables
./keyhunt -m bsgs -f pubkeys.txt -b 125 -S save_prefix

# Subsequent runs: load pre-computed tables
./keyhunt -m bsgs -f pubkeys.txt -b 125 -L save_prefix
```

### 3.2 Pollard's Kangaroo Algorithm

Pollard's Kangaroo (also called Lambda method) offers similar O(sqrt(N)) complexity with lower memory requirements.

**How It Works:**

1. Deploy "tame" kangaroos starting from known points
2. Deploy "wild" kangaroos starting from target point
3. Both types take pseudo-random "jumps"
4. When a wild kangaroo lands on a tame kangaroo's path, we can compute the discrete log

**Advantages over BSGS:**
- Much lower memory requirements (no large pre-computed table)
- Easily parallelizable
- Better for distributed computing (no shared state needed)

**Disadvantages:**
- Probabilistic (may need multiple runs)
- Slightly higher constant factor in time complexity

**Kangaroo Tools:**

Keyhunt focuses on BSGS. For Kangaroo, use Jean-Luc Pons' implementation:

```bash
# Jean-Luc Pons Kangaroo (separate tool)
# https://github.com/JeanLucPons/Kangaroo

./Kangaroo -t 8 -gpu -gpuId 0 -range 135 input.txt

# input.txt format:
# Public key in compressed or uncompressed format
```

**Historical Success: Puzzle #110 Solved**

Puzzle #110 (110-bit range) was solved in 2020 using Pollard's Kangaroo:
- Hardware: 256 Tesla V100 GPUs (distributed)
- Time: Approximately 2.1 days
- Operations: ~2^55 (sqrt of 2^110)

### 3.3 XPOINT Mode (Fastest for Known Public Keys)

When you have the public key, XPOINT mode is even faster than ADDRESS mode because it searches for X-coordinates directly:

```bash
# XPOINT mode (faster than address for known pubkeys)
./keyhunt -m xpoint -f targets/puzzle_xpoints.txt -b 125 -t 8 -R

# Target file contains X-coordinates (32 bytes hex):
# 145d2611c823a396ef6712ce0f712f09b9b4f3135e3e0aa3230fb9b6d08d1e16
```

**Why XPOINT is Faster:**
- Skips SHA256 computation
- Skips RIPEMD160 computation
- Skips Base58 encoding
- Only compares 32-byte X values

**Throughput Comparison:**

| Mode    | Operations/Key | Relative Speed |
|---------|----------------|----------------|
| ADDRESS | SHA256+RMD160+Base58 | 1x (baseline) |
| RMD160  | SHA256+RMD160  | 1.1x          |
| XPOINT  | Point multiply only | 2-3x          |

---

## 4. Pattern-Based Optimization

### Sequential Key Generation Analysis

Some hunters have theorized that puzzle private keys follow patterns because they were generated sequentially. However:

**Creator's Statement (2015):**
> "The keys are generated randomly using a cryptographically secure random number generator. There is no pattern."

**Analysis Results:**
- Statistical analysis of solved puzzles shows no detectable pattern
- Keys are uniformly distributed within their bit ranges
- No correlation between adjacent puzzle keys

### Sub-Range Focus Strategy

While there's no pattern, statistical approaches can be employed:

**Divide and Conquer:**
```bash
# Split 71-bit range into 1024 sub-ranges (10-bit division)
# Range: 2^70 to 2^71-1
# Each sub-range: (2^71 - 2^70) / 1024 = 2^60.5 keys

# Search first sub-range
./keyhunt -m address -f targets/71.txt -r 40000000000000000:40100000000000000 -l compress

# Different workers tackle different sub-ranges
```

**Community Pool Approach:**

Organized groups divide the keyspace:

1. Central coordinator assigns ranges to participants
2. Each participant reports searched ranges
3. Progress tracked publicly
4. Finder shares reward with pool

**Tools for Pool Coordination:**
- Custom web dashboards tracking searched ranges
- Bitcoin Talk forum coordination threads
- Discord/Telegram groups for real-time coordination

### Statistical Hot Spot Theory

Some researchers focus on ranges where keys are "statistically more likely":

```bash
# Focus on middle of bit range (most keys fall here)
# For 71-bit: most keys are closer to 2^70.5 than edges

# Calculate midpoint: (2^70 + 2^71) / 2 = 3 * 2^69
# Search around midpoint
./keyhunt -m address -f targets/71.txt -r 60000000000000000:70000000000000000 -l compress
```

**Warning:** This is mathematically unsound for truly random keys, but some hunters swear by it.

---

## 5. Transaction/ECDSA Vulnerabilities

### Nonce Reuse Attacks (k-value Reuse)

ECDSA signatures use a random nonce 'k'. If the same 'k' is used twice with the same private key:

```
s1 = k^(-1) * (z1 + r*d) mod n
s2 = k^(-1) * (z2 + r*d) mod n

Where:
- z1, z2 are message hashes
- r is the x-coordinate of k*G
- d is the private key

From two equations with same k and r:
d = (z1*s2 - z2*s1) / (r*(s1 - s2)) mod n
```

**Tools for Detection:**
```bash
# Scan blockchain for nonce reuse
# (External tools, not keyhunt)
python3 scan_for_reuse.py --blockchain bitcoin
```

### Weak RNG Exploitation

Some wallets have used weak random number generators:

- Android SecureRandom bug (2013)
- Blockchain.info k-value generation flaw (2014)
- Various hardware wallet vulnerabilities

**NOT Applicable to Puzzles:**

The puzzle creator confirmed using cryptographically secure generation. These vulnerabilities only apply to:
- Old wallet software
- Compromised implementations
- User-generated brainwallets

---

## 6. Brainwallet Dictionary Attacks

### What Are Brainwallets?

Brainwallets derive private keys from memorable phrases:

```
Private Key = SHA256("correct horse battery staple")
            = c4bbcb1fbec99d65bf59d85c8cb62ee2db963f0fe106f483d9afa73bd4e39a8a
```

### Why They're Vulnerable

- Humans choose predictable phrases
- Dictionary attacks are effective
- Precomputed rainbow tables exist

### Educational Examples (NOT for Puzzles)

```bash
# Generate addresses from wordlist (educational only)
while read phrase; do
    echo -n "$phrase" | sha256sum | cut -d' ' -f1
done < wordlist.txt > private_keys.txt

# Search for these addresses
./keyhunt -m address -f known_brainwallets.txt -i private_keys.txt
```

**Brainwallet Tools:**
- Brainflayer (fast GPU-based scanner)
- BTCRecover (wallet recovery tool)

### NOT Applicable to Bitcoin Puzzles

The puzzle private keys are:
- Randomly generated (not derived from phrases)
- Within specific bit ranges
- Not following any human-predictable pattern

Brainwallet attacks are only useful for:
- Finding old, abandoned brainwallets
- Educational/research purposes
- CTF challenges

---

## 7. Strategy Selection Matrix

### Decision Table: Choosing Your Approach

| Scenario | Public Key? | Best Tool | Best Mode | Complexity | Memory Needed |
|----------|-------------|-----------|-----------|------------|---------------|
| Puzzle #71 | No | keyhunt | ADDRESS | O(2^70) | Minimal |
| Puzzle #135 | **Yes** | keyhunt | **BSGS** | O(2^67.5) | ~100GB+ |
| Puzzle #135 | **Yes** | Kangaroo | Kangaroo | O(2^67.5) | ~1GB |
| Any puzzle w/o pubkey | No | keyhunt | ADDRESS | O(N) | Minimal |
| Any puzzle w/ pubkey | **Yes** | keyhunt/Kangaroo | BSGS/Kangaroo | O(sqrt(N)) | Variable |
| Known X-coordinate | **Yes** | keyhunt | XPOINT | O(N) but faster | Minimal |
| Brainwallet hunt | N/A | Brainflayer | Dictionary | O(dict_size) | Variable |

### Algorithm Comparison

| Algorithm | Time | Space | Parallelizable | Distributed | Best For |
|-----------|------|-------|----------------|-------------|----------|
| Brute Force | O(N) | O(1) | Easy | Easy | No public key |
| BSGS | O(sqrt(N)) | O(sqrt(N)) | Medium | Hard | Single machine, lots of RAM |
| Kangaroo | O(sqrt(N)) | O(1) | Easy | Easy | Distributed computing |
| XPOINT | O(N) | O(1) | Easy | Easy | Quick pubkey search |

### Flowchart: Choosing Your Strategy

```
Start
  |
  v
Is public key known?
  |
  +---> NO ---> Brute Force (ADDRESS or RMD160 mode)
  |              |
  |              v
  |             Range > 60 bits?
  |              |
  |              +---> YES ---> Consider pool/distributed approach
  |              |
  |              +---> NO ---> Solo search feasible
  |
  +---> YES ---> How much RAM available?
                  |
                  +---> < 16GB ---> Pollard's Kangaroo
                  |
                  +---> 16-64GB ---> BSGS (limited N)
                  |
                  +---> > 64GB ---> BSGS (optimal)
                  |
                  v
                 GPU available?
                  |
                  +---> YES ---> GPU Kangaroo (fastest)
                  |
                  +---> NO ---> CPU BSGS/Kangaroo
```

---

## 8. Realistic Time Estimates

### Puzzle #71 (NO Public Key - Pure Brute Force)

**Search Space:** 2^70 keys (from 2^70 to 2^71-1)

| Hardware | Throughput | Time to Search Full Range |
|----------|------------|---------------------------|
| 1 CPU (8-core, AVX2) | 50 Mkeys/s | 747,000 years |
| 10 CPUs coordinated | 500 Mkeys/s | 74,700 years |
| 1 GPU (RTX 3080) | 1 Gkeys/s | 37,350 years |
| 10 GPUs | 10 Gkeys/s | 3,735 years |
| 100 GPUs | 100 Gkeys/s | 373 years |
| 1000 GPUs | 1 Tkeys/s | 37 years |

**Mathematical Reality:**
```
Keys to search: 2^70 = 1,180,591,620,717,411,303,424
At 100 Mkeys/s: 1.18 * 10^22 / (10^8) = 1.18 * 10^14 seconds
              = 3.7 million years

Even at 100 Tkeys/s (unlikely): 3,735 years
```

**Probability of Early Find:**
- 50% chance after searching 50% of range
- 10% chance after searching 10% of range
- 1% chance after searching 1% of range (still ~7,500 years at 100 Gkeys/s)

### Puzzle #135 WITH Public Key (BSGS/Kangaroo)

**Effective Complexity:** O(sqrt(2^135)) = O(2^67.5)

| Method | Hardware | Throughput | Estimated Time |
|--------|----------|------------|----------------|
| BSGS | 256GB RAM, 32-core | ~100K ops/s | ~60,000 years |
| Kangaroo | 256 Tesla V100 | ~10B ops/s | ~600 years |
| Kangaroo | 1000 A100 GPUs | ~100B ops/s | ~60 years |

**Reference Point - Puzzle #110 Solution:**
- Solved with ~2^55 operations (sqrt of 2^110)
- 256 V100 GPUs took 2.1 days
- Puzzle #135 requires 2^12.5 more work = 5,700 times longer
- Estimated: 5,700 * 2.1 days = ~33 years with same hardware

### Cost Analysis

**Cloud Computing Estimate for Puzzle #135:**

```
Assuming AWS p4d.24xlarge (8x A100 GPUs):
- Cost: ~$32/hour
- Throughput: ~40 Gops/s per instance

Need: 2^67.5 operations
At 40 Gops/s: 2^67.5 / (40 * 10^9) = ~4.6 * 10^9 hours per instance

With 100 instances: 46 million hours of compute
Cost: 46,000,000 * $32 = $1.47 billion

Puzzle #135 bounty: ~$50,000 (at 1.35 BTC)
```

**Conclusion:** Puzzle #135 is economically infeasible with current technology.

### Feasibility Summary

| Puzzle | Bit Range | Public Key | Economically Feasible? | Notes |
|--------|-----------|------------|------------------------|-------|
| #71 | 71-bit | No | No | O(2^70) brute force required |
| #72-#100 | 72-100 bit | No | No | Exponentially harder |
| #135 | 135-bit | Yes | No | Even O(2^67.5) is too large |
| #140 | 140-bit | Yes | No | O(2^70) with pubkey |
| #145+ | 145+ bit | Yes | No | Beyond current technology |

---

## 9. Security Considerations

### Transaction Sniping (Front-Running)

When you find a puzzle private key and broadcast a transaction, sophisticated actors can:

1. **Mempool Monitoring:** Bots constantly scan the mempool for transactions
2. **Key Extraction:** They extract the public key from your signature
3. **Fee Replacement:** They create a competing transaction with higher fees
4. **Your coins are stolen**

**Historical Examples:**
- Puzzle #66 (2019): Original finder was sniped, recovered through negotiation
- Puzzle #69 (2019): Complex sniping battle, multiple competing transactions
- Puzzle #120 (2023): Finder used private relay to avoid sniping

### Protection Strategies

**1. Private Transaction Relay:**

Use services that relay directly to miners:
- Flashbots Protect (Ethereum, not Bitcoin)
- Mining pool direct submission
- Private mempool services

```bash
# Example: Direct submission to mining pool
# (Requires mining pool partnership)
curl -X POST https://miningpool.com/api/submit-tx \
  -H "Authorization: Bearer YOUR_API_KEY" \
  -d '{"tx": "0100000001..."}'
```

**2. Transaction Batching:**

Create a transaction that pays to a new address you control:

```bash
# Generate new destination address (keep private key SECURE)
# Create transaction: Puzzle_Address -> Your_New_Address
# Sign and broadcast through private channels
```

**3. RBF (Replace-By-Fee) Preparation:**

Prepare multiple transactions with escalating fees:

```bash
# Transaction 1: Normal fee (1 sat/vbyte)
# Transaction 2: High fee (50 sat/vbyte)
# Transaction 3: Very high fee (200 sat/vbyte)

# Broadcast TX1, monitor mempool, broadcast TX2/TX3 if sniping detected
```

**4. CPFP (Child Pays For Parent):**

Pre-create a child transaction that boosts parent priority:

```bash
# After broadcasting main TX, immediately broadcast child TX
# Child TX spends from main TX output with high fee
# Total fee/vbyte increases, improving confirmation priority
```

### MEV (Miner Extractable Value) Protection

While MEV is more prevalent in Ethereum, Bitcoin miners can also:
- Reorder transactions within their blocks
- Include their own competing transactions
- Collude with mempool observers

**Mitigation:**
- Build relationships with multiple mining pools
- Use Stratum V2 job declaration (when available)
- Consider timing attacks (broadcast during high-volume periods)

### Operational Security

**Before searching:**
- Use dedicated hardware (not your main computer)
- Isolate network traffic (VPN, Tor)
- Prepare destination addresses in advance
- Test with small amounts first

**When you find a key:**
- Do NOT post about it publicly
- Do NOT wait before claiming
- Have transaction ready to broadcast
- Use multiple broadcast methods simultaneously

**After claiming:**
- Move funds through multiple hops
- Consider privacy-preserving techniques
- Secure your tax records (cryptocurrency gains are taxable)

---

## Appendix A: Keyhunt Command Reference

### ADDRESS Mode (Brute Force)
```bash
./keyhunt -m address -f <targets.txt> -b <bits> [options]

Options:
  -t <n>       : Number of threads
  -l compress  : Compressed keys only (2x faster)
  -R           : Random starting points
  -r <start:end> : Specific range in hex
  -s <seconds> : Status update interval
  -q           : Quiet mode
```

### RMD160 Mode (Hash Search)
```bash
./keyhunt -m rmd160 -f <targets.rmd> -b <bits> [options]
# Same options as ADDRESS mode
# Target file: 40-character hex RIPEMD160 hashes
```

### XPOINT Mode (X-Coordinate Search)
```bash
./keyhunt -m xpoint -f <targets.txt> -b <bits> [options]
# Same options as ADDRESS mode
# Target file: 64-character hex X-coordinates
```

### BSGS Mode (Baby Step Giant Step)
```bash
./keyhunt -m bsgs -f <pubkeys.txt> -b <bits> [options]

Options:
  -n <N>       : Baby step count (hex)
  -k <K>       : K factor multiplier
  -S <prefix>  : Save tables to files
  -L <prefix>  : Load tables from files
  -R           : Random range selection
```

### General Options
```bash
-o <file>    : Output results to file
-e           : Enable endomorphism (6x key check)
-v           : Verbose output
--help       : Display help message
```

---

## Appendix B: Additional Resources

### Tools
- **Keyhunt**: https://github.com/albertobsd/keyhunt
- **Kangaroo**: https://github.com/JeanLucPons/Kangaroo
- **BitCrack**: https://github.com/brichard19/BitCrack
- **VanitySearch**: https://github.com/JeanLucPons/VanitySearch

### Documentation
- Bitcoin Wiki - Secp256k1: https://en.bitcoin.it/wiki/Secp256k1
- ECDSA Security: https://bitcoin.stackexchange.com/questions/tagged/ecdsa

### Community
- Bitcoin Talk Puzzle Thread: https://bitcointalk.org/index.php?topic=1306983.0
- Puzzle Tracking: https://privatekeys.pw/puzzles/bitcoin-puzzle-tx

### Academic Papers
- "A Note on Shanks' Algorithm" - Original BSGS paper
- "Monte Carlo Methods for Index Computation" - Pollard's Kangaroo
- "Faster Batch Verification" - Optimizations for ECDSA

---

*Last updated: January 2026*
*Keyhunt Version: Latest*

**Disclaimer:** This guide is for educational purposes. Always ensure you have legal rights to any private keys you attempt to recover. The Bitcoin puzzles are specifically designed as public challenges with bounties for successful solvers.
