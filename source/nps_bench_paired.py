#!/usr/bin/env python3
"""Confronto NPS fra due binari NODE-IDENTICAL, alternando A/B per annullare la deriva.

Perche' non nps_ab_binaries.py: quello esegue TUTTE le posizioni di A e poi tutte quelle di B,
quindi qualunque deriva nell'arco del run (turbo, temperatura, altri processi) finisce dritta
nella differenza A-B. Misurato il 29/07: chi corre secondo appare piu' veloce dell'1-4,8%, e il
segno del confronto si RIBALTA scambiando l'ordine.

Qui si alterna A,B,A,B,... e si confrontano le COPPIE ADIACENTI: una deriva lenta agisce quasi
uguale sui due membri della coppia e si cancella. Si usa la MEDIANA, non la media: un singolo
run rallentato da un processo di sistema sposta la media e non la mediana.

⚠️ Vale SOLO fra binari node-identical (il bench deve dare lo stesso conteggio nodi). Se i nodi
differiscono il confronto non ha senso e lo script si ferma.

USO:  python nps_bench_paired.py <exeA> <exeB> [--rounds 15]
"""
import argparse
import re
import statistics as st
import subprocess
import sys
import time

ap = argparse.ArgumentParser()
ap.add_argument("exe_a")
ap.add_argument("exe_b")
ap.add_argument("--rounds", type=int, default=15)
args = ap.parse_args()

NODES_RE = re.compile(r"[Nn]odes searched\s*:\s*(\d+)")


def bench(path):
    """Un bench in un processo FRESCO: non e' idempotente nello stesso processo
    (killer/node_cache/prev_pv/eval_cache/Finny non si azzerano con ucinewgame)."""
    t0 = time.perf_counter()
    p = subprocess.run([path], input="bench\nquit\n", capture_output=True, text=True, timeout=300)
    el = time.perf_counter() - t0
    m = NODES_RE.search(p.stdout)
    if not m:
        sys.exit(f"nessun 'Nodes searched' da {path}:\n{p.stdout[-500:]}")
    return int(m.group(1)), el


na, _ = bench(args.exe_a)
nb, _ = bench(args.exe_b)
print(f"nodi A = {na}   nodi B = {nb}")
if na != nb:
    sys.exit("❌ NODI DIVERSI: i due binari non sono node-identical, il confronto NPS non ha senso.")
print(f"✅ node-identical. {args.rounds} round alternati A/B\n")

ratios, ta_all, tb_all = [], [], []
for i in range(args.rounds):
    _, ta = bench(args.exe_a)
    _, tb = bench(args.exe_b)
    ta_all.append(ta)
    tb_all.append(tb)
    ratios.append(ta / tb)          # >1 = B piu' veloce
    print(f"  round {i+1:2d}:  A {ta:6.3f}s   B {tb:6.3f}s   B/A = {(ta/tb - 1)*100:+6.2f}%")

med = st.median(ratios)
print(f"\nmediana A: {st.median(ta_all):.3f}s   mediana B: {st.median(tb_all):.3f}s")
print(f"NPS A: {na/st.median(ta_all):,.0f}   NPS B: {nb/st.median(tb_all):,.0f}")
print(f"\n=== B vs A: {(med - 1) * 100:+.2f}%  (mediana dei rapporti appaiati, n={len(ratios)}) ===")
lo, hi = min(ratios), max(ratios)
print(f"    dispersione dei rapporti: {(lo-1)*100:+.2f}% .. {(hi-1)*100:+.2f}%")
if lo < 1 < hi:
    print("    ⚠️ i rapporti cambiano SEGNO fra i round: l'effetto e' sotto il rumore.")
