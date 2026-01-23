# Architecture Overview

This document describes the keyhunt codebase structure and design.

## High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                           KEYHUNT                                   │
├─────────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌────────────┐ │
│  │   CLI/UI    │  │   Wizard    │  │ Distributed │  │  Benchmark │ │
│  │  (cli.cpp)  │  │ (wizard/)   │  │ (server/    │  │(benchmark.)│ │
│  │             │  │             │  │  client)    │  │            │ │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └──────┬─────┘ │
│         │                │                │                │       │
│         └────────────────┴────────────────┴────────────────┘       │
│                                   │                                 │
│  ┌────────────────────────────────▼────────────────────────────┐   │
│  │                     SEARCH ENGINE (keyhunt.cpp)             │   │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐       │   │
│  │  │ ADDRESS  │ │   BSGS   │ │  XPOINT  │ │  VANITY  │  ...  │   │
│  │  │   Mode   │ │   Mode   │ │   Mode   │ │   Mode   │       │   │
│  │  └──────────┘ └──────────┘ └──────────┘ └──────────┘       │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                   │                                 │
│  ┌────────────────────────────────▼────────────────────────────┐   │
│  │                    CRYPTOGRAPHIC LAYER                      │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐  │   │
│  │  │  secp256k1/  │  │    hash/     │  │     bloom/       │  │   │
│  │  │ (EC math)    │  │ (SHA/RMD160) │  │ (Bloom filters)  │  │   │
│  │  └──────────────┘  └──────────────┘  └──────────────────┘  │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                   │                                 │
│  ┌────────────────────────────────▼────────────────────────────┐   │
│  │                    HARDWARE ABSTRACTION                     │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐  │   │
│  │  │   sysinfo    │  │  cpu_tuning  │  │    gpu/ (CUDA)   │  │   │
│  │  │(HW detect)   │  │(SIMD select) │  │ (GPU offload)    │  │   │
│  │  └──────────────┘  └──────────────┘  └──────────────────┘  │   │
│  └─────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

## Directory Structure

```
keyhunt/
├── keyhunt.cpp          # Main entry point and search modes
├── Makefile             # Build system
├── CLAUDE.md            # AI guidance document
│
├── secp256k1/           # Custom secp256k1 implementation
│   ├── Int.cpp/h        # 256-bit integer arithmetic
│   ├── Point.cpp/h      # EC point operations
│   ├── SECP256K1.cpp/h  # Main curve class
│   └── IntGroup.cpp/h   # Batch modular inversion
│
├── hash/                # Hash function implementations
│   ├── sha256.cpp       # Reference SHA256
│   ├── sha256_sse.cpp   # SSE2 optimized
│   ├── ripemd160.cpp    # Reference RIPEMD160
│   ├── ripemd160_sse.cpp    # SSE2 4-way
│   ├── ripemd160_avx2.cpp   # AVX2 8-way
│   └── ripemd160_avx512.cpp # AVX-512 16-way
│
├── bloom/               # Bloom filter implementation
│   ├── bloom.cpp/h      # New optimized bloom
│   └── bloom_test.cpp   # Bloom filter tests
│
├── oldbloom/            # Legacy bloom filter
│   └── bloom.cpp/h
│
├── wizard/              # Interactive wizard
│   ├── wizard.c/h       # Main wizard entry
│   ├── wizard_config.c  # Configuration/puzzles
│   ├── wizard_ui.c      # Terminal UI
│   ├── wizard_community.c  # Community integration
│   ├── wizard_server.c  # Server mode
│   └── wizard_client.c  # Client mode
│
├── distributed/         # Distributed computing
│   ├── protocol.h       # Message formats
│   ├── server.c         # Coordinator
│   └── client.c         # Worker
│
├── gpu/                 # GPU acceleration (CUDA)
│   ├── gpu_kernel.cu    # CUDA kernels
│   └── gpu_interface.cpp  # CPU-GPU bridge
│
├── src/                 # Modular components
│   ├── cli.cpp/h        # Command-line parsing
│   ├── output.cpp/h     # Colored output
│   ├── progress.cpp/h   # Progress persistence
│   └── benchmark.cpp/h  # Performance benchmark
│
├── tests/               # Test data files
│   ├── 1to32.txt        # Known test addresses
│   ├── 66.txt           # Puzzle 66 address
│   └── *.rmd            # RMD160 hash files
│
└── docs/                # Documentation
    ├── wiki/            # Wiki documentation
    └── archive/         # Historical plans
```

