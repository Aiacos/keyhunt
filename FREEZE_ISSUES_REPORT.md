# Report: Problemi di Blocco all'Avvio di Keyhunt

## Risposta alla Domanda: Puzzle 71

**❌ PUZZLE 71 NON HA CHIAVE PUBBLICA**

```
Indirizzo: 1JTK7s9YVYywfm5XUH7RNhHJH1LshCaRFR
Transazioni: 6 (solo ricevute, mai speso)
Chiave Pubblica: NON DISPONIBILE ON-CHAIN
```

**Motivo:** L'indirizzo del puzzle 71 ha solo ricevuto Bitcoin ma non ha mai speso. La chiave pubblica viene rivelata SOLO quando si spende, quindi per puzzle 71-160 (non ancora risolti) la chiave pubblica NON è disponibile a meno che non abbiano speso.

---

## Analisi Problemi di Blocco

### 🔴 Problema 1: getrandom() può causare exit immediato

**File:** keyhunt.cpp:818-832

```cpp
int bytes_read = getrandom(&rseedvalue, sizeof(unsigned long), GRND_NONBLOCK);
if(bytes_read > 0) {
    rseed(rseedvalue);
} else {
    fprintf(stderr,"[E] Error getrandom() ?\n");
    exit(EXIT_FAILURE);  // ⚠️ EXIT IMMEDIATO
    rseed(clock() + time(NULL) + rand()*rand());  // ⬅️ CODICE MAI ESEGUITO
}
```

**Problema:**
- Se `getrandom()` fallisce (sistema senza entropia, container Docker limitato, WSL con problemi)
- Il programma esce immediatamente con `exit(EXIT_FAILURE)`
- Il codice fallback con `rseed()` alla riga 833 NON viene mai eseguito (unreachable code)

**Fix Suggerito:**
```cpp
int bytes_read = getrandom(&rseedvalue, sizeof(unsigned long), GRND_NONBLOCK);
if(bytes_read > 0) {
    rseed(rseedvalue);
} else {
    fprintf(stderr,"[W] Warning: getrandom() failed, using fallback RNG\n");
    rseed(clock() + time(NULL) + rand()*rand());  // FALLBACK invece di exit
}
```

---

### 🔴 Problema 2: sysinfo_init() fa molte letture bloccanti

**File:** sysinfo.c:347-365

```cpp
void sysinfo_init(system_info_t *info) {
    detect_physical_cores();    // ⚠️ Legge /sys/devices/system/cpu/*
    detect_logical_cores();     // ⚠️ Chiama sysconf()
    detect_cache_sizes(info);   // ⚠️ Legge /sys/devices/system/cpu/cpu0/cache/*
    detect_memory(info);        // ⚠️ Legge /proc/meminfo + sysinfo()
    detect_cpu_features(info);  // ⚠️ Legge /proc/cpuinfo
    calculate_optimal_params(info);
}
```

**Problemi potenziali:**

1. **Filesystem /sys e /proc lenti:**
   - NFS montato lento
   - Container con overlay filesystem
   - Sistema con I/O saturo

2. **opendir("/sys/devices/system/cpu") bloccante:**
   - Directory con migliaia di file (sistemi con 128+ core)
   - Filesystem corrotto o in errore

3. **fopen("/proc/cpuinfo") può bloccarsi:**
   - Procfs non disponibile
   - Kernel panic parziale
   - SELinux che blocca l'accesso

**Fix Suggerito:**
Aggiungere timeout e error handling:

```cpp
// Aggiungere flag per skip auto-detection
void sysinfo_init_safe(system_info_t *info, bool use_defaults_on_error) {
    memset(info, 0, sizeof(system_info_t));

    // Timeout per ogni operazione
    alarm(5);  // 5 secondi timeout

    // Try-catch equivalent con signal handler
    if (setjmp(timeout_buffer) == 0) {
        info->cpu_physical_cores = detect_physical_cores();
        // ... altri detect
    } else {
        // Timeout - usa defaults
        fprintf(stderr, "[W] System detection timeout, using defaults\n");
        use_safe_defaults(info);
    }

    alarm(0);  // Cancella timeout
}
```

