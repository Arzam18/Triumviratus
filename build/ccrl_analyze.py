"""Analisi delle partite CCRL con Stockfish come arbitro (26/09/2026).

Uso: python ccrl_analyze.py <games.pgn> <stockfish.exe> <out.json> [--nodes 300000] [--jobs 20] [--name Triumviratus]
Per ogni partita: converte le mosse SAN in UCI (Stockfish `d` + `go perft 1`, niente librerie di scacchi),
valuta ogni posizione con `go nodes N` e salva la curva della valutazione dal punto di vista del Bianco,
il colore del nostro motore, il risultato e il numero di pezzi a ogni semimossa.
"""
import argparse
import json
import re
import subprocess
from concurrent.futures import ThreadPoolExecutor


class SF:
    def __init__(self, exe):
        self.p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        self.w("uci"); self.until("uciok")
        self.w("setoption name Threads value 1"); self.w("setoption name Hash value 16")
        self.w("isready"); self.until("readyok")

    def w(self, c):
        self.p.stdin.write(c + "\n"); self.p.stdin.flush()

    def until(self, prefix):
        out = []
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError("stockfish uscito")
            out.append(line.rstrip("\n"))
            if line.startswith(prefix):
                return out

    def state(self, moves):
        self.w("position startpos" + (" moves " + " ".join(moves) if moves else ""))
        self.w("d")
        fen = next(l[5:].strip() for l in self.until("Checkers") if l.startswith("Fen:"))
        self.w("go perft 1")
        legal = [l.split(":")[0].strip() for l in self.until("Nodes searched") if re.match(r"^[a-h][1-8][a-h][1-8][qrbn]?:", l)]
        return fen, legal

    def evaluate(self, moves, nodes):
        self.w("position startpos" + (" moves " + " ".join(moves) if moves else ""))
        self.w(f"go nodes {nodes}")
        score = None
        for l in self.until("bestmove"):
            m = re.search(r" score (cp|mate) (-?\d+)", l)
            if m and " bound" not in l:
                score = int(m.group(2)) if m.group(1) == "cp" else (100000 - abs(int(m.group(2)))) * (1 if int(m.group(2)) > 0 else -1)
        return score

    def quit(self):
        try:
            self.w("quit"); self.p.wait(timeout=5)
        except Exception:
            self.p.kill()


def board_from_fen(fen):
    rows = fen.split()[0].split("/")
    sq = {}
    for r, row in enumerate(rows):
        f = 0
        for ch in row:
            if ch.isdigit():
                f += int(ch)
            else:
                sq["abcdefgh"[f] + str(8 - r)] = ch
                f += 1
    return sq


def san_to_uci(san, fen, legal):
    s = re.sub(r"[+#!?]", "", san)
    side = fen.split()[1]
    if s in ("O-O", "0-0"):
        return "e1g1" if side == "w" else "e8g8"
    if s in ("O-O-O", "0-0-0"):
        return "e1c1" if side == "w" else "e8c8"
    board = board_from_fen(fen)
    promo = ""
    m = re.search(r"=?([QRBN])$", s)
    if m and s[0] not in "QRBNK" or (m and "=" in s):
        promo = m.group(1).lower(); s = s[:m.start()]
    piece = s[0] if s[0] in "NBRQK" else "P"
    body = s[1:] if piece != "P" else s
    body = body.replace("x", "")
    dest = body[-2:]; dis = body[:-2]
    cands = []
    for u in legal:
        if u[2:4] != dest or (u[4:] != promo):
            continue
        pc = board.get(u[0:2], "")
        if pc.upper() != piece:
            continue
        if dis and any((c in "abcdefgh" and u[0] != c) or (c in "12345678" and u[1] != c) for c in dis):
            continue
        cands.append(u)
    return cands[0] if len(cands) == 1 else None


def parse_games(path):
    txt = open(path, encoding="utf-8", errors="replace").read()
    out = []
    for g in re.split(r"\n(?=\[Event )", txt):
        tags = dict(re.findall(r'\[(\w+) "([^"]*)"\]', g))
        if "Result" not in tags or "\n\n" not in g:
            continue
        body = g.split("\n\n", 1)[1]
        body = re.sub(r"\{[^}]*\}|\([^)]*\)|\$\d+", " ", body)
        toks = [t for t in body.split() if not re.match(r"^\d+\.+$", t) and t not in ("1-0", "0-1", "1/2-1/2", "*")]
        toks = [re.sub(r"^\d+\.+", "", t) for t in toks]
        out.append((tags, [t for t in toks if t]))
    return out


def analyse(args, game):
    tags, sans = game
    sf = SF(args.sf)
    try:
        moves, evals, pieces = [], [], []
        for san in sans:
            fen, legal = sf.state(moves)
            u = san_to_uci(san, fen, legal)
            if u is None:
                return {"tags": tags, "error": f"SAN non convertita: {san} in {fen}"}
            sc = sf.evaluate(moves, args.nodes)
            stm_white = fen.split()[1] == "w"
            evals.append(None if sc is None else (sc if stm_white else -sc))
            pieces.append(sum(c.isalpha() for c in fen.split()[0]))
            moves.append(u)
        fen, _ = sf.state(moves)
        sc = sf.evaluate(moves, args.nodes)
        stm_white = fen.split()[1] == "w"
        evals.append(None if sc is None else (sc if stm_white else -sc))
        pieces.append(sum(c.isalpha() for c in fen.split()[0]))
        return {"tags": tags, "moves": moves, "evals_white": evals, "pieces": pieces,
                "we_white": args.name in tags.get("White", "")}
    finally:
        sf.quit()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pgn"); ap.add_argument("sf"); ap.add_argument("out")
    ap.add_argument("--nodes", type=int, default=300000)
    ap.add_argument("--jobs", type=int, default=20)
    ap.add_argument("--name", default="Triumviratus")
    args = ap.parse_args()
    games = parse_games(args.pgn)
    import threading
    done = [0]
    lock = threading.Lock()

    def work(g):
        r = analyse(args, g)
        with lock:  # avanzamento in tempo reale (26/09: prima non stampava nulla fino alla fine)
            done[0] += 1
            print(f"{done[0]}/{len(games)} partite", flush=True)
        return r

    with ThreadPoolExecutor(args.jobs) as ex:
        res = list(ex.map(work, games))
    json.dump(res, open(args.out, "w"))
    print(f"partite {len(res)}  errori di conversione {sum(1 for r in res if 'error' in r)}")


if __name__ == "__main__":
    main()
