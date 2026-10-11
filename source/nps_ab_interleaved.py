#!/usr/bin/env python3
"""NPS fra due binari NODE-IDENTICAL, interlacciati POSIZIONE PER POSIZIONE.

Perche' non nps_ab_binaries.py: quello esegue TUTTE le posizioni di A e poi tutte quelle di B.
Su un laptop una misura seria dura decine di minuti di AVX512 continuo, la CPU si scalda e il
turbo cala -> il secondo lato gira in uno stato termico diverso dal primo, e quella deriva
finisce DRITTA nella differenza A-B.

Misurato il 29/07 su questa macchina, a caricatore collegato e senza altri carichi:
  ordine 1:  665.612 -> 718.059   (+7,9% per il secondo)
  ordine 2:  529.953 -> 563.573   (+6,3% per il secondo, e -20% in assoluto sul run prima)
Il segno seguiva la POSIZIONE, non la patch.

Qui si alterna A,B sulla STESSA posizione, con entrambi i motori vivi per tutta la durata
(niente ricaricamento degli 87 MB di rete). Due misure appaiate distano meno di un secondo:
la deriva termica agisce quasi identica su entrambe e si cancella nel rapporto.

Si riporta la MEDIANA dei rapporti per-posizione, non il rapporto delle somme: una singola
posizione disturbata da un processo di sistema sposta la somma e non la mediana. E si stampa
la frazione di posizioni in cui B vince, che e' un test del segno indipendente dalla scala.

USO:
  python nps_ab_interleaved.py <exeA> <exeB> --net <rete.nnue> [--n 200] [--depth 14]
"""
import argparse
import math
import random
import re
import statistics as st
import subprocess
import sys

# Windows: la console e' cp1252 e un emoji nei messaggi finali faceva morire lo script
# con UnicodeEncodeError DOPO aver stampato il risultato (traceback al posto del verdetto).
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

ap = argparse.ArgumentParser()
ap.add_argument("engine_a")
ap.add_argument("engine_b")
ap.add_argument("--n", type=int, default=60)
ap.add_argument("--depth", type=int, default=18)   # servono ricerche da SECONDI: vedi search()
ap.add_argument("--book", default=r"C:\Users\franc\source\repos\Triumviratus_3.0\OpeningBooks\UHO_4060_v4\UHO_4060_v4.epd")
ap.add_argument("--net", required=True)
ap.add_argument("--net-b", default=None,
                help="Rete per il lato B. Serve per confrontare DUE RETI sullo stesso "
                     "binario (es. permutata vs originale): senza, entrambi i lati "
                     "caricherebbero la stessa rete e la misura sarebbe di due processi "
                     "identici.")
ap.add_argument("--hash", type=int, default=256,
                help="MB di Hash. Default 256 = il regime CCRL. Con 64 si sottostimano "
                     "le ottimizzazioni di localita': la TT non fa pressione sulla L3.")
ap.add_argument("--seed", type=int, default=42)
ap.add_argument("--opt-a", action="append", default=[], metavar="NOME=VALORE",
                help="setoption da inviare al SOLO lato A (ripetibile)")
ap.add_argument("--opt-b", action="append", default=[], metavar="NOME=VALORE",
                help="setoption da inviare al SOLO lato B (ripetibile). Con --opt-b su "
                     "un binario identico ai due lati si misura la sola leva, senza "
                     "varianza di build.")
args = ap.parse_args()

INFO_RE = re.compile(r"info depth (\d+) .*? nodes (\d+) .*? time (\d+)")


