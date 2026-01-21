#!/bin/bash
#
# puzzle71_worker.sh - Worker script per puzzle 71 distribuito
#
# Uso: ./puzzle71_worker.sh <coordinator_ip> [porta]
#
# Questo script si connette al coordinator, riceve range di lavoro,
# e usa keyhunt per la ricerca effettiva.

COORDINATOR_IP="${1:-localhost}"
COORDINATOR_PORT="${2:-7777}"
WORKER_ID="$$-$(hostname)"
TARGET_FILE="puzzle71.txt"

# Colori
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "╔══════════════════════════════════════════════════════════════╗"
echo "║           KEYHUNT PUZZLE 71 - WORKER                         ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
echo "[+] Worker ID: $WORKER_ID"
echo "[+] Coordinator: $COORDINATOR_IP:$COORDINATOR_PORT"
echo "[+] Target file: $TARGET_FILE"
echo ""

# Verifica che esista il file target
if [ ! -f "$TARGET_FILE" ]; then
    echo -e "${RED}[ERRORE] File $TARGET_FILE non trovato!${NC}"
    echo "Crea il file con l'indirizzo del puzzle 71:"
    echo "  echo '1PWCx5fovoEaoBowAvF5k91m2Xat9bMgwb' > puzzle71.txt"
    exit 1
fi

# Verifica keyhunt
if [ ! -x "./keyhunt" ]; then
    echo -e "${RED}[ERRORE] keyhunt non trovato o non eseguibile!${NC}"
    exit 1
fi

# Funzione per ottenere lavoro dal coordinator (semplificata con netcat)
get_work() {
    # In una versione completa, useremmo il client C
    # Per ora, usiamo un approccio basato su file di stato

    local WORK_FILE="/tmp/keyhunt_work_${WORKER_ID}"
    local WORK_ID=$((RANDOM % 1000000))

    # Simula assegnazione di range progressivo
    # In produzione, questo verrebbe dal coordinator via TCP
    if [ -f "$WORK_FILE" ]; then
        source "$WORK_FILE"
        NEXT_START=$(printf "%x" $((0x$CURRENT_END + 1)))
    else
        NEXT_START="400000000000000000"
    fi

    # Calcola fine del range (4G chiavi = 0x100000000)
    NEXT_END=$(printf "%x" $((0x$NEXT_START + 0x100000000 - 1)))

    # Verifica se abbiamo superato il range
    if [ $((16#$NEXT_START)) -ge $((16#7FFFFFFFFFFFFFFFFF)) ]; then
        echo "NO_MORE_WORK"
        return 1
    fi

    # Salva stato
    echo "CURRENT_START=$NEXT_START" > "$WORK_FILE"
    echo "CURRENT_END=$NEXT_END" >> "$WORK_FILE"
    echo "WORK_ID=$WORK_ID" >> "$WORK_FILE"

    echo "$NEXT_START:$NEXT_END"
    return 0
}

# Loop principale
TOTAL_KEYS=0
WORK_COUNT=0

while true; do
    echo ""
    echo -e "${YELLOW}[+] Richiedo lavoro al coordinator...${NC}"

    # Per il demo, usiamo divisione locale
    # In produzione, questo chiamerebbe il coordinator
    RANGE=$(get_work)

    if [ "$RANGE" == "NO_MORE_WORK" ]; then
        echo -e "${GREEN}[✓] Nessun altro lavoro disponibile. Termino.${NC}"
        break
    fi

    START=$(echo $RANGE | cut -d: -f1)
    END=$(echo $RANGE | cut -d: -f2)

    echo "[+] Lavoro ricevuto: 0x$START - 0x$END"
    WORK_COUNT=$((WORK_COUNT + 1))

    # Esegui keyhunt
    echo "[+] Avvio ricerca..."
    START_TIME=$(date +%s)

    # Esegui keyhunt con timeout (evita blocchi)
    timeout 300 ./keyhunt -m address -f "$TARGET_FILE" -r "$START:$END" -t $(nproc) -q 2>&1 | tee /tmp/keyhunt_output_$$.txt

    END_TIME=$(date +%s)
    ELAPSED=$((END_TIME - START_TIME))

    # Controlla se trovato
    if grep -q "Hit!" /tmp/keyhunt_output_$$.txt; then
        echo -e "${GREEN}[!!!] CHIAVE TROVATA!${NC}"
        grep -A3 "Hit!" /tmp/keyhunt_output_$$.txt
        # In produzione: notifica coordinator
        break
    fi

    # Calcola statistiche
    KEYS_CHECKED=$((0x100000000))  # 4G per work unit
    TOTAL_KEYS=$((TOTAL_KEYS + KEYS_CHECKED))

    if [ $ELAPSED -gt 0 ]; then
        SPEED=$((KEYS_CHECKED / ELAPSED / 1000000))
        echo "[+] Completato in ${ELAPSED}s (~${SPEED} Mkeys/s)"
    fi

    echo "[+] Work units completati: $WORK_COUNT | Chiavi totali: $TOTAL_KEYS"
done

echo ""
echo "─────────────────────────────────────────────────────────────────"
echo "[+] Worker terminato"
echo "    Work units processati: $WORK_COUNT"
echo "    Chiavi totali controllate: $TOTAL_KEYS"
