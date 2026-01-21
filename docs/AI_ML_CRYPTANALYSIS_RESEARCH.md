# AI/ML per Ricerca Chiavi Crittografiche: Analisi Esaustiva

## Executive Summary

Questa analisi esaustiva esamina se algoritmi genetici (GA), reti neurali (NN), e altre tecniche di machine learning (ML) possono accelerare la ricerca di chiavi private Bitcoin/secp256k1. La conclusione principale e':

> **Gli algoritmi AI/ML NON possono rompere direttamente la crittografia a curva ellittica (ECC) usata in Bitcoin.** Tuttavia, possono essere utili in scenari specifici: side-channel attacks, rilevamento di RNG deboli, attacchi su password/brain wallet, e ottimizzazione di attacchi esistenti quando ci sono vulnerabilita' implementative.

---

## 1. Perche' AI/ML Non Puo' Rompere la Crittografia Moderna

### 1.1 Incompatibilita' Fondamentale

La crittografia moderna e' specificamente progettata per **eliminare pattern e struttura** negli output, mentre AI/ML **richiede pattern** per apprendere efficacemente.

**Ragioni tecniche del fallimento:**

| Problema | Spiegazione |
|----------|-------------|
| **Nessun gradiente** | La loss landscape per indovinare chiavi e' piatta - non ci sono "briciole" da seguire con gradient descent |
| **Output casuali** | I cifrari moderni producono output indistinguibili dal rumore casuale |
| **No generalizzazione** | Modelli addestrati su un algoritmo falliscono su altri |
| **Costi computazionali** | Addestrare reti profonde richiede risorse enormi, impraticabile per algoritmi complessi |

### 1.2 Evidenze Empiriche

Uno studio del 2024 su centinaia di paper mostra che le reti neurali riescono solo su versioni **toy o ridotte** dei cifrari, con **0% di successo** su crittografia reale come AES o RSA.

