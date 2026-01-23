# Keyhunt Documentation Wiki

Welcome to the comprehensive documentation for keyhunt, a high-performance cryptocurrency private key search tool for secp256k1-based cryptocurrencies.

## Documentation Structure

### Getting Started
| Document | Description |
|----------|-------------|
| [Installation](getting-started/installation.md) | System requirements and build instructions |
| [Quick Start](getting-started/quick-start.md) | Get running in 5 minutes |
| [Basic Usage](getting-started/basic-usage.md) | Understanding command-line options |

### Search Modes
| Document | Description |
|----------|-------------|
| [Address Mode](modes/address-mode.md) | Bitcoin address brute-force search |
| [BSGS Mode](modes/bsgs-mode.md) | Baby-Step Giant-Step algorithm for known public keys |
| [XPoint Mode](modes/xpoint-mode.md) | X-coordinate search for public keys |
| [RMD160 Mode](modes/rmd160-mode.md) | Direct RIPEMD160 hash search |
| [Vanity Mode](modes/vanity-mode.md) | Generate custom address prefixes |

### Distributed Computing
| Document | Description |
|----------|-------------|
| [Overview](distributed/overview.md) | Multi-machine distributed search architecture |
| [Server Setup](distributed/server-setup.md) | Configure coordinator server |
| [Client Setup](distributed/client-setup.md) | Connect worker clients |
| [Protocol](distributed/protocol.md) | TCP+JSON communication protocol |
| [Troubleshooting](distributed/troubleshooting.md) | Common issues and solutions |

### Interactive Wizard
| Document | Description |
|----------|-------------|
| [Wizard Guide](wizard/wizard-guide.md) | Step-by-step wizard walkthrough |
| [Configuration](wizard/configuration.md) | Configuration file format and options |

### Performance Optimization
| Document | Description |
|----------|-------------|
| [CPU Tuning](optimization/cpu-tuning.md) | SIMD, threads, and batch size optimization |
| [GPU Setup](optimization/gpu-setup.md) | CUDA configuration and modes |
| [Hybrid Mode](optimization/hybrid-mode.md) | Combined CPU+GPU operation |
| [Memory Optimization](optimization/memory-optimization.md) | BSGS memory management |

### Development
| Document | Description |
|----------|-------------|
| [Architecture](development/architecture.md) | Codebase structure and design |
| [Building](development/building.md) | Compilation and build system |
| [Testing](development/testing.md) | Test suite and validation |
| [Contributing](development/contributing.md) | How to contribute to keyhunt |

### Reference
| Document | Description |
|----------|-------------|
| [CLI Options](reference/cli-options.md) | Complete command-line reference |
| [Configuration Files](reference/configuration-files.md) | File formats and locations |
| [FAQ](reference/faq.md) | Frequently asked questions |

## Quick Navigation

### I want to...

| Goal | Go to |
|------|-------|
| Build and install keyhunt | [Installation](getting-started/installation.md) |
| Run my first search | [Quick Start](getting-started/quick-start.md) |
| Search for a Bitcoin address | [Address Mode](modes/address-mode.md) |
| Use BSGS with a known public key | [BSGS Mode](modes/bsgs-mode.md) |
| Set up multi-PC distributed search | [Distributed Overview](distributed/overview.md) |
| Use the interactive wizard | [Wizard Guide](wizard/wizard-guide.md) |
| Optimize for my hardware | [CPU Tuning](optimization/cpu-tuning.md) |
| Enable GPU acceleration | [GPU Setup](optimization/gpu-setup.md) |
| Understand the codebase | [Architecture](development/architecture.md) |

## Bitcoin Puzzle Context

Keyhunt is commonly used for solving Bitcoin puzzle challenges. Current status (2025):

| Puzzle | Bits | Status | Strategy |
|--------|------|--------|----------|
| 1-70 | 1-70 | Solved | Brute force |
| 71 | 71 | Active | ADDRESS mode (no public key) |
| 72-134 | 72-134 | Unsolved | No public key exposed |
| 135 | 135 | Recommended | BSGS (public key exposed) |

**Key Insight**: Puzzles with exposed public keys can be solved with O(sqrt(N)) complexity using BSGS, vastly more efficient than O(N) brute force.

## Performance Benchmarks

Run the built-in benchmark to see your system's capabilities:

```bash
./keyhunt --benchmark
```

Typical performance on modern hardware:
- **ADDRESS mode**: 50-100 Mkeys/s (AVX2), up to 400 Mkeys/s (GPU hybrid)
- **BSGS mode**: Depends on table size, but sqrt(N) complexity

## Additional Resources

- [Main README](../../README.md) - Project overview and examples
- [CLAUDE.md](../../CLAUDE.md) - AI assistant guidance for development
- [Archived Plans](../archive/) - Historical development plans

---

*This wiki provides comprehensive documentation for the keyhunt tool. For bug reports and feature requests, please use the GitHub issue tracker.*
