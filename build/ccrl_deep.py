"""Rianalisi profonda delle nostre mosse sospette nelle partite CCRL (26/09/2026).

Uso: python ccrl_deep.py <analisi.json> <stockfish.exe> <out.json> [--nodes 3000000] [--jobs 20] [--book 16]
Candidati (nostre mosse), dalla curva a 300k nodi di ccrl_analyze.py:
  - sconfitte: dalla 16a semimossa prima del crollo definitivo (valutazione sempre <= -150) a 4 dopo;
  - patte con vantaggio (>= +100 per 8+ nostre mosse): dal picco a quando la valutazione torna sotto +50;
  - tutte le partite: nostre mosse dopo cui, in DUE semimosse (nostra + risposta), la valutazione scende di >= 100 cp.
Per ogni candidato: SF a N nodi sulla posizione prima della mossa (score + mossa migliore) e dopo (score);
perdita = score_prima - score_dopo (dal nostro punto di vista). Stampa gli errori confermati (>= 80 cp).
"""
import argparse
import json
import re
import subprocess
from concurrent.futures import ThreadPoolExecutor
import threading


def sf_open(exe):
    p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
    return p


def w(p, c):
    p.stdin.write(c + "\n"); p.stdin.flush()


def until(p, prefix):
    out = []
    while True:
        l = p.stdout.readline()
        if not l:
            raise RuntimeError("sf uscito")
        out.append(l.rstrip("\n"))
        if l.startswith(prefix):
            return out


def search(p, moves, nodes):
    w(p, "position startpos" + (" moves " + " ".join(moves) if moves else ""))
    w(p, f"go nodes {nodes}")
    sc, best = None, None
    for l in until(p, "bestmove"):
        m = re.search(r" score (cp|mate) (-?\d+)", l)
        if m and " bound" not in l:
            v = int(m.group(2))
            sc = v if m.group(1) == "cp" else (10000 - abs(v)) * (1 if v > 0 else -1)
        if l.startswith("bestmove"):
            best = l.split()[1]
    return sc, best


def fen_of(p, moves):
    w(p, "position startpos" + (" moves " + " ".join(moves) if moves else ""))
    w(p, "d")
    return next(l[5:].strip() for l in until(p, "Checkers") if l.startswith("Fen:"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("json"); ap.add_argument("sf"); ap.add_argument("out")
    ap.add_argument("--nodes", type=int, default=3000000)
    ap.add_argument("--jobs", type=int, default=20)
    ap.add_argument("--book", type=int, default=16)
    a = ap.parse_args()
    games = [g for g in json.load(open(a.json)) if "error" not in g and all(e is not None for e in g["evals_white"])]
    C = 1000
    cand = {}  # (gi, ply) -> motivo
    for gi, g in enumerate(games):
        ww = g["we_white"]; n = len(g["moves"])
        us = [max(-C, min(C, e)) if ww else -max(-C, min(C, e)) for e in g["evals_white"]]
        res = {"1-0": 1, "0-1": 0, "1/2-1/2": .5}[g["tags"]["Result"]]
        if not ww:
            res = 1 - res
        ours = [i for i in range(a.book, n) if (i % 2 == 0) == ww]
        if res == 0:
            fb = next((i for i in range(a.book, n + 1) if all(u <= -150 for u in us[i:])), n)
            for i in ours:
                if fb - 16 <= i <= fb + 4:
                    cand.setdefault((gi, i), "sconfitta")
        if res == .5 and sum(1 for i in ours if us[i] >= 100) >= 8:
            pk = max(range(a.book, n + 1), key=lambda i: us[i])
            end = next((i for i in range(pk, n + 1) if us[i] < 50), n)
            for i in ours:
                if pk <= i <= end:
                    cand.setdefault((gi, i), "patta con vantaggio")
        for i in ours:
            if i + 2 <= n and us[i] - us[i + 2] >= 100:
                cand.setdefault((gi, i), "salto in 2 semimosse")
    items = sorted(cand.items())
    print(f"candidati {len(items)} in {len(set(k[0] for k, _ in items))} partite", flush=True)
    local = threading.local()
    done = [0]; lock = threading.Lock()

    def work(item):
        (gi, i), why = item
        if not hasattr(local, "p"):
            local.p = sf_open(a.sf); w(local.p, "uci"); until(local.p, "uciok")
            w(local.p, "setoption name Threads value 1"); w(local.p, "setoption name Hash value 64")
            w(local.p, "isready"); until(local.p, "readyok")
        p = local.p; g = games[gi]; mv = g["moves"]
        w(p, "ucinewgame")
        s0, best = search(p, mv[:i], a.nodes)
        s1, _ = search(p, mv[:i + 1], a.nodes)
        fen = fen_of(p, mv[:i])
        with lock:
            done[0] += 1
            if done[0] % 25 == 0:
                print(f"{done[0]}/{len(items)}", flush=True)
        opp = g["tags"]["Black"] if g["we_white"] else g["tags"]["White"]
        return {"game": gi, "ply": i, "why": why, "opp": opp.replace(" 64-bit", ""), "result": g["tags"]["Result"],
                "we_white": g["we_white"], "fen": fen, "our": mv[i], "best": best, "before": s0,
                "after": None if s1 is None else -s1, "loss": None if s0 is None or s1 is None else s0 + s1,
                "pieces": g["pieces"][i]}

    with ThreadPoolExecutor(a.jobs) as ex:
        res = list(ex.map(work, items))
    json.dump(res, open(a.out, "w"), indent=1)
    conf = [r for r in res if r["loss"] is not None and r["loss"] >= 80 and r["our"] != r["best"]]
    print(f"\nerrori confermati a {a.nodes} nodi (perdita >= 80 cp, mossa diversa da quella di SF): {len(conf)} su {len(res)} candidati")
    for r in sorted(conf, key=lambda r: -r["loss"]):
        print(f"  [{r['why'][:9]}] {r['opp'][:18]:18s} ply {r['ply']:3d} pezzi {r['pieces']:2d}  {r['our']} (SF {r['best']})  "
              f"{r['before']:+5d} -> {r['after']:+5d}  = -{r['loss']}  {r['fen']}")


if __name__ == "__main__":
    main()
