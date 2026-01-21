#!/bin/bash
#
# puzzle71_worker_hybrid.sh - Worker ottimizzato con supporto GPU
#
# Uso: ./puzzle71_worker_hybrid.sh <coordinator_ip> [porta] [gpu_percent]
#
# Esempi:
#   ./puzzle71_worker_hybrid.sh 192.168.1.100 7777      # Solo CPU
#   ./puzzle71_worker_hybrid.sh 192.168.1.100 7777 70   # 70% GPU + 30% CPU
#

COORDINATOR_IP="${1:-localhost}"
COORDINATOR_PORT="${2:-7777}"
GPU_PERCENT="${3:-0}"
WORKER_ID="$$-$(hostname)"
TARGET_FILE="puzzle71.txt"

# Colori
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# Detect hardware
CPU_THREADS=$(nproc)
HAS_GPU=$(./keyhunt -m address -f puzzle71.txt -b 66 -t 1 -s 1 -q 2>&1 | grep -c "CUDA\|GPU" || echo "0")

echo "╔══════════════════════════════════════════════════════════════╗"
echo "║        KEYHUNT PUZZLE 71 - HYBRID WORKER                     ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
echo -e "[+] Worker ID: ${CYAN}$WORKER_ID${NC}"
echo -e "[+] Coordinator: ${CYAN}$COORDINATOR_IP:$COORDINATOR_PORT${NC}"
echo -e "[+] CPU Threads: ${CYAN}$CPU_THREADS${NC}"
if [ "$GPU_PERCENT" -gt 0 ]; then
    echo -e "[+] GPU Mode: ${GREEN}ENABLED ($GPU_PERCENT%)${NC}"
else
    echo -e "[+] GPU Mode: ${YELLOW}DISABLED (CPU only)${NC}"
fi
echo ""

# Verifica file e binario
if [ ! -f "$TARGET_FILE" ]; then
    echo -e "${RED}[ERRORE] File $TARGET_FILE non trovato!${NC}"
    echo "Crea il file:"
    echo "  echo '1PWCx5fovoEaoBowAvF5k91m2Xat9bMgwb' > puzzle71.txt"
    exit 1
fi

if [ ! -x "./keyhunt" ]; then
    echo -e "${RED}[ERRORE] keyhunt non trovato o non eseguibile!${NC}"
    exit 1
fi

# Work unit size: 4G chiavi (~80 secondi su CPU, ~8 secondi su GPU)
WORK_UNIT_SIZE="100000000"  # 0x100000000 = 4.29 miliardi