class Engine:
    def __init__(self, path, net, hash_mb=256, opts=None):
        self.p = subprocess.Popen([path], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  text=True, bufsize=1)
        self.announced = set()
        self._send("uci"); self._wait("uciok", collect=True)
        self._send(f"setoption name EvalFile value {net}")
        # 🔴 Hash 256 (era 64): CCRL gira con 128-256 MB, e una TT di quella taglia
        # scorre in L3 e sfratta i pesi della rete. Misurare con Hash 64 significa
        # misurare in un regime dove la cache e' molto piu' libera di quanto sia in
        # lista — e sottostimare qualunque ottimizzazione che agisce sulla LOCALITA'.
        self._send(f"setoption name Hash value {hash_mb}")
        self._send("setoption name Threads value 1")
        # 🔴 5/08/2026: opzioni per-lato, cosi' l'A/B puo' girare su UN SOLO binario con
        # una leva a RUNTIME. Due build PGO diverse introducono una varianza di layout che
        # quel giorno ha prodotto un falso z=5,31 (42/150) sparito ricostruendo lo stesso
        # codice (69/150). Stesso binario = quella varianza non esiste per costruzione.
        for kv in (opts or []):
            name, _, value = kv.partition("=")
            if name not in self.announced:
                raise SystemExit(
                    "STOP: il motore " + path + " NON annuncia l'opzione '" + name + "'. "
                    "Verrebbe ignorata IN SILENZIO e i due lati girerebbero IDENTICI. "
                    "Serve la build DEV: la release annuncia solo 12 opzioni.")
            self._send(f"setoption name {name} value {value}")

    def _send(self, cmd):
        self.p.stdin.write(cmd + "\n"); self.p.stdin.flush()

    def _wait(self, token, collect=False):
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError("engine died")
            if collect and line.startswith("option name "):
                # 🔴 Serve per il controllo qui sotto: un motore IGNORA IN SILENZIO una
                # setoption che non ha annunciato (stessa trappola nota di fastchess).
                # Senza questo, un --opt-b su una build release farebbe giocare due lati
                # IDENTICI e misureremmo zero chiamandolo "patch nulla".
                self.announced.add(line.split("option name ", 1)[1].split(" type ")[0].strip())
            if token in line:
                return

    def search(self, fen, depth):
        """Ritorna (nodi, millisecondi) dal campo `time` del motore, che misura la SOLA
        ricerca — piu' pulito di un cronometro esterno, che ci infilerebbe dentro il round
        trip UCI e lo scheduling.
        🔴 Il campo e' in MILLISECONDI INTERI, quindi serve che le ricerche siano LUNGHE. A
        depth 14 molte durano 30-80 ms, dove un 5% vale 1,5-4 ms: le coppie cadono sullo
        stesso intero, il rapporto viene esattamente 1,0 e la mediana esce +0.00% su qualunque
        campione (sembra un bug di calcolo, e' la risoluzione). A depth 18 si va sui secondi e
        il problema sparisce. Lo script controlla e avvisa (vedi il gate a fine run)."""
        self._send("ucinewgame")
        self._send("isready"); self._wait("readyok")
        self._send(f"position fen {fen}")
        self._send(f"go depth {depth}")
        nodes = ms = 0
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError("engine died mid-search")
            m = INFO_RE.search(line)
            if m and int(m.group(1)) == depth:
                nodes, ms = int(m.group(2)), int(m.group(3))
            if line.startswith("bestmove"):
                # ms == 0 non e' misurabile: si segnala invece di spacciare 1 ms per dato.
                return nodes, ms

    def quit(self):
        self._send("quit")
        try:
            self.p.wait(timeout=5)
        except Exception:
            self.p.kill()


with open(args.book, encoding="utf-8", errors="replace") as f:
    lines = [ln.strip() for ln in f if ln.strip()]
random.seed(args.seed)
fens = random.sample(lines, min(args.n, len(lines)))

A = Engine(args.engine_a, args.net, args.hash, args.opt_a)
B = Engine(args.engine_b, args.net_b or args.net, args.hash, args.opt_b)
if args.opt_a or args.opt_b:
    print(f"[setup] opzioni  A: {args.opt_a or '(nessuna)'}   B: {args.opt_b or '(nessuna)'}")
print(f"[setup] {len(fens)} posizioni, depth={args.depth}, interlacciate A/B\n", flush=True)

ratios, na_tot, nb_tot, ta_tot, tb_tot, b_wins = [], 0, 0, 0, 0, 0
times_a = []
mismatch = 0
ties = 0
zero_ms = 0
r_afirst, r_bfirst = [], []   # rapporti separati per ordine di esecuzione dentro la coppia
for i, fen in enumerate(fens, 1):
    # 🔴 ORDINE ALTERNATO (5/08/2026). Prima era fisso A,B con la motivazione "la deriva
    # fra due ricerche adiacenti (<1 s) e' trascurabile". FALSO, misurato due volte nello
    # stesso giorno con un controllo nullo (stesso binario, stesse opzioni):
    #     macchina che si RAFFREDDA dopo una build -> B (secondo) +5,72%, 135/150, z=9,72
    #     macchina fredda che si SCALDA            -> B (secondo) -3,5%
    # Chi sta nello slot fisso "secondo" eredita TUTTA la deriva, in qualunque verso.
    # Alternando, meta' delle coppie ha A per primo e meta' B: la deriva si cancella nel
    # campione invece di accumularsi su un lato. E la differenza fra i due sottoinsiemi
    # DIVENTA la misura dell'effetto d'ordine (vedi diagnostica a fine run).
    a_first = (i % 2 == 1)
    if a_first:
        na, ta = A.search(fen, args.depth)
        nb, tb = B.search(fen, args.depth)
    else:
        nb, tb = B.search(fen, args.depth)
        na, ta = A.search(fen, args.depth)
    if na != nb:
        mismatch += 1
    if ta == 0 or tb == 0:
        # Prima si faceva max(1, ms): fabbricava un millisecondo e lo trattava come dato.
        zero_ms += 1
        continue
    na_tot += na; nb_tot += nb; ta_tot += ta; tb_tot += tb
    times_a.append(ta)
    # NPS per posizione, poi rapporto: le posizioni facili non pesano piu' delle difficili.
    r = (nb / tb) / (na / ta)
    ratios.append(r)
    (r_afirst if a_first else r_bfirst).append(r)
    if r > 1:
        b_wins += 1
    elif r == 1:
        ties += 1   # pareggio esatto (ms interi): NON e' una vittoria di A
    if i % 10 == 0:
        print(f"  {i}/{len(fens)}  mediana B/A = {(st.median(ratios)-1)*100:+.2f}%", flush=True)

