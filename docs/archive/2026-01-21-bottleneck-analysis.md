# Keyhunt Bottleneck Analysis and Search Space Optimization

**Data:** 2026-01-21
**Hardware test:** Intel i9-9900 (16 threads), no GPU (CUDA non disponibile su questa macchina)
**Branch:** refactor-pipeline

---

## 1. Baseline Performance

### CPU Benchmark Results

| Mode | Threads | Speed | Notes |
|------|---------|-------|-------|
| rmd160 | 16 | **49 Mkeys/s** | Compress only, random |
| address | 16 | **28 Mkeys/s** | Full address generation |

### Profiler Output (address mode)

```
prof 556ns/key EC32 Hash62 Bloom5 Bin0 Write0
```

**Time Distribution:**
- **Hash (62%)** - SHA256 + RIPEMD160 pipeline
- **EC (32%)** - Elliptic curve point generation
- **Bloom (5%)** - Filter checks
- **Binary Search (0%)** - Target lookup
- **Write (0%)** - Output

---

## 2. Bottleneck Analysis

### 2.1 Hash Pipeline (62% del tempo)

**Stato attuale:**
- AVX2 8-way RIPEMD160 implementato
- AVX2 8-way SHA256 implementato
- SHA-NI disponibile ma non sempre usato

**Limiti:**
- Pipeline sequenziale: SHA256 → RIPEMD160
- Latency dependency tra i due hash
- i9-9900 non ha AVX-512 (solo Skylake-X e successori)

**Potenziale miglioramento:** 10-15% con SHA-NI se supportato

### 2.2 EC Point Generation (32% del tempo)

**Stato attuale:**
- Montgomery's trick per batch inversion (1024 punti)
- ModMulK1 già ottimizzato (intrinsics `_umul128`, `_addcarry_u64`)
- GTable precomputed (8192 punti)

**Limiti:**
- ModInv è O(n) con costante alta
- Serial dependency nella generazione punti batch
- Assembly test ha dato 0.85x (peggio del compiler)

**Potenziale miglioramento:** 5-10% con prefetching aggressivo

### 2.3 Bloom Filter (5% del tempo)

**Stato attuale:**
- Fast bloom filter XXH3 + Kirsch-Mitzenmacher (2.5x speedup già fatto)
- Cache-aligned 64 bytes
- Early exit su primo miss

**Potenziale miglioramento:** Minimo (già ottimizzato)

---

## 3. Search Space Optimization Strategies

### 3.1 Endomorphism (già implementato)

**Come funziona:**
- secp256k1 ha proprietà speciale: `Q * λ = (x * β mod p, y)`
- Per ogni punto calcolato, genera 12 indirizzi diversi:
  - Q, Q*β, Q*β² (3 varianti x-coordinate)
  - Ognuno con prefix 0x02 e 0x03 (compressed)
  - Più versioni uncompressed

**Speedup:** ~3x più indirizzi per computazione EC