> "Despite some hype, there has been no practical demonstration of AI breaking a real cryptographic system."
> — [PostQuantum.com](https://postquantum.com/ai-security/ai-break-encryption/)

### 1.3 AI vs Quantum Computing

| | AI/ML | Quantum Computing |
|---|-------|-------------------|
| **Minaccia ECC** | Nessuna (fondamentalmente) | Si' (algoritmo di Shor) |
| **Timeline** | Mai | ~2029-2035 (stima IBM) |
| **Difesa della comunita'** | Business-as-usual | Sforzi enormi per PQC |

---

## 2. Algoritmi Genetici in Crittanalisi

### 2.1 Applicazioni Documentate

Gli algoritmi genetici sono stati usati principalmente per:

1. **Crittanalisi di cifrari a blocchi** - Ricerca della chiave nello spazio delle chiavi
2. **Ottimizzazione di parametri ECC** - Generazione di parametri sicuri
3. **Attacchi a cifrari classici** - Sostituzione monoalfabetica, Vigenere

**Paper chiave:** [On the Fitness Functions Involved in Genetic Algorithms and the Cryptanalysis of Block Ciphers](https://www.mdpi.com/1099-4300/25/2/261) (MDPI, 2023)

### 2.2 Limitazioni per Bitcoin/ECC

| Aspetto | Problema |
|---------|----------|
| **Fitness function** | Non esiste una funzione che misuri "quanto vicino" e' una chiave - o funziona o no |
| **No gradiente** | GA necessita feedback incrementale, ECC non ne fornisce |
| **Spazio chiavi** | 2^256 possibilita' - troppo grande per qualsiasi GA |
| **Random search equivalente** | Senza fitness utile, GA = ricerca casuale |

### 2.3 Uso Legittimo: Generazione di Chiavi

I GA possono essere usati per **generare** parametri ECC ottimali, non per romperli:

> "The effectiveness of ECC relies on the difficulty of solving the discrete logarithm problem, and a significant portion of its security is attributed to the random selection of private keys. If the GA-based process fails to generate sufficiently random private keys, it might open avenues for attacks."
> — [MDPI Proceedings](https://www.mdpi.com/2673-4591/59/1/59)

---

## 3. Reti Neurali e Deep Learning

### 3.1 Neural Differential Cryptanalysis

**Il lavoro piu' significativo**: Aron Gohr (2019) ha dimostrato che reti neurali possono creare distinguisher per attacchi differenziali su cifrari ridotti come Speck.

**Risultati chiave:**
- Distinguisher neurali su 9 round di Speck con mean key rank 5x migliore dei distinguisher classici
- Riduzione della sicurezza di 11-round Speck32/64 a ~38 bit

**Survey 2024:** 229 pubblicazioni catalogate in 6 anni di neural differential cryptanalysis ([ePrint 2024/1300](https://eprint.iacr.org/2024/1300.pdf))

### 3.2 Transformer/LLM per Crittanalisi

Ricerca recente su modelli transformer:

| Cifrario | Successo | Note |
|----------|----------|------|
| Caesar | 90% EM, 98% BLEU | Banale, non crittograficamente rilevante |
| Vigenere | Parziale | Degrada con lunghezza chiave |
| AES/RSA | ~0% | Nessun successo su crittografia moderna |

> "Success degrades rapidly with increasing key length, which mirrors the challenges faced in classical cryptanalysis."
> — [arXiv:2508.10235](https://arxiv.org/html/2508.10235v1)

### 3.3 Applicabilita' a secp256k1/Bitcoin

**Non applicabile direttamente** perche':
- ECDLP (Elliptic Curve Discrete Logarithm Problem) non ha struttura sfruttabile
- Non esistono pattern nelle chiavi private ben generate
- Il creatore dei puzzle Bitcoin ha confermato: "There is no pattern. It is just consecutive keys from a deterministic wallet."

---

## 4. Dove AI/ML PUO' Essere Utile

### 4.1 Side-Channel Attacks (MOLTO EFFICACE)

Le reti neurali eccellono negli attacchi side-channel:

**Power Analysis:**
> "GPAM is able to recover the four most significant bits of the secret scalar with accuracy between 71.86% to 96.39%... This is reportedly the first time that highly-protected ECDSA implementations have been proven vulnerable to power side-channel attacks."
> — [arXiv:2306.07249](https://arxiv.org/html/2306.07249v2)

**Tecniche efficaci:**
- **CNN (Convolutional Neural Networks)**: Rompono EdDSA con singola misurazione
- **LSTM Autoencoder**: Estrae features con 10x meno tracce rispetto a DPA classico
- **RNN per time series**: Analisi di tracce power/EM

**Requisiti:**
- Accesso fisico al dispositivo target
- Misurazioni di power consumption o emanazioni EM
- 200-500 tracce per training

### 4.2 Attacchi su Password/Brain Wallet

**PassGAN** (GAN per password cracking):
- 47% delle password indovinate da leak RockYou
- +24% password matchate combinato con HashCat
- Genera password oltre la portata di tool rule-based

**Applicazione a brain wallet:**
> "Attack speed can reach approximately 16,250 passwords per second on each thread and has cracked more than 18,000 brain wallet addresses."

**LSTM per password:**
- Modella distribuzioni di probabilita' a livello di carattere
- Genera password mai viste nel training set
- [GitHub: cupslab/neural_network_cracking](https://github.com/cupslab/neural_network_cracking)

### 4.3 Rilevamento RNG Deboli

Vulnerabilita' documentate dove AI/ML potrebbe aiutare:

| Vulnerabilita' | Anno | Impatto |
|----------------|------|---------|
| **Randstorm** | 2011-2015 | ~1.4M BTC in wallet vulnerabili |
| **Milk Sad (CVE-2023-39910)** | 2023 | $900k+ rubati, MT19937 con 32-bit entropy |
| **Android SecureRandom** | 2013 | Nonce collisions in ECDSA |

**Potenziale ML:**
- Pattern recognition su wallet storici
- Anomaly detection su distribuzioni di chiavi
- Clustering di wallet con RNG correlati

### 4.4 Lattice Attacks + ML (Combinazione)

Quando esistono bias nei nonce ECDSA:

> "Researchers computed the private keys for 302 distinct keys that were compromised via small nonces, nonces with shared prefixes, or nonces with shared suffixes."
> — [FC19](https://fc19.ifca.ai/preproceedings/104-preproceedings.pdf)

**Requisiti:** Minimo 4 bit noti per nonce (LSB o MSB)
**Tool:** [github.com/bitlogik/lattice-attack](https://github.com/bitlogik/lattice-attack)

---

## 5. Ottimizzazione di Algoritmi Esistenti

### 5.1 Pollard Rho/Kangaroo

Questi algoritmi GIA' offrono complessita' O(sqrt(N)) per ECDLP quando si conosce la public key.

**Ottimizzazioni esistenti (non AI):**
- GPU parallelization (CUDA/Vulkan)
- Distinguished points method
- Linear speedup con parallelizzazione

**Tool:** [JeanLucPons/Kangaroo](https://github.com/JeanLucPons/Kangaroo)

**Potenziale ML:** Minimo - l'algoritmo e' gia' ottimale matematicamente.

### 5.2 Baby-Step Giant-Step (BSGS)

Complessita' O(sqrt(N)) con trade-off tempo-memoria.

**Ottimizzazioni esistenti:**
- Bloom filter per lookup veloci
- AVX2/AVX-512 SIMD
- Batched operations

**Potenziale ML:** Nessuno - e' un algoritmo deterministico senza spazio per "apprendimento".

---

## 6. Ricerca Accademica Rilevante

### 6.1 Survey Principali

| Paper | Anno | Focus |
|-------|------|-------|
| [Neural Differential Cryptanalysis Survey](https://eprint.iacr.org/2024/1300.pdf) | 2024 | 229 pubblicazioni, 66 paper dettagliati |
| [AI in Cryptanalysis](https://fse-journal.org/index.php/ojs/article/view/75) | 2025 | Deep learning per side-channel e fault analysis |
| [ML-Based Cryptanalysis Metrics](https://arxiv.org/html/2501.15076v1) | 2025 | Information theoretic approach |

### 6.2 Paper Chiave su Limitazioni

- **[Why AI Cannot Break Modern Encryption](https://postquantum.com/ai-security/ai-break-encryption/)** - Analisi completa delle limitazioni fondamentali
- **[Is ML-Based Cryptanalysis Inherently Limited?](https://dl.acm.org/doi/10.1007/978-3-030-77870-5_28)** (EUROCRYPT 2021) - Simulazione di adversary crittografici
- **[No, AI did not break post-quantum cryptography](https://blog.cloudflare.com/kyber-isnt-broken/)** - Cloudflare debunking di claims esagerati

---

## 7. Applicabilita' a Keyhunt

### 7.1 Modi di Ricerca Attuali

| Modo | Complessita' | AI/ML Utile? |
|------|--------------|--------------|
| ADDRESS | O(N) brute force | NO - nessun pattern |
| BSGS | O(sqrt(N)) | NO - gia' ottimale |
| XPOINT | O(N) | NO - nessun pattern |
| RMD160 | O(N) | NO - hash crittografico |

### 7.2 Possibili Miglioramenti con ML

| Area | Fattibilita' | Beneficio |
|------|--------------|-----------|
| **Prioritizzazione range** | BASSA | Nessun pattern nei puzzle Bitcoin |
| **Side-channel su hardware** | ALTA (se accesso fisico) | Potenziale recupero parziale nonce |
| **Brain wallet recovery** | MEDIA | PassGAN-style per passphrase comuni |
| **Anomaly detection su wallet** | MEDIA | Identificare RNG deboli storici |

### 7.3 Raccomandazione

**Per i puzzle Bitcoin (66, 67, 71, 135, etc.):**
- AI/ML **NON** offre vantaggi rispetto a brute force
- Il creatore ha confermato assenza di pattern
- Miglior approccio: hardware piu' veloce, parallelizzazione, BSGS per puzzle con pubkey

**Per wallet recovery legittimo:**
- Se password dimenticata: PassGAN/HashCat combination
- Se RNG potenzialmente debole (wallet 2011-2015): analisi Randstorm
- Se accesso fisico a hardware: side-channel analysis

---

## 8. Conclusioni

### 8.1 Cosa AI/ML NON PUO' Fare

1. Rompere ECDLP/secp256k1 direttamente
2. Trovare pattern in chiavi ben generate
3. Accelerare brute force matematicamente
4. Ridurre lo spazio di ricerca per chiavi random

### 8.2 Cosa AI/ML PUO' Fare

1. **Side-channel attacks**: Recupero parziale di nonce/chiavi da misurazioni fisiche
2. **Password cracking**: Generazione intelligente di candidati per brain wallet
3. **Anomaly detection**: Identificare wallet con RNG deboli
4. **Neural differential cryptanalysis**: Attacchi su cifrari simmetrici ridotti (non ECC)

### 8.3 Verdetto Finale per Keyhunt

> **L'integrazione di AI/ML in keyhunt per la ricerca di chiavi Bitcoin non offrirebbe vantaggi significativi.** Lo spazio delle chiavi e' matematicamente sicuro e non presenta pattern sfruttabili. Le risorse sarebbero meglio investite in:
> - Ottimizzazione SIMD (AVX-512)
> - Parallelizzazione GPU (CUDA)
> - BSGS per puzzle con public key nota
> - Hardware specializzato (FPGA/ASIC)

---

## 9. Fonti

### Sicurezza ECC
- [Secp256k1 - Bitcoin Wiki](https://en.bitcoin.it/wiki/Secp256k1)
- [Security of Secp256k1](https://www.ijcns.latticescipub.com/wp-content/uploads/papers/v4i1/A1426054124.pdf)

### AI e Crittografia
- [Why AI Cannot Break Modern Encryption](https://postquantum.com/ai-security/ai-break-encryption/)
- [Neural Networks In Cryptanalysis](https://aicompetence.org/neural-networks-in-cryptanalysis/)
- [Applications of Neural Network-Based AI in Cryptography](https://hal.science/hal-04222021/document)

### Machine Learning Cryptanalysis
- [Survey: 6 Years of Neural Differential Cryptanalysis](https://eprint.iacr.org/2024/1300.pdf)
- [Improving Attacks on Speck32/64 Using Deep Learning](https://eprint.iacr.org/2019/037.pdf)
- [Deep Learning Side-Channel Attacks](https://elie.net/blog/security/hacker-guide-to-deep-learning-side-channel-attacks-the-theory)

### Side-Channel Attacks
- [Generalized Power Attacks against Crypto](https://arxiv.org/html/2306.07249v2)
- [One Trace Is All It Takes: ML Side-Channel Attack on EdDSA](https://link.springer.com/chapter/10.1007/978-3-030-35869-3_8)
- [SoK: Deep Learning-based Physical Side-channel Analysis](https://dl.acm.org/doi/10.1145/3569577)

### Password/Brain Wallet Attacks
- [PassGAN Password Cracking](https://www.darkreading.com/analytics/passgan-password-cracking-using-machine-learning)
- [Modeling Password Guessability Using Neural Networks](https://www.usenix.org/system/files/conference/usenixsecurity16/sec16_paper_melicher.pdf)
- [Neural Network Password Cracking](https://github.com/cupslab/neural_network_cracking)

### Vulnerabilita' RNG
- [Randstorm Vulnerability](https://www.darkreading.com/application-security/randstorm-bug-millions-of-crypto-wallets-open-to-theft)
- [Milk Sad / Libbitcoin Vulnerability](https://tangem.com/en/blog/post/randomness-importance/)
- [120,000 BTC Theft Deep Dive](https://safeheron.com/blog/a-deep-dive-into-120k-btc-theft/)

### Lattice Attacks
- [Biased Nonce Sense: Lattice Attacks](https://eprint.iacr.org/2019/023)
- [Lattice Attack Tool](https://github.com/bitlogik/lattice-attack)

### Pollard Algorithms
- [JeanLucPons/Kangaroo](https://github.com/JeanLucPons/Kangaroo)
- [CUDA Pollard's Rho Implementation](https://www.researchgate.net/publication/228913329_CUDA_based_implementation_of_parallelized_Pollard's_Rho_algorithm_for_ECDLP)

### Transformer/LLM
- [Can Transformers Break Encryption?](https://arxiv.org/html/2508.10235v1)
- [Benchmarking LLMs for Cryptanalysis](https://arxiv.org/html/2505.24621v1)

---

*Documento generato il 2026-01-21*
*Ricerca condotta con Claude Code*