## Core Components

### keyhunt.cpp

The main file (~3000 lines) containing:
- Argument parsing and initialization
- Search mode implementations
- Multi-threaded search loop
- Result handling

Key functions:
```cpp
int main(int argc, char **argv);
void *thread_process(void *arg);      // Worker thread
void init_generator();                 // Initialize EC generator
void checkAddress(Int *privateKey);    // ADDRESS mode check
void checkBsgs(Int *privateKey);       // BSGS mode check
```

### secp256k1/ - Elliptic Curve Math

Custom implementation without external dependencies.

**Int.cpp/h** - 256-bit integer:
```cpp
class Int {
    uint64_t bits64[5];  // 320 bits for overflow handling

    void Add(Int *a);
    void Sub(Int *a);
    void Mult(Int *a);
    void ModInv();       // Modular inverse (expensive)
};
```

**Point.cpp/h** - EC point:
```cpp
class Point {
    Int x, y, z;  // Jacobian coordinates

    bool isZero();
    void Reduce();  // Convert to affine
};
```

**SECP256K1.cpp/h** - Curve operations:
```cpp
class SECP256K1 {
    Point G;           // Generator point
    Point GTable[256]; // Pre-computed multiples

    Point ComputePublicKey(Int *privateKey);
    void GetHash160(int type, Point &pubKey, uint8_t *hash);
    void GetHash160_AVX2(...);  // SIMD version
};
```

**IntGroup.cpp/h** - Batch inversion:
```cpp
class IntGroup {
    // Montgomery's trick: invert N numbers with N+1 multiplications
    void ModInv(Int *a, Int *b, int n);
};
```

### hash/ - SIMD Hash Implementations

**ripemd160_avx2.cpp** example:
```cpp
// Process 8 messages in parallel using AVX2
void ripemd160_avx2(
    const uint8_t *msg[8],  // 8 input messages
    uint8_t *hash[8]         // 8 output hashes
);

// Internal: 8-way parallel round function
static inline __m256i F(__m256i x, __m256i y, __m256i z) {
    return _mm256_xor_si256(x, _mm256_xor_si256(y, z));
}
```

### bloom/ - Bloom Filters

3-tier hierarchy for fast negative lookups:

```cpp
class Bloom {
    uint8_t *bf1;   // Primary bloom filter
    uint8_t *bf2;   // Secondary (1/32 size)
    uint8_t *bf3;   // Tertiary (1/1024 size)

    bool check(uint8_t *hash160);
    void add(uint8_t *hash160);
};
```

### wizard/ - Interactive Setup

State machine for guided configuration:

```c
typedef struct {
    wizard_step_t current_step;
    wizard_mode_t mode;
    puzzle_info_t puzzle;
    server_config_t server;
    client_config_t client;
    search_config_t search;
} wizard_state_t;

void wizard_run(wizard_state_t *state);
```

### distributed/ - Coordination Layer

Protocol messages defined in protocol.h:

```c
typedef struct {
    msg_type_t type;
    char client_name[64];
    union {
        register_msg_t reg;
        work_unit_msg_t work;
        progress_msg_t progress;
        key_found_msg_t found;
    } payload;
} protocol_msg_t;
```

## Data Flow

### Address Mode Pipeline

