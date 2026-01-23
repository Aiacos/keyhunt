# Installation Guide

This guide covers system requirements and build instructions for keyhunt.

## System Requirements

### Operating System
- **Linux** (Ubuntu 20.04+, Fedora 35+, Debian 11+ recommended)
- macOS support is experimental
- Windows requires WSL2

### Compiler
- **GCC 8+** or **Clang 10+** with C++17 support
- GCC 11+ recommended for best AVX-512 support

### CPU Features (auto-detected)
| Feature | Benefit | Detection |
|---------|---------|-----------|
| SSE2 | Baseline SIMD (required) | Automatic |
| AVX2 | 8-way parallel hashing (2x faster) | Automatic |
| AVX-512 | 16-way parallel hashing (cutting-edge) | Automatic |
| SHA-NI | Hardware SHA256 acceleration | Automatic |

### Memory Requirements
| Mode | Minimum RAM | Recommended RAM |
|------|-------------|-----------------|
| ADDRESS | 2 GB | 4 GB |
| BSGS | 8 GB | 32 GB+ |
| XPOINT | 2 GB | 4 GB |
| Distributed Server | 4 GB | 16 GB |

### Optional: GPU Support
- NVIDIA GPU with CUDA 11.0+
- Compute Capability 5.0+ (Maxwell or newer)
- NVIDIA driver 450.0+

## Building from Source

### Standard Build

```bash
# Clone the repository
git clone https://github.com/albertobsd/keyhunt.git
cd keyhunt

# Build with auto-detected optimizations
make clean && make
```

The build system automatically:
1. Detects CPU features (AVX2, AVX-512, SHA-NI)
2. Compiles optimized code paths for your hardware
3. Enables Link-Time Optimization (LTO)

### Build Options

```bash
# Standard build (recommended)
make

# Legacy build (requires libssl-dev, libgmp-dev)
make legacy

# BSGS daemon variant
make bsgsd

# Clean build artifacts
make clean
```

### Legacy Build Dependencies

The legacy build requires additional libraries:

```bash
# Ubuntu/Debian
sudo apt install libssl-dev libgmp-dev

# Fedora
sudo dnf install openssl-devel gmp-devel

# Then build
make legacy
```

## Verifying Installation

After building, verify the installation works:

```bash
# Check version and detected features
./keyhunt --version

# Quick functionality test (finds keys 1-32)
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -t 4
```

Expected output should show:
- Detected CPU features (AVX2, SHA-NI, etc.)
- Found private keys for the test addresses

## Performance Benchmark

Run the built-in benchmark to see your system's performance:

```bash
./keyhunt --benchmark
```

This tests:
- CPU hashing speed per thread
- GPU performance (if available)
- Hybrid CPU+GPU mode
- Recommended settings for your hardware

## Installation Locations

Keyhunt creates the following directories:

| Location | Purpose |
|----------|---------|
| `~/.keyhunt/` | User configuration and data |
| `~/.keyhunt/progress/` | Search progress persistence |
| `~/.keyhunt/bloom/` | Cached bloom filter tables |
| `./keyhunt_wizard.json` | Wizard configuration |

## Troubleshooting Build Issues

### "AVX2 not detected"

Check if your CPU supports AVX2:
```bash
grep avx2 /proc/cpuinfo
```

If supported but not detected, ensure you're using GCC 8+ or Clang 10+.

### "Undefined reference" errors

Usually indicates missing dependencies for legacy build:
```bash
sudo apt install libssl-dev libgmp-dev build-essential
```

### Compilation warnings

The standard build uses `-O2` optimization. Do NOT use `-Ofast` as it causes system freezes on some Ubuntu systems.

### Out of Memory during build

Reduce parallel compilation:
```bash
make -j2   # Use only 2 parallel jobs
```

## Updating

To update to the latest version:

```bash
cd keyhunt
git pull origin main
make clean && make
```

**Note**: Saved bloom filter tables (`.blm` files) may need regeneration after major updates.

## Next Steps

- [Quick Start](quick-start.md) - Run your first search
- [Basic Usage](basic-usage.md) - Learn command-line options
- [CPU Tuning](../optimization/cpu-tuning.md) - Optimize for your hardware
