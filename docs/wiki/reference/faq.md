# Frequently Asked Questions

Common questions and answers about keyhunt.

## General Questions

### What is keyhunt?

Keyhunt is a high-performance tool for searching Bitcoin private keys. It's commonly used for solving Bitcoin puzzles (challenge transactions with known address or public key) and educational purposes in cryptography.

### Is this legal?

Keyhunt is legal to use for:
- Solving Bitcoin puzzles (challenge transactions)
- Recovering your own lost keys
- Educational purposes
- Security research

It is NOT legal to:
- Attempt to steal funds from addresses you don't own
- Use for any malicious purposes

### Can keyhunt crack any Bitcoin address?

**No.** Bitcoin's security is based on the difficulty of the discrete logarithm problem on elliptic curves. For a random 256-bit key:

- Searching all 2^256 possibilities would take longer than the age of the universe
- Even with all computers on Earth working together
- There is no mathematical shortcut that breaks this

Keyhunt can only find keys in **specific, small ranges** (like puzzles) or when you have additional information (like the public key).

### What are Bitcoin puzzles?

Bitcoin puzzles are challenge transactions created with private keys in known bit ranges. For example, Puzzle 66 has a private key between 2^65 and 2^66. They were created as challenges for the community and have BTC rewards.

## Mode Questions

### When should I use BSGS mode?

Use BSGS when:
- You have the **public key** (exposed from blockchain)
- The range is manageable (up to ~2^70 bits effective search)
- You have sufficient RAM

BSGS uses O(sqrt(N)) complexity instead of O(N), making it vastly faster.

### When should I use ADDRESS mode?

Use ADDRESS when:
- You only have the Bitcoin address, not the public key
- No BSGS advantage is possible

### What's the difference between compressed and uncompressed keys?

| Type | Public Key Size | Prefix | Usage |
|------|-----------------|--------|-------|
| Compressed | 33 bytes | 02/03 | Modern wallets (95%+) |
| Uncompressed | 65 bytes | 04 | Legacy (pre-2012) |

Use `-l compress` for most searches (2x faster than checking both).

### Should I use random mode (-R)?

Use random mode when:
- The range is too large to search sequentially
- You want statistical coverage
- Multiple people are searching the same range

Use sequential mode when:
- You need guaranteed coverage
- The range is small enough to complete

## Performance Questions

### How do I maximize speed?

1. **Use all threads**: `-t $(nproc)`
2. **Enable GPU**: `-G hybrid`
3. **Use compressed only**: `-l compress`
4. **Enable quiet mode**: `-q`
5. **Run benchmark first**: `--benchmark`

### What speed should I expect?

Typical speeds on modern hardware:

| Mode | CPU Only | GPU Hybrid |
|------|----------|------------|
| ADDRESS | 50-100 Mkeys/s | 200-500 Mkeys/s |
| BSGS | Memory-dependent | N/A |
| XPOINT | 80-120 Mkeys/s | N/A |

### Why is my speed low?

Check:
1. SIMD detection at startup (AVX2, SHA-NI)
2. CPU frequency scaling (`cpupower frequency-set -g performance`)
3. Thermal throttling (`sensors`)
4. Build optimization (`make clean && make`)

### How long will the search take?

Calculate:
```
Range size / Speed = Time

Example:
2^66 range = 73,786,976,294,838,206,464 keys
At 100 Mkeys/s = 738 billion seconds = 23,400 years
```

This is why large puzzles require massive parallelization or luck.

## Memory Questions

### How much RAM do I need?

| Mode | RAM |
|------|-----|
| ADDRESS | 2-4 GB |
| BSGS | 8-64+ GB (depends on N) |
| XPOINT | 2-4 GB |
| Distributed client | 4-8 GB |

### "Not enough memory for BSGS"

Options:
1. Reduce N value: `-n 0x10000000000`
2. Use swap space (slower)
3. Use distributed mode
4. Use ADDRESS or XPOINT instead

### Can I use swap for BSGS?

Yes, but it's slow:
1. NVMe SSD: Moderate slowdown
2. SATA SSD: Significant slowdown
3. HDD: Not recommended

## Distributed Questions

