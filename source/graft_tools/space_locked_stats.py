"""Statistica leggera: voci Space/LockedPawns attive e cambiate per mossa (definizioni del trainer, a1 = 0),
con varianti di formato. Radice + figli di ogni FEN, tutte le mosse legali (e il sottoinsieme delle catture)."""
import sys
import chess

A = 0x0101010101010101
H = A << 7
M64 = (1 << 64) - 1
CF = (A << 2) | (A << 3) | (A << 4) | (A << 5)
REGW = CF & 0x00000000FFFFFF00  # traverse 2-4 (a1 = 0)
REGB = CF & 0x00FFFFFF00000000  # traverse 5-7


def masks(wp, bp):
    wa = (((wp & ~A) << 7) | ((wp & ~H) << 9)) & M64
    ba = ((bp & ~A) >> 9) | ((bp & ~H) >> 7)
    sw = REGW & ~wp & ~ba
    sb = REGB & ~bp & ~wa
    bw = ((wp >> 8) | (wp >> 16) | (wp >> 24))
    bb = ((bp << 8) | (bp << 16) | (bp << 24)) & M64
    rams = wp & (bp >> 8)
    return sw, sb, bw, bb, rams


def pc(x):
    return bin(x).count("1")


def feats(b):
    wp = int(b.pieces(chess.PAWN, chess.WHITE))
    bp = int(b.pieces(chess.PAWN, chess.BLACK))
    sw, sb, bw, bb, rams = masks(wp, bp)
    space = set()
    nobeh = set()
    for S, safe, beh in ((0, sw, bw), (1, sb, bb)):
        x = safe
        while x:
            s = (x & -x).bit_length() - 1
            x &= x - 1
            space.add((S, s, (beh >> s) & 1))
            nobeh.add((S, s))
    noatt = set()
    for S, own, beh, reg in ((0, wp, bw, REGW), (1, bp, bb, REGB)):
        x = reg & ~own
        while x:
            s = (x & -x).bit_length() - 1
            x &= x - 1
            noatt.add((S, s, (beh >> s) & 1))
    filec = {(S, f, pc(safe & (A << f))) for S, safe in ((0, sw), (1, sb)) for f in range(2, 6)}
    cnt = {(S, pc(safe) + pc(safe & beh)) for S, safe, beh in ((0, sw, bw), (1, sb, bb))}
    cnt7 = {(S, min(6, (pc(safe) + pc(safe & beh)) // 3)) for S, safe, beh in ((0, sw, bw), (1, sb, bb))}
    lock = set()
    lockf = set()
    x = rams
    while x:
        s = (x & -x).bit_length() - 1
        x &= x - 1
        lock.add(s)
        lockf.add(s & 7)
    lockcnt = {pc(rams)}
    return dict(noatt=noatt, filec=filec, space=space, nobeh=nobeh, cnt=cnt, cnt7=cnt7, lock=lock, lockf=lockf, lockcnt=lockcnt)


KEYS = ["noatt", "filec", "space", "nobeh", "cnt", "cnt7", "lock", "lockf", "lockcnt"]


def run(path):
    fens = [l.strip() for l in open(path) if l.strip()]
    act = {k: 0 for k in KEYS}
    npos = 0
    tot = {k: 0 for k in KEYS}
    totc = {k: 0 for k in KEYS}
    nmv = npev = ncap = ncappev = 0
    for fen in fens:
        root = chess.Board(fen.split(" moves")[0])
        boards = [root]
        for m in list(root.legal_moves):
            c = root.copy(stack=False)
            c.push(m)
            boards.append(c)
        for b in boards:
            f0 = feats(b)
            npos += 1
            for k in KEYS:
                act[k] += len(f0[k])
            for m in b.legal_moves:
                pawn_ev = b.piece_type_at(m.from_square) == chess.PAWN or b.piece_type_at(m.to_square) == chess.PAWN \
                          or b.is_en_passant(m)
                cap = b.is_capture(m)
                nmv += 1
                ncap += cap
                if not pawn_ev:
                    continue
                npev += 1
                ncappev += cap
                c = b.copy(stack=False)
                c.push(m)
                f1 = feats(c)
                for k in KEYS:
                    d = len(f0[k] ^ f1[k])  # tolte + aggiunte = righe per prospettiva
                    tot[k] += d
                    if cap:
                        totc[k] += d
    print(f"== {path}: {npos} posizioni, {nmv} mosse, eventi di pedone {npev / nmv:.1%}, "
          f"catture {ncap / nmv:.1%} (di cui con pedoni {ncappev / max(ncap, 1):.1%})")
    for k in KEYS:
        print(f"  {k:8s} attive {act[k] / npos:5.2f}  righe/evento di pedone {tot[k] / npev:5.2f}  "
              f"righe/mossa {tot[k] / nmv:5.3f}  righe/cattura {totc[k] / max(ncap, 1):5.3f}")


for p in sys.argv[1:]:
    run(p)
