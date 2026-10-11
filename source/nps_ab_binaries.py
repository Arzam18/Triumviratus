#!/usr/bin/env python3
# Come nps_ab_test.py ma confronta DUE BINARI diversi (stesso identico protocollo:
# N posizioni casuali dal libro, depth fissa, repeats, ucinewgame per run, NPS
# aggregato = somma nodi / somma tempo, stesse posizioni/ordine su entrambi i lati).
import argparse, random, re, subprocess, sys, time

ap = argparse.ArgumentParser()
ap.add_argument("engine_a")
ap.add_argument("engine_b")
ap.add_argument("--n", type=int, default=150)
ap.add_argument("--depth", type=int, default=15)
ap.add_argument("--repeats", type=int, default=2)
ap.add_argument("--book", default=r"C:\Users\franc\source\repos\Triumviratus_3.0\OpeningBooks\uho_2024\UHO_2024_+085_+094\UHO_2024_8mvs_big_+080_+099.epd")
ap.add_argument("--seed", type=int, default=42)
ap.add_argument("--net", default=r"C:\Users\franc\source\repos\Triumviratus_3.0\Triumviratus_5\x64\Release\nn-rubicon-alea-v1.nnue")
args = ap.parse_args()

with open(args.book, encoding="utf-8", errors="replace") as f:
    lines = [l.strip() for l in f if l.strip()]
random.seed(args.seed)
fens = random.sample(lines, min(args.n, len(lines)))
print(f"[setup] {len(fens)} posizioni, depth={args.depth}, repeats={args.repeats}", flush=True)

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

def run_side(engine_path, label):
    eng = Engine(engine_path)
    total_nodes, total_ms, done = 0, 0, 0
    for rep in range(args.repeats):
        for fen in fens:
            nodes, ms = eng.search_depth(fen, args.depth)
            total_nodes += nodes; total_ms += max(1, ms); done += 1
            if done % 50 == 0:
                nps_now = total_nodes * 1000 // max(1, total_ms)
                print(f"  [{label}] {done}/{len(fens)*args.repeats} nps_aggregato={nps_now}", flush=True)
    eng.quit()
    return total_nodes, total_ms

print(f"\n=== LATO A: {args.engine_a} ===", flush=True)
n_a, t_a = run_side(args.engine_a, "A")
nps_a = n_a * 1000 // max(1, t_a)
print(f"A: nodi={n_a} tempo={t_a}ms nps_aggregato={nps_a}", flush=True)

print(f"\n=== LATO B: {args.engine_b} ===", flush=True)
n_b, t_b = run_side(args.engine_b, "B")
nps_b = n_b * 1000 // max(1, t_b)
print(f"B: nodi={n_b} tempo={t_b}ms nps_aggregato={nps_b}", flush=True)

print(f"\n=== RISULTATO ===")
print(f"nodi identici: {n_a == n_b}  ({n_a} vs {n_b})")
print(f"NPS A = {nps_a}")
print(f"NPS B = {nps_b}")
print(f"Delta (B vs A) = {100.0*(nps_b-nps_a)/nps_a:+.2f}%")