### How do I set up distributed search?

Easiest way:
```bash
# Server
./keyhunt --wizard  # Select "Server"

# Clients
./keyhunt --wizard  # Select "Client", enter server IP
```

### Can I use different hardware on different clients?

Yes! Each client auto-detects its hardware and reports capabilities to the server. Mix of CPU-only and GPU clients is fine.

### What happens if a client crashes?

The server tracks work unit assignments. After timeout (10 minutes by default), work is reassigned to another client.

### Is distributed mode secure?

By default, communication is unencrypted. For untrusted networks:
1. Use VPN (WireGuard, OpenVPN)
2. Use SSH tunnel
3. Firewall server to trusted IPs

## Troubleshooting Questions

### "Key not found" - am I doing something wrong?

Probably not. Bitcoin puzzles are extremely difficult:
- 2^66 range = ~74 quintillion keys
- At 100 Mkeys/s = 23,400 years
- Many people searching = still very difficult

Finding a key requires either:
- Luck (random mode)
- Complete coverage of range
- Team effort over time

### Keyhunt crashes on start

Check:
1. Enough RAM for mode
2. Target file exists and is readable
3. Valid range specification
4. Built correctly (`make clean && make`)

### "AVX2 not detected" but my CPU supports it

Check:
1. Kernel support: `grep avx2 /proc/cpuinfo`
2. Compiler version: GCC 8+ or Clang 10+
3. Rebuild: `make clean && make`

### Progress lost after restart

Use checkpoint files:
- Distributed: `--checkpoint-interval 60`
- BSGS: `-S` (saves tables)
- Progress auto-saved to `~/.keyhunt/progress/`

## Technical Questions

### What algorithm does keyhunt use?

- **ADDRESS**: Brute force with bloom filter optimization
- **BSGS**: Baby-Step Giant-Step (Shanks' algorithm)
- **XPOINT**: Direct X-coordinate comparison
- **VANITY**: Random generation with prefix matching

### How does BSGS achieve sqrt(N) complexity?

1. Pre-compute sqrt(N) "baby steps": i*G for i = 0 to sqrt(N)
2. Store in lookup table
3. For each "giant step" j, compute Target - j*sqrt(N)*G
4. Check if result matches any baby step
5. If match: key = i + j*sqrt(N)

### Why is RIPEMD160 the bottleneck?

The key derivation pipeline:
```
Private Key -> EC Mult -> SHA256 -> RIPEMD160 -> Address
```

- EC Mult: Optimized with precomputation
- SHA256: Has hardware acceleration (SHA-NI)
- RIPEMD160: No hardware acceleration, must use SIMD

RIPEMD160 takes ~60% of total computation time.

### What is the endomorphism optimization?

secp256k1 has an efficient endomorphism that maps:
```
(x, y) -> (beta*x, y)
```

This allows checking 6 related keys per computation. Only useful for full-curve searches, not puzzle bit-ranges.

## Puzzle-Specific Questions

### Which puzzle should I target?

| Puzzle | Public Key | Strategy | Feasibility |
|--------|------------|----------|-------------|
| 66-70 | No | ADDRESS, random | Very difficult |
| 71+ | No | ADDRESS, random | Extremely difficult |
| 135+ | Yes | BSGS | Difficult but tractable |

### Has anyone solved puzzles using keyhunt?

Yes, keyhunt and similar tools have been used to solve lower puzzles. However, the difficulty increases exponentially with each puzzle.

### How much can I earn from puzzles?

Puzzle rewards range from a few BTC to over 10 BTC. However:
- Competition is fierce
- Electricity costs can exceed expected value
- No guarantee of success

Consider it a challenge/lottery, not a reliable income.

## Community Questions

### Where can I get help?

1. GitHub Issues
2. Bitcoin puzzle forums
3. Documentation wiki

### How can I contribute?

See [Contributing Guide](../development/contributing.md):
- Bug fixes
- Performance improvements
- Documentation
- Testing

## See Also

- [Quick Start](../getting-started/quick-start.md) - Getting started
- [CLI Options](cli-options.md) - Command reference
- [Troubleshooting](../distributed/troubleshooting.md) - Problem solving
