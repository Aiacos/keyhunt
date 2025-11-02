#!/usr/bin/env python3
"""
Script per calcolare parametri BSGS ottimali basati sulla RAM disponibile
"""

import sys
import math

def format_bytes(bytes_val):
    """Formatta bytes in MB/GB"""
    mb = bytes_val / (1024 * 1024)
    if mb >= 1024:
        return f"{mb/1024:.2f} GB"
    return f"{mb:.2f} MB"

def calculate_bsgs_memory(n_value, k_factor):
    """
    Calcola memoria richiesta per BSGS

    Formula:
    M = sqrt(N)
    M * K = numero totale elementi nel bloom filter

    bloom1 = (M * K) * 3.5 bytes
    bloom2 = bloom1 / 32
    bloom3 = bloom1 / 1024
    bP_table = (M / 32 * K) * 16 bytes
    """

    m = int(math.sqrt(n_value))
    m_times_k = m * k_factor

    # Bloom filters
    bloom1_bytes = m_times_k * 3.5
    bloom2_bytes = bloom1_bytes / 32
    bloom3_bytes = bloom1_bytes / 1024

    # bP table
    bp_table_bytes = (m / 32 * k_factor) * 16

    total_bytes = bloom1_bytes + bloom2_bytes + bloom3_bytes + bp_table_bytes

    return {
        'n': n_value,
        'n_hex': hex(n_value),
        'm': m,
        'k': k_factor,
        'm_times_k': m_times_k,
        'bloom1_mb': bloom1_bytes / (1024 * 1024),
        'bloom2_mb': bloom2_bytes / (1024 * 1024),
        'bloom3_mb': bloom3_bytes / (1024 * 1024),
        'bp_table_mb': bp_table_bytes / (1024 * 1024),
        'total_mb': total_bytes / (1024 * 1024),
        'total_gb': total_bytes / (1024 * 1024 * 1024)
    }

def find_optimal_params(available_ram_gb, k_factor=2048):
    """Trova N ottimale per la RAM disponibile"""

    # Lascia 20% di margine
    target_ram_gb = available_ram_gb * 0.8

    # Prova diversi N
    candidates = [
        0x1000000000000,   # 281 trilioni
        0x400000000000,    # 70 trilioni
        0x100000000000,    # 17 trilioni
        0x40000000000,     # 4.4 trilioni
        0x10000000000,     # 1.1 trilioni
        0x4000000000,      # 274 miliardi
        0x1000000000,      # 68 miliardi
    ]

    print(f"\n{'='*70}")
    print(f"Calcolo parametri BSGS ottimali per {available_ram_gb} GB RAM")
    print(f"Target RAM (80% disponibile): {target_ram_gb:.1f} GB")
    print(f"{'='*70}\n")

    best = None

    for n in candidates:
        result = calculate_bsgs_memory(n, k_factor)

        fits = "✅" if result['total_gb'] <= target_ram_gb else "❌"

        print(f"{fits} N = {result['n_hex']}")
        print(f"   M = {result['m']:,}")
        print(f"   M * K = {result['m_times_k']:,} elementi")
        print(f"   Bloom1: {result['bloom1_mb']:.1f} MB")
        print(f"   Bloom2: {result['bloom2_mb']:.1f} MB")
        print(f"   Bloom3: {result['bloom3_mb']:.1f} MB")
        print(f"   bP table: {result['bp_table_mb']:.1f} MB")
        print(f"   TOTALE: {result['total_gb']:.2f} GB")
        print()

        if result['total_gb'] <= target_ram_gb and best is None:
            best = result

    return best

def main():
    print("="*70)
    print("Calcolatore Parametri BSGS Ottimali")
    print("="*70)

    if len(sys.argv) > 1:
        # Calcola per parametri specifici
        if len(sys.argv) >= 3:
            n_value = int(sys.argv[1], 16) if sys.argv[1].startswith('0x') else int(sys.argv[1])
            k_factor = int(sys.argv[2])
        else:
            print("Uso: python3 calculate_bsgs_params.py [N_hex] [K]")
            print("     python3 calculate_bsgs_params.py 0x400000000000 8192")
            print()
            print("O per calcolare parametri ottimali per la tua RAM:")
            print("     python3 calculate_bsgs_params.py")
            return

        result = calculate_bsgs_memory(n_value, k_factor)

        print(f"\nParametri: N = {result['n_hex']}, K = {k_factor}")
        print(f"{'='*70}")
        print(f"M (sqrt N): {result['m']:,}")
        print(f"M * K: {result['m_times_k']:,} elementi")
        print(f"\nMemoria Richiesta:")
        print(f"  Bloom filter 1: {result['bloom1_mb']:.2f} MB")
        print(f"  Bloom filter 2: {result['bloom2_mb']:.2f} MB")
        print(f"  Bloom filter 3: {result['bloom3_mb']:.2f} MB")
        print(f"  bP table:       {result['bp_table_mb']:.2f} MB")
        print(f"  {'─'*50}")
        print(f"  TOTALE:         {result['total_gb']:.2f} GB")
        print()

        if result['total_gb'] > 32:
            print("⚠️  ATTENZIONE: Richiede più di 32 GB di RAM!")
            print("   Il sistema potrebbe crashare per OOM")
        elif result['total_gb'] > 16:
            print("⚠️  Richiede molta RAM, assicurati di avere abbastanza memoria libera")
        else:
            print("✅ Parametri OK per sistemi con >= 32 GB RAM")

    else:
        # Trova parametri ottimali
        print("\nRilevamento configurazione sistema...")

        # Leggi RAM disponibile
        try:
            with open('/proc/meminfo', 'r') as f:
                for line in f:
                    if line.startswith('MemTotal:'):
                        total_kb = int(line.split()[1])
                        total_gb = total_kb / (1024 * 1024)
                        break
        except:
            print("Impossibile rilevare RAM, usando 32 GB come default")
            total_gb = 32

        print(f"RAM totale rilevata: {total_gb:.1f} GB")

        # Trova parametri ottimali per K=2048 (standard)
        print("\n" + "="*70)
        print("Parametri consigliati con K=2048:")
        best = find_optimal_params(total_gb, k_factor=2048)

        if best:
            print("="*70)
            print("COMANDO CONSIGLIATO:")
            print("="*70)
            print(f"./keyhunt -m bsgs -f tests/135.txt -b 135 \\")
            print(f"          -n {best['n_hex']} -k {best['k']} -t 16")
            print()
            print(f"Memoria richiesta: {best['total_gb']:.2f} GB / {total_gb:.1f} GB disponibili")
            print()

        # Mostra anche opzioni con K diversi
        print("\n" + "="*70)
        print("Alternative con K=4096 (più veloce ma più RAM):")
        best_4k = find_optimal_params(total_gb, k_factor=4096)

        if best_4k:
            print("="*70)
            print("COMANDO ALTERNATIVO:")
            print("="*70)
            print(f"./keyhunt -m bsgs -f tests/135.txt -b 135 \\")
            print(f"          -n {best_4k['n_hex']} -k {best_4k['k']} -t 16")
            print()

if __name__ == "__main__":
    main()
