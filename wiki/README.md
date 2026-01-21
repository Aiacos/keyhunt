# Keyhunt Wiki

Welcome to the keyhunt documentation wiki. This wiki contains comprehensive guides for solving Bitcoin cryptographic puzzles using the keyhunt tool.

## Table of Contents

| Document | Description |
|----------|-------------|
| [00-Quick-Start.md](00-Quick-Start.md) | Get started in 5 minutes - build, test, basic usage |
| [01-Strategy-Guide.md](01-Strategy-Guide.md) | Complete strategy guide for Bitcoin puzzles |
| [02-Algorithm-Reference.md](02-Algorithm-Reference.md) | Technical reference for all algorithms |
| [03-Distributed-Computing.md](03-Distributed-Computing.md) | Multi-PC distributed search setup |

## Quick Navigation

### I want to...

| Goal | Go to |
|------|-------|
| Build and test keyhunt | [Quick Start](00-Quick-Start.md#build) |
| Choose the right search strategy | [Strategy Guide - Selection Matrix](01-Strategy-Guide.md#7-strategy-selection-matrix) |
| Understand BSGS algorithm | [Algorithm Reference - BSGS](02-Algorithm-Reference.md#4-bsgs-algorithm-details) |
| Set up distributed search | [Distributed Computing - Setup](03-Distributed-Computing.md#setup-instructions) |
| Know realistic time estimates | [Strategy Guide - Time Estimates](01-Strategy-Guide.md#8-realistic-time-estimates) |

## Bitcoin Puzzle Status

Current status (as of 2025):

| Puzzle | Bits | Status | Notes |
|--------|------|--------|-------|
| #1-70 | 1-70 | Solved | Brute force |
| #71 | 71 | **Active Target** | No public key, ~720K years/CPU |
| #72-134 | 72-134 | Unsolved | No public key exposed |
| #135 | 135 | **Recommended** | Public key exposed! O(sqrt(N)) |
| #136-160 | 136-160 | Unsolved | Every 5th has exposed pubkey |

## Key Insight

> **Puzzles with exposed public keys** (like #135) can be solved with O(sqrt(N)) complexity using BSGS or Kangaroo algorithms. This is vastly more efficient than brute force O(N).
>
> For puzzle #135 (135-bit range), sqrt(2^135) = 2^67.5 operations instead of 2^135.

## Tools Comparison

| Tool | Best For | Algorithm | GPU Support |
|------|----------|-----------|-------------|
| **keyhunt** | ADDRESS/BSGS/XPOINT | Multiple | CUDA (hybrid) |
| **Kangaroo** | Exposed pubkeys | Pollard's Kangaroo | CUDA |
| **BitCrack** | Brute force | Linear search | CUDA/OpenCL |

## Resources

- [Bitcoin Puzzle Challenge](https://privatekeys.pw/puzzles/bitcoin-puzzle-tx)
- [BTC Puzzle Info](https://btcpuzzle.info/)
- [Kangaroo by Jean-Luc Pons](https://github.com/JeanLucPons/Kangaroo)
- [keyhunt GitHub](https://github.com/albertobsd/keyhunt)

## Contributing

Found an error or want to improve the documentation? Contributions are welcome!

---

*This wiki was generated to provide comprehensive documentation for Bitcoin puzzle solving strategies and the keyhunt tool.*
