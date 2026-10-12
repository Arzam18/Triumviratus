#!/usr/bin/env python3
# =============================================================================
# A/B NPS test rigoroso per una singola opzione UCI (default: SeeGE) su MOLTE
# posizioni diverse, a depth fissa, con ripetizioni. Molto piu' stabile del
# "bench" a 8 posizioni: NPS aggregato = somma(nodi) / somma(tempo) su tutte
# le posizioni*ripetizioni, non media di NPS per-posizione (che e' rumorosa).
#
# USO:
#   python nps_ab_test.py <engine.exe> [--n 150] [--depth 15] [--repeats 2]
#                          [--option SeeGE] [--book <path.epd>] [--seed 42]
#
# Ogni posizione: "ucinewgame" (TT/history puliti) + "position fen X" +
# "go depth D", si legge l'ultima riga "info depth D ... nodes N ... time T"
# prima di "bestmove". Stesso identico ordine/seed di posizioni per OFF e ON,
# cosi' il confronto e' appaiato (stesso lavoro su entrambi i lati).
# =============================================================================
import argparse, random, re, subprocess, sys, time

ap = argparse.ArgumentParser()
ap.add_argument("engine")
ap.add_argument("--n", type=int, default=150)
ap.add_argument("--depth", type=int, default=15)
ap.add_argument("--repeats", type=int, default=2)
ap.add_argument("--option", default="SeeGE")
ap.add_argument("--book", default=r"C:\Users\franc\source\repos\Triumviratus_3.0\OpeningBooks\uho_2024\UHO_2024_+085_+094\UHO_2024_8mvs_big_+080_+099.epd")
ap.add_argument("--seed", type=int, default=42)
ap.add_argument("--net", default=r"C:\Users\franc\source\repos\Triumviratus_3.0\Triumviratus_5\x64\Release\nn-rubicon-alea-v1.nnue")
args = ap.parse_args()

with open(args.book, encoding="utf-8", errors="replace") as f:
    lines = [l.strip() for l in f if l.strip()]
random.seed(args.seed)
fens = random.sample(lines, min(args.n, len(lines)))
print(f"[setup] {len(fens)} posizioni, depth={args.depth}, repeats={args.repeats}, "
      f"option={args.option}, engine={args.engine}", flush=True)

INFO_RE = re.compile(r"info depth (\d+) .*? nodes (\d+) .*? time (\d+)")

class Engine:
    def __init__(self, path):
        self.p = subprocess.Popen([path], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   text=True, bufsize=1)
        self._send("uci"); self._wait("uciok")
        self._send(f"setoption name EvalFile value {args.net}")
        self._send("setoption name Hash value 64")
        self._send("setoption name Threads value 1")

    def _send(self, cmd):
        self.p.stdin.write(cmd + "\n"); self.p.stdin.flush()

    def _wait(self, token):
        while True:
            line = self.p.stdout.readline()
            if not line: raise RuntimeError("engine died")
            if token in line: return

    def set_option(self, name, value):
        self._send(f"setoption name {name} value {value}")

    def search_depth(self, fen, depth):
        self._send("ucinewgame")
        self._send("isready"); self._wait("readyok")
        self._send(f"position fen {fen}")
        self._send(f"go depth {depth}")
        nodes, elapsed_ms = 0, 0
        while True:
            line = self.p.stdout.readline()
            if not line: raise RuntimeError("engine died mid-search")
            m = INFO_RE.search(line)
            if m and int(m.group(1)) == depth:
                nodes, elapsed_ms = int(m.group(2)), int(m.group(3))
            if line.startswith("bestmove"):
                return nodes, elapsed_ms

    def quit(self):
        self._send("quit")
        try: self.p.wait(timeout=5)
        except Exception: self.p.kill()

def run_side(option_value):
    eng = Engine(args.engine)
    eng.set_option(args.option, option_value)
    total_nodes, total_ms, done = 0, 0, 0
    t0 = time.time()
    for rep in range(args.repeats):
        for fen in fens:
            nodes, ms = eng.search_depth(fen, args.depth)
            total_nodes += nodes; total_ms += max(1, ms); done += 1
            if done % 50 == 0:
                nps_now = total_nodes * 1000 // max(1, total_ms)
                print(f"  [{args.option}={option_value}] {done}/{len(fens)*args.repeats} "
                      f"nps_aggregato={nps_now}", flush=True)
    eng.quit()
    wall = time.time() - t0
    return total_nodes, total_ms, wall

print(f"\n=== LATO A: {args.option}=false ===", flush=True)
n_off, t_off, w_off = run_side("false")
nps_off = n_off * 1000 // max(1, t_off)
print(f"OFF: nodi={n_off} tempo_motore={t_off}ms nps_aggregato={nps_off} wall={w_off:.1f}s", flush=True)

print(f"\n=== LATO B: {args.option}=true ===", flush=True)
n_on, t_on, w_on = run_side("true")
nps_on = n_on * 1000 // max(1, t_on)
print(f"ON:  nodi={n_on} tempo_motore={t_on}ms nps_aggregato={nps_on} wall={w_on:.1f}s", flush=True)

delta = 100.0 * (nps_on - nps_off) / nps_off
print(f"\n=== RISULTATO ===")
print(f"NPS OFF = {nps_off}")
print(f"NPS ON  = {nps_on}")
print(f"Delta   = {delta:+.2f}%")