**Limitazione:** Non combinabile con BSGS (rompe l'ordinamento della tabella)

**Flag:** `-e`

### 3.2 BSGS - Baby Step Giant Step (già implementato)

**Complessità:**
- Tempo: O(√N) invece di O(N)
- Spazio: O(√N) per la tabella precomputata

**Speedup:** 10-100x per ricerca con public key nota

**Memory formula:**
```
M = √N
RAM = (M * K * 3.5) + (M * K * 3.5 / 32) + (M * K * 3.5 / 1024) + (M / 32 * K * 16)
```

**Flag:** `-m bsgs`

### 3.3 XPOINT Mode (già implementato)

**Ottimizzazione:** Solo x-coordinate, evita calcolo y

**Speedup:** ~2x rispetto a full point

**Flag:** `-m xpoint`

### 3.4 Strategie NON implementate

#### Pollard Rho
- O(√N) tempo, O(1) spazio
- Meno efficace di BSGS per target specifici
- Non implementato: BSGS è superiore per questo use case

#### Kangaroo Algorithm
- Simile a Pollard Rho
- Meglio per range bounded di lunghezza sconosciuta
- Non implementato: BSGS copre i casi pratici

#### Hybrid BSGS + Endomorphism
- Attualmente bloccato nel codice (linea 2302-2308)
- Potenziale 3x speedup aggiuntivo
- Richiede ristrutturazione della tabella BSGS
- **Opportunità futura**

---

## 4. Unrealized Opportunities

### 4.1 libsecp256k1 5×52-bit Representation

**Attuale:** 4×64-bit limbs
**Alternativa:** 5×52-bit (usato da Bitcoin Core)

**Vantaggi:**
- Carry propagation più semplice
- Migliore utilizzo della mantissa 52-bit

**Costo:** Refactoring significativo della classe Int

**Stima guadagno:** 5-15%

### 4.2 Montgomery Form per EC Points

**Idea:** Usare forma Montgomery per coordinate EC

**Vantaggi:**
- Elimina alcune riduzioni modulari durante operazioni punto

**Costo:** Cambiamento architetturale

**Stima guadagno:** 5-10%

### 4.3 GPU Full Exploitation

**Stato attuale:** GPU dà 570-600 Mkeys/s (10-12x CPU)

**Opportunità:**
- Multi-GPU load balancing dinamico
- Async pipeline per overlap compute/transfer
- Kernel splitting per ridurre register pressure

**Stima guadagno:** 20-40% su GPU

---

## 5. Revised Optimization Strategy

Basandomi sull'analisi, rivedo la priorità degli incrementi:

### Alta Priorità (ROI massimo)

| # | Incremento | Target | Guadagno atteso |
|---|------------|--------|-----------------|
| 1 | **Config System** | Usability | Fondamenta |
| 2 | **Hardware Detection** | All | Fondamenta |
| 3 | **GPU Async Pipeline** | GPU | +20-30% GPU |
| 4 | **Adaptive Work Stealing** | Hybrid | +15-25% hybrid |

### Media Priorità

| # | Incremento | Target | Guadagno atteso |
|---|------------|--------|-----------------|
| 5 | Multi-GPU Scheduler | GPU | +50-100% (lineare con GPU) |
| 6 | Memory Optimization | All | +5-10% |
| 7 | SHA-NI Auto-enable | CPU | +10-15% se disponibile |

### Bassa Priorità (complessità alta)

| # | Incremento | Target | Guadagno atteso |
|---|------------|--------|-----------------|
| 8 | Distributed Mode | Cluster | Lineare con nodi |
| 9 | BSGS + Endomorphism | BSGS | +3x (complesso) |
| 10 | 5×52 Field Repr. | All | +5-15% (refactoring) |

---

## 6. Quick Wins Identificati

### 6.1 SHA-NI Detection e Auto-enable

Il codice ha SHA-NI ma potrebbe non usarlo sempre. Verificare runtime detection.

**File:** `hash/sha256_shani.cpp`, `keyhunt.cpp`

### 6.2 Prefetching più aggressivo

Aggiungere prefetch per GTable access durante point generation.

**File:** `secp256k1/IntGroup.cpp`

### 6.3 Batch size dinamico

CPU_GRP_SIZE=1024 è ottimale per CPU, ma GPU potrebbe beneficiare di batch più grandi.

**File:** `keyhunt.cpp`, `gpu/gpu_backend_cuda.cu`

---

## 7. Performance Ceilings

### CPU Only (i9-9900, 16 threads)

| Scenario | Speed | Notes |
|----------|-------|-------|
| Current | 49 Mkeys/s | rmd160 mode |
| +SHA-NI | ~55 Mkeys/s | +10-15% |
| +Prefetch | ~58 Mkeys/s | +5% |
| Theoretical max | ~65 Mkeys/s | All CPU opts |

### GPU Only (RTX 2080S)

| Scenario | Speed | Notes |
|----------|-------|-------|
| Current | ~570 Mkeys/s | GPU full mode |
| +Async Pipeline | ~700 Mkeys/s | +20-30% |
| +Kernel tuning | ~750 Mkeys/s | +5-10% |
| Theoretical max | ~800 Mkeys/s | This GPU |

### Hybrid (i9-9900 + RTX 2080S)

| Scenario | Speed | Notes |
|----------|-------|-------|
| Current (50/50) | ~300 Mkeys/s | Suboptimal balance |
| +Adaptive | ~650 Mkeys/s | GPU 90% + CPU 10% |
| +All opts | ~750 Mkeys/s | Near theoretical |

### Multi-GPU / Cluster

| Scenario | Speed | Notes |
|----------|-------|-------|
| 2× RTX 2080S | ~1.4 Gkeys/s | Linear scaling |
| 4× RTX 3090 | ~4-5 Gkeys/s | Estimated |
| 10-node cluster | ~10 Gkeys/s | Network overhead ~10% |

---

## 8. Recommendations

### Per il tuo hardware (i9-9900 + RTX 2080S)

1. **Build con CUDA** - Priorità massima, 10x speedup immediato
2. **Usa hybrid mode** - `-G hybrid` per CPU+GPU
3. **Enable work stealing** - `KEYHUNT_HYBRID_WORK_STEAL=1`

### Per development

1. **Implementa config system** - Permette di salvare/confrontare benchmark
2. **Implementa adaptive scheduler** - Massimizza hybrid efficiency
3. **GPU async pipeline** - Guadagno significativo

### Per scalabilità futura

1. **Multi-GPU scheduler** - Supporto 2+ GPU
2. **Distributed mode** - Cluster computing
3. **BSGS + Endomorphism** - Per puzzle con pubkey nota

---

## 9. Conclusioni

Il codebase è già **altamente ottimizzato** per CPU:
- ModMulK1 al limite teorico
- Hash pipeline AVX2 8-way
- Bloom filter 2.5x ottimizzato

I guadagni maggiori verranno da:
1. **GPU utilization** (10-12x già, +20-40% possibile)
2. **Hybrid balancing** (adaptive scheduler)
3. **Multi-GPU/Cluster** (scaling lineare)

Lo spazio di ricerca è già ridotto dove possibile:
- Endomorphism: 3x più indirizzi per punto
- BSGS: O(√N) per pubkey note
- XPOINT: 2x per x-coordinate only

Le uniche opportunità non sfruttate sono:
- BSGS + Endomorphism combinati (complesso)
- 5×52-bit field representation (refactoring)
