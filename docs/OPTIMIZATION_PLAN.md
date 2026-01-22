# Piano di Ottimizzazione Keyhunt - Ordine di Grandezza

## Sommario Esecutivo

Questo piano mira ad aumentare le prestazioni di keyhunt di **almeno un ordine di grandezza (10x)**.
Obiettivo: da ~86 Mkeys/s attuali a **>860 Mkeys/s** (idealmente 1+ Gkeys/s).

## Analisi Attuale dei Bottleneck

### Profilo di Performance Stimato (% CPU Time)
| Componente | % Tempo | Attuale | Target |
|------------|---------|---------|--------|
| RIPEMD160 hashing | 40-50% | AVX2 8-way | AVX-512 16-way + ASM |
| SHA256 hashing | 20-25% | AVX2 8-way | SHA-NI + AVX-512 |
| ModInv (inversione modulare) | 15-20% | DRS62 | safegcd + batch ottimizzato |
| EC Point Addition | 10-15% | Standard | Batch affine coords |
| Bloom filter checks | 3-5% | XXH64 | Cache-sectorized SIMD |
| Binary search | 2-3% | Standard | SIMD-accelerated |

## Fasi di Ottimizzazione

---

## FASE 1: Ottimizzazioni Immediate (Impatto: +30-50%)

### 1.1 Implementazione RIPEMD160 AVX-512 (16-way)
**File:** `hash/ripemd160_avx512.cpp`
**Impatto stimato:** +20-25%

L'implementazione AVX-512 attuale processa 16 hash in parallelo vs 8 di AVX2.
Ottimizzazioni:
- Utilizzare tutti i 32 registri ZMM disponibili
- Eliminare operazioni NOT ridondanti usando `_mm512_ternarylogic_epi32`
- Implementare prefetching aggressivo per i 16 blocchi di input
- Allineare tutti i buffer a 64 byte (cache line)

```cpp
// Pattern ottimizzato per AVX-512 con ternary logic
#define f1_512(x,y,z) _mm512_xor_epi32(x, _mm512_xor_epi32(y, z))
#define f3_512(x,y,z) _mm512_ternarylogic_epi32(x, y, z, 0x96) // XOR3
```

### 1.2 SHA256 con SHA-NI Extensions
**File:** `hash/sha256_shani.cpp` (nuovo)
**Impatto stimato:** +15-20%

Implementare SHA256 usando le istruzioni hardware SHA-NI:
- `sha256msg1`, `sha256msg2`: Message scheduling
- `sha256rnds2`: 2 round SHA256 per istruzione
- Velocità attesa: ~4x rispetto a AVX2

```cpp
// Esempio di round SHA-NI
STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
```

### 1.3 Bloom Filter Cache-Sectorized
**File:** `bloom/bloom_simd.cpp` (nuovo)
**Impatto stimato:** +5-8%