---

### 🔴 Problema 3: Race condition nei thread con pthread_detach

**File:** keyhunt.cpp:2085-2117

```cpp
s = pthread_create(&tid[j], NULL, thread_bPload_2blooms, (void*)&bPload_temp_ptr[j]);
pthread_detach(tid[j]);  // ⚠️ Thread detached immediatamente

// ... più tardi ...

do {
    for(j = 0; j < NTHREADS; j++) {
        pthread_mutex_lock(&bPload_mutex[j]);  // ⚠️ Può bloccarsi se thread muore
        finished = bPload_temp_ptr[j].finished;
        pthread_mutex_unlock(&bPload_mutex[j]);
        // ...
    }
} while(FINISHED_THREADS_COUNTER < THREADCYCLES);  // ⚠️ Loop infinito se contatore non sale
```

**Problemi:**

1. **Thread detached + mutex wait = deadlock:**
   - Thread viene detached immediatamente
   - Se il thread crasha o termina prematuramente
   - Il mutex potrebbe rimanere locked
   - Il main thread aspetta per sempre

2. **FINISHED_THREADS_COUNTER può non incrementarsi:**
   - Se un thread non setta mai `finished = 1`
   - Il loop `do...while` diventa infinito
   - Nessun timeout, il programma si blocca

**Fix Suggerito:**

```cpp
// OPZIONE 1: Usare pthread_join invece di detach
s = pthread_create(&tid[j], NULL, thread_bPload_2blooms, (void*)&bPload_temp_ptr[j]);
// NON fare pthread_detach

// Poi alla fine:
for(j = 0; j < NTHREADS; j++) {
    pthread_join(tid[j], NULL);  // Aspetta terminazione
}

// OPZIONE 2: Aggiungere timeout nel loop
int timeout_counter = 0;
const int MAX_WAIT_CYCLES = 10000;  // ~10 secondi

do {
    for(j = 0; j < NTHREADS; j++) {
        pthread_mutex_lock(&bPload_mutex[j]);
        finished = bPload_temp_ptr[j].finished;
        pthread_mutex_unlock(&bPload_mutex[j]);
        // ...
    }

    usleep(1000);  // 1ms sleep
    timeout_counter++;

    if(timeout_counter > MAX_WAIT_CYCLES) {
        fprintf(stderr, "[E] Thread timeout waiting for completion\n");
        break;  // Esci dal loop invece di bloccarti
    }

} while(FINISHED_THREADS_COUNTER < THREADCYCLES);
```

---

### 🔴 Problema 4: Mutex senza timeout

**File:** Ovunque in keyhunt.cpp

```cpp
pthread_mutex_lock(&bsgs_thread);  // ⚠️ NESSUN TIMEOUT
// ... operazioni critiche ...
pthread_mutex_unlock(&bsgs_thread);
```

**Problema:**
- `pthread_mutex_lock()` blocca per sempre se il mutex non viene rilasciato
- Se un thread crasha mentre tiene un mutex, tutti gli altri si bloccano
- Nessun meccanismo di recovery

**Fix Suggerito:**

```cpp
// Usare pthread_mutex_timedlock invece di pthread_mutex_lock
struct timespec timeout;
clock_gettime(CLOCK_REALTIME, &timeout);
timeout.tv_sec += 5;  // 5 secondi timeout

int ret = pthread_mutex_timedlock(&bsgs_thread, &timeout);
if(ret == ETIMEDOUT) {
    fprintf(stderr, "[E] Mutex lock timeout - possible deadlock\n");
    // Gestisci errore invece di bloccarti per sempre
    return;
}
```

---

## Come Diagnosticare il Blocco

### Test 1: Verifica getrandom()

```bash
# Testa se getrandom funziona sul tuo sistema
strace -e getrandom ./keyhunt 2>&1 | head -20

# Se vedi "getrandom() = -1" allora è il problema
```

