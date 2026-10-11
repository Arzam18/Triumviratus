"""Controllo delle formule in numerazione del motore (a8 = 0) di pawn_grafts.h (_wip graft_space_locked) contro il
riferimento del trainer verify_blocks.py: righe attive per prospettiva e diff incrementale (tolte/aggiunte) per ogni
mossa che tocca pedoni. Solo Python, nessun motore."""
import sys
import chess

sys.path.insert(0, r"C:\Users\Francesco\Desktop\Triumviratus\Training_NNUE\graft_blocks")
import verify_blocks as vb  # noqa: E402

M64 = (1 << 64) - 1
FA = 0x0101010101010101
FH = FA << 7
CF = (FA << 2) | (FA << 3) | (FA << 4) | (FA << 5)
REGW = CF & 0x00FFFFFF00000000
REGB = CF & 0x00000000FFFFFF00

SPACE_SQ = [0] * 64
for s in range(64):
    row, f = s >> 3, s & 7
    if 2 <= f <= 5:
        if 4 <= row <= 6:
            SPACE_SQ[s] = (6 - row) * 4 + (f - 2)
        elif 1 <= row <= 3:
            SPACE_SQ[s] = (row - 1) * 4 + (f - 2)


def eng(bb_trainer):  # a1 = 0 -> a8 = 0 (casa ^ 56 = byteswap)
    return int.from_bytes(bb_trainer.to_bytes(8, "little"), "big")


def masks(wp, bp):
    wa = ((wp & ~FA) >> 9) | ((wp & ~FH) >> 7)
    ba = (((bp & ~FA) << 7) | ((bp & ~FH) << 9)) & M64
    safe = [REGW & ~wp & ~ba, REGB & ~bp & ~wa]
    behind = [((wp << 8) | (wp << 16) | (wp << 24)) & M64, (bp >> 8) | (bp >> 16) | (bp >> 24)]
    rams = wp & ((bp << 8) & M64)
    return safe, behind, rams


def bits(b):
    while b:
        s = (b & -b).bit_length() - 1
        b &= b - 1
        yield s


def orient_tbl(ksq_eng):  # SQ_A1 = 0 / SQ_H1 = 7 (rete), dipende solo dalla colonna
    return 7 if (ksq_eng & 7) >= 4 else 0


def space_rows(S, b, behind, persp, mm):
    return [(24 if S != persp else 0) + ((behind >> s) & 1) * 12 + (SPACE_SQ[s] ^ mm) for s in bits(b)]


def locked_rows(rams, persp, o):
    b = rams if persp == 0 else rams >> 8
    return [(s ^ o) - 8 for s in bits(b)]


def active(wp, bp, persp, ksq):
    o = orient_tbl(ksq) ^ (56 * (1 - persp))
    mm = o & 3
    safe, behind, rams = masks(wp, bp)
    sp = space_rows(0, safe[0], behind[0], persp, mm) + space_rows(1, safe[1], behind[1], persp, mm)
    return sorted(sp), sorted(locked_rows(rams, persp, o))


def diff(wb, bb_, wa, ba, persp, ksq):
    o = orient_tbl(ksq) ^ (56 * (1 - persp))
    mm = o & 3
    kb, ka = masks(wb, bb_), masks(wa, ba)
    rem, add = [], []
    for S in (0, 1):
        chg = kb[1][S] ^ ka[1][S]
        rem += space_rows(S, kb[0][S] & (~ka[0][S] | chg), kb[1][S], persp, mm)
        add += space_rows(S, ka[0][S] & (~kb[0][S] | chg), ka[1][S], persp, mm)
    lrem = locked_rows(kb[2] & ~ka[2], persp, o)
    ladd = locked_rows(ka[2] & ~kb[2], persp, o)
    return sorted(rem), sorted(add), sorted(lrem), sorted(ladd)


def pawns_eng(b):
    return eng(int(b.pieces(chess.PAWN, chess.WHITE))), eng(int(b.pieces(chess.PAWN, chess.BLACK)))


def ksq_eng(b, persp):
    return b.king(chess.WHITE if persp == 0 else chess.BLACK) ^ 56


bad = npos = nmv = 0
files = sys.argv[1:]
fens = list(vb.FENS)
for p in files:
    fens += [l.strip() for l in open(p) if l.strip()]
for fen in fens:
    parts = fen.split(" moves ")
    root = chess.Board(parts[0])
    for mv in (parts[1].split() if len(parts) > 1 else []):
        root.push_uci(mv)
    boards = [root]
    for m in list(root.legal_moves):
        c = root.copy(stack=False)
        c.push(m)
        boards.append(c)
    for b in boards:
        npos += 1
        wp, bp = pawns_eng(b)
        f = b.fen()
        for persp in (0, 1):
            sp, lk = active(wp, bp, persp, ksq_eng(b, persp))
            if sp != vb.space_indices(f, persp) or lk != vb.lockedpawns_indices(f, persp):
                bad += 1
                if bad < 5:
                    print("ATTIVE", f, persp, sp, vb.space_indices(f, persp), lk, vb.lockedpawns_indices(f, persp))
        for m in b.legal_moves:
            if not (b.piece_type_at(m.from_square) == chess.PAWN or b.piece_type_at(m.to_square) == chess.PAWN
                    or b.is_en_passant(m)):
                continue
            c = b.copy(stack=False)
            c.push(m)
            wa, ba = pawns_eng(c)
            nmv += 1
            for persp in (0, 1):
                k = ksq_eng(c, persp)
                if orient_tbl(k) != orient_tbl(ksq_eng(b, persp)):
                    continue  # la rete fa un refresh: niente diff incrementale
                rem, add, lrem, ladd = diff(wp, bp, wa, ba, persp, k)
                fb, fa = b.fen(), c.fen()
                sb, sa = set(vb.space_indices(fb, persp)), set(vb.space_indices(fa, persp))
                lb, la = set(vb.lockedpawns_indices(fb, persp)), set(vb.lockedpawns_indices(fa, persp))
                if rem != sorted(sb - sa) or add != sorted(sa - sb) or lrem != sorted(lb - la) or ladd != sorted(la - lb):
                    bad += 1
                    if bad < 5:
                        print("DIFF", fb, m, persp)
print(f"posizioni {npos}, mosse di pedone {nmv}, errori {bad}")
