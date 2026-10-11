#!/usr/bin/env python3
# =====================================================================================================================
#  prel_base_convert.py -- PassedRel nel formato "a base" (PRB1), riparametrizzazione ESATTA, senza riaddestrare.
#  _wip graft_passedrel2, 10/10/2026. Riferimento: docs/audit_8.0/GRAFT_PASSEDREL_COSTO2.md §8.
#
#  Idea. Per ogni gruppo g = (colore relativo, casa orientata), lo stesso indice della feature PassedPawns v1
#  ((c != prospettiva ? 48 : 0) + casa orientata - 8), si sceglie uno stato base b(g) fra gli 8 stati di PassedRel
#  (bit 0 imprendibile, bit 1 collegato, bit 2 strada libera). Allora, con W = righe di PassedRel e V = riga v1:
#      V'[g]    = V[g] + W[g, b]            (la riga v1 e' attiva esattamente quando il passato c'e': stessa definizione)
#      W'[g, s] = W[g, s] - W[g, b]         (zero per s = b: quella riga il motore non la somma piu')
#  Per ogni passato V' + W'[s] = V + W[s]: somme intere, accumulatori identici bit per bit. Stesso per la PSQT (int32).
#  b(g) = 8 vuol dire "nessuna base" (gruppo lasciato com'era): e' il ripiego quando una somma esce dall'int8.
#
#  Formato del file (stesso di nnue_feature_transformer.h, read_parameters): intestazione (versione, hash, descrizione),
#  hash della sezione FT, bias (LEB128), minacce int8 grezze + PSQT (LEB128), HalfKA (LEB128) + PSQT (LEB128), PawnPair
#  int8 + PSQT, PassedPawns v1 int8 (96 righe) + PSQT, poi i blocchi da innesto della maschera (int8 + PSQT; PassedRel
#  per primo), poi le reti di uscita. Le righe nel file sono in ordine logico (permutazioni tolte da write_parameters).
#  Il formato a base aggiunge 96 byte (la tabella b) subito dopo la PSQT di PassedRel e cambia l'hash del blocco da
#  "PRV2" a "PRB1" (intestazione e sezione FT).
#
#  USO
#    python prel_base_convert.py convert IN.nnue OUT.nnue --hist prel_hist.txt [--mask 1] [--limit 127]
#    python prel_base_convert.py convert IN.nnue OUT.nnue --order 4,0,6,2,5,1,7,3      (senza istogramma)
#    python prel_base_convert.py check   OUT.nnue                                       (rilegge e stampa la tabella)
#  --hist: file scritto dal motore compilato con -DTRIUMV_PREL_HIST (variabile PREL_HIST_FILE), righe "g c0..c7",
#  blocchi chiusi da END (si sommano). Per gruppo si prova lo stato con piu' righe applicate, poi il secondo, ...; il
#  primo con tutte le somme nell'int8 (|x| <= --limit) vince; se nessuno va, b = 8.
#  --limit 127 (default): il trainer taglia a +-127, si resta nello stesso intervallo. 128 permette anche -128.
#
#  Il trainer (serializer) puo' emettere lo stesso formato: fold_prel_base() lavora sugli array gia' quantizzati (int8
#  per i pesi, int32 per la PSQT) e va chiamata dopo la quantizzazione di PassedPawns e PassedRel, prima della scrittura;
#  l'addestramento resta nella parametrizzazione originale.
# =====================================================================================================================
import sys, struct, argparse
import numpy as np

L1 = 1024
PSQT_BUCKETS = 8
THREAT_ROWS = 59808        # FullThreats::Dimensions
PAWNPAIR_ROWS = 4560       # PawnPair::Dimensions
V1_ROWS = 96               # PassedPawns::Dimensions
GRAFT_DIM = [768, 768, 48, 48]   # PassedRel, KingFiles, Space, LockedPawns (pawn_grafts.h)
H_PRV2 = 0x50525632
H_PRB1 = 0x50524231
MAGIC = b"COMPRESSED_LEB128"


def rotl32(x, k):
    k %= 32
    return ((x << k) | (x >> (32 - k))) & 0xFFFFFFFF if k else x


# --- LEB128 (come nnue_common.h: magia, u32 = byte, valori con segno) ------------------------------------------------
def leb_decode(buf, count):
    out = np.empty(count, dtype=np.int64)
    pos = 0
    for i in range(count):
        result, shift = 0, 0
        while True:
            byte = buf[pos]
            pos += 1
            result |= (byte & 0x7F) << shift
            shift += 7
            if not byte & 0x80:
                if byte & 0x40:
                    result -= 1 << shift
                break
        out[i] = result
    if pos != len(buf):
        raise ValueError("LEB128: byte avanzati (conteggio sbagliato?)")
    return out