### Test 2: Verifica sysinfo lento

```bash
# Esegui con timer
time ./keyhunt -h

# Se impiega più di 2-3 secondi, sysinfo_init è lento
```

### Test 3: Verifica thread deadlock

```bash
# Esegui e se si blocca, controlla lo stato dei thread
./keyhunt -m rmd160 -f test.txt -t 8 &
PID=$!

# Dopo che si blocca:
ps -T -p $PID  # Mostra tutti i thread
cat /proc/$PID/task/*/status | grep State  # Mostra stato dei thread
```

### Test 4: Strace completo

```bash
# Vedi esattamente dove si blocca
strace -f -o keyhunt.strace ./keyhunt -m rmd160 -f test.txt

# Cerca nell'output:
grep "futex\|INFINITE\|blocking" keyhunt.strace
tail -100 keyhunt.strace  # Ultime 100 righe prima del blocco
```

---

## Soluzioni Immediate

### Soluzione 1: Variabile d'ambiente per skip checks

Aggiungi all'inizio di main():

```cpp
// keyhunt.cpp:790
if(getenv("KEYHUNT_SKIP_SYSINFO")) {
    fprintf(stderr, "[W] Skipping sysinfo detection (KEYHUNT_SKIP_SYSINFO set)\n");
    // Usa valori di default sicuri
} else {
    sysinfo_init(&sysinfo);
}
```

Uso:
```bash
KEYHUNT_SKIP_SYSINFO=1 ./keyhunt -m rmd160 -f test.txt
```

### Soluzione 2: Flag --no-autodetect

Aggiungi opzione command line:

```cpp
case 'X':  // --no-autodetect
    FLAG_NO_AUTODETECT = 1;
    break;
```

### Soluzione 3: Timeout globale con alarm()

Aggiungi all'inizio di main():

```cpp
#include <signal.h>

void timeout_handler(int sig) {
    fprintf(stderr, "\n[E] Initialization timeout - system detection failed\n");
    fprintf(stderr, "[E] Try running with: KEYHUNT_SKIP_SYSINFO=1 ./keyhunt ...\n");
    exit(EXIT_FAILURE);
}

// In main():
signal(SIGALRM, timeout_handler);
alarm(10);  // 10 secondi timeout per l'inizializzazione

sysinfo_init(&sysinfo);
// ... altre inizializzazioni ...

alarm(0);  // Cancella timeout se arrivato qui
```

---

## Priorità Fix

### 🔴 URGENTE (causa exit immediato):
1. Fix getrandom() - rimuovere `exit(EXIT_FAILURE)`, usare fallback

### 🟠 ALTA (causa blocchi frequenti):
2. Aggiungere timeout a sysinfo_init()
3. Aggiungere timeout ai mutex lock
4. Fix race condition con pthread_detach

### 🟡 MEDIA (miglioramenti):
5. Aggiungere flag --no-autodetect
6. Aggiungere variabile KEYHUNT_SKIP_SYSINFO
7. Migliorare error reporting

---

## Test Raccomandati

Dopo aver applicato i fix:

```bash
# Test 1: Sistema senza /proc
unshare --mount --map-root-user sh -c 'umount /proc; ./keyhunt -h'

# Test 2: Sistema senza entropia
# (difficile da simulare, ma testare in container)

# Test 3: Stress test con molti thread
./keyhunt -m rmd160 -f huge_file.txt -t 128

# Test 4: Kill random dei thread
while true; do
    ./keyhunt -m rmd160 -f test.txt -t 16 &
    sleep 2
    kill -9 $(pgrep keyhunt | head -1)
    sleep 1
done
```

---

## Conclusioni

**Puzzle 71:** ❌ NO chiave pubblica (mai speso)

**Problemi trovati:** 4 cause critiche di blocco
- getrandom() exit immediato
- sysinfo_init() lento/bloccante
- pthread_detach race conditions
- mutex senza timeout

**Raccomandazione:** Applicare almeno i fix urgenti prima di rilasciare