# Funzione per ottenere lavoro
get_work() {
    local WORK_FILE="/tmp/keyhunt_work_${WORKER_ID}"

    if [ -f "$WORK_FILE" ]; then
        source "$WORK_FILE"
        NEXT_START=$(printf "%x" $((0x$CURRENT_END + 1)))
    else
        NEXT_START="400000000000000000"
    fi

    # Fine del range (work unit size)
    NEXT_END=$(printf "%x" $((0x$NEXT_START + 0x$WORK_UNIT_SIZE - 1)))

    # Verifica se abbiamo superato il range del puzzle 71
    if [ $((16#$NEXT_START)) -ge $((16#7FFFFFFFFFFFFFFFFF)) ]; then
        echo "NO_MORE_WORK"
        return 1
    fi

    # Salva stato per recovery
    echo "CURRENT_START=$NEXT_START" > "$WORK_FILE"
    echo "CURRENT_END=$NEXT_END" >> "$WORK_FILE"

    echo "$NEXT_START:$NEXT_END"
    return 0
}

# Costruisci comando keyhunt ottimale
build_keyhunt_cmd() {
    local START=$1
    local END=$2

    CMD="./keyhunt -m address -f $TARGET_FILE -r $START:$END"
    CMD="$CMD -t $CPU_THREADS"
    CMD="$CMD -l compress"  # Solo compressed (2x più veloce)
    CMD="$CMD -q"           # Quiet mode

    # Aggiungi GPU se richiesto
    if [ "$GPU_PERCENT" -gt 0 ]; then
        CMD="$CMD -G $GPU_PERCENT"
    fi

    echo "$CMD"
}

# Loop principale
TOTAL_KEYS=0
WORK_COUNT=0
TOTAL_TIME=0

trap "echo ''; echo -e '${YELLOW}[!] Interruzione...${NC}'; exit 0" SIGINT SIGTERM

while true; do
    echo ""
    echo -e "${YELLOW}[+] Richiedo lavoro...${NC}"

    RANGE=$(get_work)

    if [ "$RANGE" == "NO_MORE_WORK" ]; then
        echo -e "${GREEN}[✓] Range completato!${NC}"
        break
    fi

    START=$(echo $RANGE | cut -d: -f1)
    END=$(echo $RANGE | cut -d: -f2)

    WORK_COUNT=$((WORK_COUNT + 1))
    echo -e "[+] Work unit #$WORK_COUNT: ${CYAN}0x$START${NC} - ${CYAN}0x$END${NC}"

    # Costruisci ed esegui comando
    KEYHUNT_CMD=$(build_keyhunt_cmd $START $END)
    echo "[+] Comando: $KEYHUNT_CMD"

    START_TIME=$(date +%s.%N)

    # Esegui con timeout (5 minuti max per work unit)
    OUTPUT=$(timeout 300 $KEYHUNT_CMD 2>&1)
    EXIT_CODE=$?

    END_TIME=$(date +%s.%N)
    ELAPSED=$(echo "$END_TIME - $START_TIME" | bc)
    TOTAL_TIME=$(echo "$TOTAL_TIME + $ELAPSED" | bc)

    # Controlla se trovato
    if echo "$OUTPUT" | grep -q "Hit!"; then
        echo ""
        echo -e "${GREEN}╔══════════════════════════════════════════════════════════════╗${NC}"
        echo -e "${GREEN}║                    🎉 CHIAVE TROVATA! 🎉                      ║${NC}"
        echo -e "${GREEN}╚══════════════════════════════════════════════════════════════╝${NC}"
        echo "$OUTPUT" | grep -A5 "Hit!"

        # Salva risultato
        echo "$OUTPUT" > "FOUND_KEY_$(date +%Y%m%d_%H%M%S).txt"

        # TODO: Notifica coordinator via TCP
        break
    fi

    # Calcola statistiche
    KEYS_CHECKED=$((0x$WORK_UNIT_SIZE))
    TOTAL_KEYS=$((TOTAL_KEYS + KEYS_CHECKED))

    if [ "$(echo "$ELAPSED > 0" | bc)" -eq 1 ]; then
        SPEED=$(echo "scale=2; $KEYS_CHECKED / $ELAPSED / 1000000" | bc)
        AVG_SPEED=$(echo "scale=2; $TOTAL_KEYS / $TOTAL_TIME / 1000000" | bc)
        echo -e "[+] Completato in ${CYAN}${ELAPSED}s${NC} - ${GREEN}${SPEED} Mkeys/s${NC} (avg: ${AVG_SPEED} Mkeys/s)"
    fi

    # Progress
    PROGRESS_HEX=$(printf "%x" $((0x$END - 0x400000000000000000)))
    TOTAL_RANGE=$((0x7FFFFFFFFFFFFFFFFF - 0x400000000000000000))
    CURRENT_POS=$((0x$END - 0x400000000000000000))
    PROGRESS_PCT=$(echo "scale=6; $CURRENT_POS * 100 / $TOTAL_RANGE" | bc 2>/dev/null || echo "0")

    echo -e "[+] Work units: ${CYAN}$WORK_COUNT${NC} | Keys: ${CYAN}$TOTAL_KEYS${NC} | Progress: ${CYAN}${PROGRESS_PCT}%${NC}"
done

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo -e "[+] Worker terminato"
echo -e "    Work units: ${CYAN}$WORK_COUNT${NC}"
echo -e "    Chiavi totali: ${CYAN}$TOTAL_KEYS${NC}"
echo -e "    Tempo totale: ${CYAN}${TOTAL_TIME}s${NC}"
if [ "$(echo "$TOTAL_TIME > 0" | bc)" -eq 1 ]; then
    FINAL_SPEED=$(echo "scale=2; $TOTAL_KEYS / $TOTAL_TIME / 1000000" | bc)
    echo -e "    Velocità media: ${GREEN}${FINAL_SPEED} Mkeys/s${NC}"
fi