def leb_encode(values):
    b = bytearray()
    for v in values:
        v = int(v)
        while True:
            byte = v & 0x7F
            v >>= 7
            if (v == 0 and not byte & 0x40) or (v == -1 and byte & 0x40):
                b.append(byte)
                break
            b.append(byte | 0x80)
    return bytes(b)


class Reader:
    def __init__(self, data):
        self.d, self.p = data, 0

    def take(self, n):
        if self.p + n > len(self.d):
            raise ValueError("file troncato")
        s = self.d[self.p:self.p + n]
        self.p += n
        return s

    def u32(self):
        return struct.unpack("<I", self.take(4))[0]

    def leb_raw(self):
        """Restituisce (byte grezzi del blocco LEB128 completo, payload)."""
        start = self.p
        if self.take(len(MAGIC)) != MAGIC:
            raise ValueError(f"magia LEB128 assente a {start}: struttura del file diversa da quella attesa")
        n = self.u32()
        payload = self.take(n)
        return self.d[start:self.p], payload


def leb_block(values):
    enc = leb_encode(values)
    return MAGIC + struct.pack("<I", len(enc)) + enc


# --- la trasformazione (usabile anche dal serializer del trainer) ----------------------------------------------------
def prel_row(g, st):
    """Riga del blocco PassedRel per il gruppo v1 g e lo stato st: (rel ? 384 : 0) + st * 48 + casa orientata - 8."""
    return (384 if g >= 48 else 0) + st * 48 + (g % 48)


def fold_prel_base(v1_w, v1_psqt, prel_w, prel_psqt, orders, limit=127):
    """v1_w (96, L1) int8, v1_psqt (96, 8) int32, prel_w (768, L1) int8, prel_psqt (768, 8) int32, orders[g] = stati
    candidati in ordine di preferenza. Modifica gli array in posto; restituisce (base[96], diagnostica)."""
    base = np.full(96, 8, dtype=np.uint8)
    diag = []
    for g in range(96):
        V = v1_w[g].astype(np.int32)
        W = np.stack([prel_w[prel_row(g, s)].astype(np.int32) for s in range(8)])
        chosen, why = 8, "nessuno stato entra nell'int8"
        for b in orders[g]:
            B = W - W[b]
            Vn = V + W[b]
            mB, mV = int(np.abs(B).max()), int(np.abs(Vn).max())
            if limit == 128:
                okB = B.min() >= -128 and B.max() <= 127
                okV = Vn.min() >= -128 and Vn.max() <= 127
            else:
                okB, okV = mB <= limit, mV <= limit
            if okB and okV:
                chosen, why = b, f"max|B| {mB}, max|V'| {mV}"
                break
        base[g] = chosen
        diag.append((g, chosen, why))
        if chosen == 8:
            continue
        v1_w[g] = (V + W[chosen]).astype(np.int8)
        v1_psqt[g] = v1_psqt[g] + prel_psqt[prel_row(g, chosen)]
        Wb, Pb = W[chosen].copy(), prel_psqt[prel_row(g, chosen)].copy()
        for s in range(8):
            r = prel_row(g, s)
            prel_w[r] = (W[s] - Wb).astype(np.int8)
            prel_psqt[r] = prel_psqt[r] - Pb
    return base, diag


def read_hist(path):
    h = np.zeros((96, 8), dtype=np.float64)
    with open(path) as f:
        for ln in f:
            t = ln.split()
            if len(t) == 9:
                h[int(t[0])] += np.array([float(x) for x in t[1:]])
    return h


# --- file ------------------------------------------------------------------------------------------------------------
def parse(data, mask, based=False):
    r = Reader(data)
    version, hdr_hash, dlen = r.u32(), r.u32(), r.u32()
    desc = r.take(dlen)
    ft_hash_pos = r.p
    ft_hash = r.u32()
    r.leb_raw()                                   # bias
    r.take(THREAT_ROWS * L1); r.leb_raw()         # minacce + PSQT
    r.leb_raw(); r.leb_raw()                      # HalfKA + PSQT
    r.take(PAWNPAIR_ROWS * L1); r.leb_raw()       # PawnPair + PSQT
    v1_pos = r.p
    v1_w = np.frombuffer(r.take(V1_ROWS * L1), dtype=np.int8).reshape(V1_ROWS, L1).copy()
    v1_leb_pos = r.p
    _, pl = r.leb_raw()
    v1_psqt = leb_decode(pl, V1_ROWS * PSQT_BUCKETS).reshape(V1_ROWS, PSQT_BUCKETS)
    if not mask & 1:
        raise ValueError("la maschera non contiene PassedRel")
    prel_pos = r.p
    prel_w = np.frombuffer(r.take(768 * L1), dtype=np.int8).reshape(768, L1).copy()
    prel_leb_pos = r.p
    _, pl = r.leb_raw()
    prel_psqt = leb_decode(pl, 768 * PSQT_BUCKETS).reshape(768, PSQT_BUCKETS)
    rest_pos = r.p
    if based:
        r.take(96)  # tabella degli stati base (formato PRB1)
    # i blocchi successivi della maschera e le reti di uscita si copiano cosi' come sono; si controlla almeno la
    # struttura dei blocchi da innesto che seguono
    for b in range(1, 4):
        if mask & (1 << b):
            r.take(GRAFT_DIM[b] * L1); r.leb_raw()
    return dict(version=version, hdr_hash=hdr_hash, desc=desc, ft_hash_pos=ft_hash_pos, ft_hash=ft_hash,
                v1_pos=v1_pos, v1_w=v1_w, v1_leb_pos=v1_leb_pos, v1_psqt=v1_psqt, prel_pos=prel_pos, prel_w=prel_w,
                prel_leb_pos=prel_leb_pos, prel_psqt=prel_psqt, rest_pos=rest_pos)