```
Private Key (256 bits)
       │
       ▼
┌──────────────────┐
│  EC Point Mult   │  secp256k1/SECP256K1.cpp
│  P = k * G       │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ Serialize PubKey │  33 or 65 bytes
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│    SHA256        │  hash/sha256.cpp
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│   RIPEMD160      │  hash/ripemd160_avx2.cpp (8 parallel)
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│  Bloom Filter    │  bloom/bloom.cpp
│     Check        │
└────────┬─────────┘
         │
    Match?
   ┌──┴──┐
   │     │
  Yes    No
   │     │
   ▼     ▼
Binary  Continue
Search
```

### BSGS Pipeline

```
              Setup Phase
┌─────────────────────────────────────────┐
│  For i = 0 to M-1:                      │
│    P_i = i * G                          │
│    Store hash(P_i.x) in bloom filter    │
│    Store P_i.x in lookup table          │
└─────────────────────────────────────────┘

              Search Phase
┌─────────────────────────────────────────┐
│  For j = 0 to K*M:                      │
│    Q = Target - j*M*G                   │
│    If hash(Q.x) in bloom filter:        │
│      Lookup i from table                │
│      Key = i + j*M                      │
└─────────────────────────────────────────┘
```

## Threading Model

```
┌─────────────────────────────────────────────────────────────────┐
│                         Main Thread                             │
│  - Parse arguments                                              │
│  - Initialize bloom filters, EC tables                          │
│  - Spawn worker threads                                         │
│  - Collect results                                              │
│  - Handle signals                                               │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ pthread_create()
                              ▼
    ┌─────────────┬─────────────┬─────────────┬─────────────┐
    │  Worker 0   │  Worker 1   │  Worker 2   │  Worker N   │
    │             │             │             │             │
    │ Range: 0-X  │ Range: X-2X │ Range: 2X-3X│ Range: ...  │
    │             │             │             │             │
    │ Local batch │ Local batch │ Local batch │ Local batch │
    └──────┬──────┴──────┬──────┴──────┬──────┴──────┬──────┘
           │             │             │             │
           └─────────────┴──────┬──────┴─────────────┘
                                │
                                ▼
                      Shared results queue
                      (mutex protected)
```

## Memory Layout

### BSGS Memory Map

```
Address Space:
┌────────────────────────────────────────┐ High
│       Stack (per thread)               │
├────────────────────────────────────────┤
│       bP Table (baby step points)      │ M/32 * K * 16 bytes
├────────────────────────────────────────┤
│       Bloom Filter 1 (primary)         │ M * K * 3.5 bytes
├────────────────────────────────────────┤
│       Bloom Filter 2 (secondary)       │ M * K * 3.5 / 32 bytes
├────────────────────────────────────────┤
│       Bloom Filter 3 (tertiary)        │ M * K * 3.5 / 1024 bytes
├────────────────────────────────────────┤
│       EC Pre-computed Tables           │ ~10 MB
├────────────────────────────────────────┤
│       Code + BSS                       │ ~2 MB
└────────────────────────────────────────┘ Low
```

## Build Variants

### Standard Build

```makefile
keyhunt: keyhunt.o secp256k1/*.o hash/*.o bloom/*.o
    $(CXX) $(CXXFLAGS) -o $@ $^ -lpthread
```

Uses custom secp256k1, no external dependencies.

### Legacy Build

```makefile
keyhunt-legacy: keyhunt.o gmp256k1/*.o hash/*.o bloom/*.o
    $(CXX) $(CXXFLAGS) -o $@ $^ -lgmp -lssl -lcrypto -lpthread
```

Uses GMP and OpenSSL.

### GPU Build

```makefile
keyhunt-gpu: keyhunt.o secp256k1/*.o hash/*.o gpu/*.o
    $(NVCC) -o $@ $^ -lcudart -lpthread
```

Adds CUDA kernel compilation.

## See Also

- [Building](building.md) - Compilation details
- [Testing](testing.md) - Test suite
- [Contributing](contributing.md) - Contribution guidelines