A.quit(); B.quit()

print(f"\nnodi: A={na_tot}  B={nb_tot}  {'IDENTICI' if na_tot == nb_tot else 'DIVERSI'}")
if mismatch:
    print(f"⚠️ {mismatch} posizioni con conteggio nodi diverso: NON sono node-identical,")
    print("   il confronto NPS non isola la velocita'.")

med = st.median(ratios)
print(f"NPS aggregato:  A = {na_tot*1000//ta_tot:,}   B = {nb_tot*1000//tb_tot:,}")
print(f"\n=== B vs A: {(med-1)*100:+.2f}%   (mediana dei rapporti per-posizione, n={len(ratios)}) ===")
print(f"    B piu' veloce in {b_wins}/{len(ratios)} posizioni ({100*b_wins/len(ratios):.1f}%)")
med_ms = st.median(times_a)
print(f"    tempo mediano per ricerca: {med_ms/1000:.2f}s")
if med_ms < 500:
    print(f"    🔴 RICERCHE TROPPO CORTE ({med_ms} ms): il campo `time` e' in ms INTERI, quindi")
    print(f"       un {abs(med-1)*100:.1f}% vale ~{med_ms*abs(med-1):.1f} ms e si perde nell'arrotondamento.")
    print( "       Rilancia con --depth piu' alta (18+). Il numero qui sopra NON e' affidabile.")
q1, q3 = st.quantiles(ratios, n=4)[0], st.quantiles(ratios, n=4)[2]
print(f"    quartili: {(q1-1)*100:+.2f}% .. {(q3-1)*100:+.2f}%")
# Test del segno: non dipende dalla scala ed e' il numero che decide (la mediana puo'
# sembrare grande e reggersi su poche posizioni). Si stampa il p-value vero invece della
# vecchia banda 40-60%, che era troppo grossolana: 89/150 = 59,3% cadeva dentro la banda
# e veniva chiamato "rumore" pur avendo z = 2,29 (p = 0,022). Normale con correzione di
# continuita': con n >= 30 l'approssimazione alla binomiale e' piu' che sufficiente.
# 🔴 I PAREGGI SI ESCLUDONO (5/08/2026). Prima `if r > 1` li lasciava nel denominatore
# senza mai assegnarli a B: con `time` in ms INTERI i pareggi esatti sono frequenti, e
# ogni pareggio spingeva la frazione verso il basso = bias sistematico CONTRO B. Il test
# del segno canonico li toglie da n.
n_r = len(ratios) - ties
if ties:
    print(f"    pareggi esatti esclusi dal test: {ties}/{len(ratios)}")
# max(0,...): con b_wins esattamente a meta' la correzione di continuita' rende z
# negativo e erfc darebbe p > 1 (visto: 75/150 -> p = 1,0651).
z = max(0.0, (abs(b_wins - n_r / 2) - 0.5) / math.sqrt(n_r * 0.25)) if n_r else 0.0
p = math.erfc(z / math.sqrt(2))  # bilaterale
print(f"    test del segno: z = {z:.2f}, p = {p:.4f} (bilaterale, n={n_r} senza pareggi)")

# --- DIAGNOSTICA D'ORDINE: lo strumento si autocontrolla -------------------------
# Con l'ordine alternato le coppie "A per primo" e "B per primo" sono due stime dello
# STESSO effetto. Se divergono, la macchina sta derivando e il numero sopra non vale:
# e' lo stesso difetto che il 5/08/2026 ha prodotto un falso z=9,72 su un controllo nullo.
if r_afirst and r_bfirst:
    m_af = (st.median(r_afirst) - 1) * 100
    m_bf = (st.median(r_bfirst) - 1) * 100
    print("")
    print(f"    [ordine] A-primo: {m_af:+.2f}% ({len(r_afirst)} pos)   "
          f"B-primo: {m_bf:+.2f}% ({len(r_bfirst)} pos)   "
          f"divergenza: {abs(m_af-m_bf):.2f}%")
    if abs(m_af - m_bf) > 2 * abs(med - 1) * 100 + 0.5:
        print("    🔴 I DUE ORDINI NON CONCORDANO: la macchina sta derivando (transitorio")
        print("       termico? carico?). La misura NON e' affidabile — far assestare la")
        print("       macchina e rilanciare. Mai misurare subito dopo una build.")
if zero_ms:
    print(f"    ⚠️ {zero_ms} posizioni scartate: il motore ha riportato time=0 ms.")
if p > 0.05:
    print("    -> NON significativo: non distinguibile dal rumore.")
elif p > 0.01:
    print("    -> significativo ma DEBOLE: raddoppia il campione (--seed diverso) prima di bakare.")
else:
    print("    -> significativo.")