def convert(args):
    data = open(args.inp, "rb").read()
    m = args.mask
    P = parse(data, m)
    if args.hist:
        h = read_hist(args.hist)
        orders = [list(np.argsort(-h[g], kind="stable")) for g in range(96)]
    else:
        h = None
        o = [int(x) for x in args.order.split(",")]
        orders = [o] * 96
    v1_w, v1_psqt, prel_w, prel_psqt = P["v1_w"], P["v1_psqt"].copy(), P["prel_w"], P["prel_psqt"].copy()
    base, diag = fold_prel_base(v1_w, v1_psqt, prel_w, prel_psqt, orders, args.limit)
    for a in (v1_psqt, prel_psqt):
        if a.min() < -2**31 or a.max() >= 2**31:
            raise ValueError("PSQT fuori dall'int32 dopo la piega")

    # hash: PRV2 -> PRB1 nel passo 0 della catena; i blocchi dopo PassedRel ruotano la differenza di un bit ciascuno
    later = sum(1 for b in range(1, 4) if m & (1 << b))
    delta = rotl32(H_PRV2 ^ H_PRB1, later)
    out = bytearray()
    out += struct.pack("<III", P["version"], P["hdr_hash"] ^ delta, len(P["desc"])) + P["desc"]
    out += struct.pack("<I", P["ft_hash"] ^ delta)
    out += data[P["ft_hash_pos"] + 4:P["v1_pos"]]
    out += v1_w.astype(np.int8).tobytes()
    out += leb_block(v1_psqt.reshape(-1))
    out += prel_w.astype(np.int8).tobytes()
    out += leb_block(prel_psqt.reshape(-1))
    out += base.tobytes()
    out += data[P["rest_pos"]:]
    open(args.out, "wb").write(out)

    nb = int((base != 8).sum())
    print(f"scritto {args.out}: {nb}/96 gruppi con uno stato base, {96 - nb} senza (ripiego int8)")
    for g, b, why in diag:
        print(f"  g {g:2d} ({'avv' if g >= 48 else 'own'} casa {g % 48 + 8:2d}): base {b}  {why}")
    if h is not None:
        tot = h.sum()
        saved = sum(h[g][base[g]] for g in range(96) if base[g] != 8)
        print(f"righe PassedRel tolte sul carico dell'istogramma: {saved / tot * 100:.1f}% "
              f"({saved:.0f} su {tot:.0f})" if tot else "istogramma vuoto")
    print("verifica: il bench della rete convertita deve essere uguale a quello di", args.inp)


def check(args):
    data = open(args.inp, "rb").read()
    P = parse(data, args.mask, based=True)
    base = np.frombuffer(data[P["rest_pos"]:P["rest_pos"] + 96], dtype=np.uint8)
    zero = 0
    for g in range(96):
        if base[g] != 8:
            r = prel_row(g, int(base[g]))
            zero += int(not P["prel_w"][r].any() and not P["prel_psqt"][r].any())
    print("tabella:", list(base))
    print(f"righe base a zero: {zero}/{int((base != 8).sum())} (devono coincidere)")


def main():
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest="cmd", required=True)
    c = sp.add_parser("convert")
    c.add_argument("inp"); c.add_argument("out")
    c.add_argument("--hist", default="")
    c.add_argument("--order", default="4,0,6,2,5,1,7,3")
    c.add_argument("--mask", type=int, default=1)
    c.add_argument("--limit", type=int, default=127, choices=[127, 128])
    k = sp.add_parser("check")
    k.add_argument("inp")
    k.add_argument("--mask", type=int, default=1)
    a = ap.parse_args()
    convert(a) if a.cmd == "convert" else check(a)


if __name__ == "__main__":
    main()