Reimplementare bloom filter secondo Lang et al. (VLDB'19):
- Bits distribuiti su cache line (64 byte)
- Test di bits in parallelo con AVX2/AVX-512
- Eliminare loop di `bloom->hashes` iterazioni

```cpp
// Pattern cache-sectorized: tutti i bit in una cache line
inline bool bloom_check_simd(uint8_t *bf, uint64_t hash) {
    __m256i bits = _mm256_set1_epi64x(hash);
    __m256i sector = _mm256_load_si256((__m256i*)(bf + (hash >> 6) * 64));
    // Test 4 bit positions in parallel
    return _mm256_testc_si256(sector, bits);
}
```

---

## FASE 2: Ottimizzazioni Architetturali (Impatto: +50-100%)

### 2.1 Batch Montgomery Inversion Ottimizzata
**File:** `secp256k1/IntGroup.cpp`
**Impatto stimato:** +15-25%

L'attuale implementazione usa Montgomery trick ma può essere migliorata:
- Aumentare batch size da 1024 a 2048-4096
- Implementare versione SIMD della forward/backward pass
- Usare `__builtin_prefetch` più aggressivamente

```cpp
// SIMD-accelerated forward pass (concettuale)
void IntGroup::ModInv_SIMD() {
    // Process 4 multiplications in parallel using AVX2
    for (int i = 0; i + 4 <= size; i += 4) {
        // Parallel ModMulK1 per 4 elementi
    }
}
```

### 2.2 Coordinate Affine con Batch Conversion
**File:** `secp256k1/SECP256K1.cpp`
**Impatto stimato:** +10-15%

Attualmente `AddDirect` richiede ModInv per ogni addizione.
Ottimizzazione: raccogliere N addizioni e fare batch inversion.

```cpp
// Nuovo pattern: accumula dx, poi batch invert
void Secp256K1::AddDirectBatch(Point *p1, Point *p2, Point *result, int count) {
    Int dx[count];
    for (int i = 0; i < count; i++) {
        dx[i].ModSub(&p2[i].x, &p1[i].x);
    }
    IntGroup g(count);
    g.Set(dx);
    g.ModInv();  // Una sola inversione per N addizioni!
    // Continua con le moltiplicazioni...
}
```

### 2.3 Pipeline di Hashing Ristrutturata
**File:** `keyhunt.cpp` (main loop)
**Impatto stimato:** +20-30%

Ristrutturare il loop principale per massimizzare throughput:
1. **Stage 1**: Generazione punti EC (CPU-bound)
2. **Stage 2**: SHA256 batch (può usare SHA-NI)
3. **Stage 3**: RIPEMD160 batch (AVX-512)
4. **Stage 4**: Bloom check batch (SIMD)

```cpp
// Pattern pipeline con staging
void process_batch_pipeline(Point *points, int count) {
    alignas(64) uint8_t sha_results[count][32];
    alignas(64) uint8_t rmd_results[count][20];

    // Stage 2: Batch SHA256 (32 alla volta con AVX-512)
    sha256_batch_avx512(pubkeys, sha_results, count);

    // Stage 3: Batch RIPEMD160 (16 alla volta con AVX-512)
    ripemd160_batch_avx512(sha_results, rmd_results, count);

    // Stage 4: Batch bloom check (8 alla volta)
    bloom_check_batch_simd(bloom, rmd_results, count, matches);
}
```

---

## FASE 3: Ottimizzazioni Assembly Critiche (Impatto: +20-40%)

### 3.1 ModMulK1 in Assembly x64
**File:** `secp256k1/asm/modmul_x64.S` (nuovo)
**Impatto stimato:** +15-20%

`ModMulK1` è chiamato milioni di volte. Versione assembly ottimizzata:

```nasm
; ModMulK1 ottimizzato per secp256k1
; Usa MULX, ADCX, ADOX per multiplicazione parallela
global ModMulK1_asm
ModMulK1_asm:
    ; Input: rdi = result, rsi = a, rdx = b
    mulx r8, rax, [rsi]      ; a[0] * b
    mulx r9, rcx, [rsi+8]    ; a[1] * b
    adcx r8, rcx
    ; ... continua con riduzione secp256k1
    ; Reduce usando costante 0x1000003D1
```

### 3.2 RIPEMD160 Core in Assembly
**File:** `hash/asm/ripemd160_avx512.S` (nuovo)
**Impatto stimato:** +10-15%

Il core RIPEMD160 può beneficiare di assembly:
- Registri allocati manualmente
- Eliminazione di spill to stack
- Scheduling ottimale delle istruzioni

### 3.3 SHA256 con SHA-NI Assembly
**File:** `hash/asm/sha256_shani.S` (nuovo)
**Impatto stimato:** +5-10%

```nasm
; SHA256 using SHA-NI instructions
sha256_shani_block:
    sha256rnds2 xmm0, xmm1, xmm2
    sha256msg1 xmm3, xmm4
    ; Interleave message schedule con rounds
```

---

## FASE 4: Librerie Esterne ad Alte Prestazioni

### 4.1 Integrazione bitcoin-core/secp256k1
**Impatto stimato:** +10-20% (per operazioni EC)

La libreria ufficiale Bitcoin ha ottimizzazioni estreme:
- Constant-time operations
- Precomputed tables ottimizzate
- Assembly per x86-64

```makefile
# Aggiungere al Makefile
LIBSECP256K1 = /usr/local/lib/libsecp256k1.a
CXXFLAGS += -DUSE_EXTERNAL_SECP256K1
```

### 4.2 Intel IPP Crypto (Opzionale)
**Impatto stimato:** +15-25% per SHA256

Per sistemi Intel, IPP offre implementazioni ottimizzate:
```cpp
#include <ippcp.h>
// SHA256 multi-buffer
ippsHashMessage_rmf(pSrc, len, pDst, ippsHashMethod_SHA256());
```

---

## FASE 5: Parallelizzazione Avanzata

### 5.1 Lock-Free Work Queue Ottimizzata
**File:** `workqueue.h`
**Impatto stimato:** +5-10%

L'attuale work queue usa mutex. Implementare versione lock-free:
```cpp
template<typename T>
class LockFreeQueue {
    std::atomic<Node*> head, tail;
    // Compare-and-swap per push/pop
};
```

### 5.2 NUMA-Aware Memory Allocation
**Impatto stimato:** +5-15% (su sistemi multi-socket)

```cpp
#include <numa.h>
void* numa_alloc_bloom(size_t size, int node) {
    return numa_alloc_onnode(size, node);
}
```

### 5.3 Thread Pool con Work Stealing
**Impatto stimato:** +5-10%

Implementare work stealing per bilanciare il carico:
```cpp
class WorkStealingPool {
    std::deque<Task> queues[MAX_THREADS];
    Task steal_from_neighbor(int thread_id);
};
```

---

## Piano di Implementazione

### Priorità Alta (Settimana 1-2)
1. [ ] SHA256 con SHA-NI extensions
2. [ ] RIPEMD160 AVX-512 ottimizzato
3. [ ] Bloom filter cache-sectorized

### Priorità Media (Settimana 3-4)
4. [ ] Batch ModInv ottimizzato
5. [ ] Pipeline di hashing ristrutturata
6. [ ] ModMulK1 assembly

### Priorità Bassa (Settimana 5+)
7. [ ] Integrazione libsecp256k1 esterna
8. [ ] NUMA-aware allocation
9. [ ] Lock-free work queue

---

## Metriche di Validazione

### Test di Correttezza
```bash
# Test funzionale (deve trovare le stesse chiavi)
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF
./keyhunt -m bsgs -f tests/125.txt -b 125 -q -s 10 -R
```

### Benchmark di Performance
```bash
# Misura baseline
time ./keyhunt -m address -f tests/66.txt -b 66 -R -q -s 30

# Confronto con ottimizzazioni
perf stat -e cycles,instructions,cache-misses ./keyhunt ...
```

### Target Performance
| Metrica | Attuale | Target | Incremento |
|---------|---------|--------|------------|
| Mkeys/s | 86 | >860 | 10x |
| Cache miss rate | ~5% | <2% | 2.5x |
| IPC | ~1.5 | >2.5 | 1.7x |

---

## Rischi e Mitigazioni

| Rischio | Probabilità | Mitigazione |
|---------|-------------|-------------|
| Regressione correttezza | Media | Test suite completa |
| Instabilità con AVX-512 | Bassa | Fallback a AVX2 |
| Incompatibilità CPU | Media | Feature detection runtime |
| Memory corruption | Bassa | Valgrind + ASan testing |

---

## Note Implementative

### Compatibilità CPU
- AVX-512: Intel Skylake-X+, AMD Zen4+
- SHA-NI: Intel Ice Lake+, AMD Zen+
- AVX2: Intel Haswell+, AMD Excavator+

### Fallback Chain
```
AVX-512 → AVX2 → SSE4.2 → Scalar
SHA-NI → SHA AVX2 → SHA Scalar
```

### Build System
```makefile
# Nuove regole per assembly e AVX-512
hash/asm/%.o: hash/asm/%.S
	$(AS) $(ASFLAGS) -o $@ $<

hash/sha256_shani.o: hash/sha256_shani.cpp
	$(CXX) $(CXXFLAGS) -msha -c $< -o $@
```

---

## Riferimenti

1. [bitcoin-core/secp256k1](https://github.com/bitcoin-core/secp256k1)
2. [minio/sha256-simd](https://github.com/minio/sha256-simd)
3. [Lang et al. - Performance-Optimal Bloom Filters (VLDB'19)](https://www.vldb.org/pvldb/vol12/p502-lang.pdf)
4. [vladkens/rmd160-simd](https://vladkens.cc/rmd160-simd/)
5. [Intel SHA Extensions](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sha-extensions.html)
