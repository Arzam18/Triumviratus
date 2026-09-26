"""Rapporto sulle partite CCRL analizzate da ccrl_analyze.py (26/09/2026).

Uso: python ccrl_report.py <analisi.json> [--book 16] [--clamp 1000]
- Perdita per mossa = valutazione (di SF, dal punto di vista di chi muove) prima della mossa meno valutazione dopo.
  Si confronta la NOSTRA perdita media con quella degli AVVERSARI, per fase (pezzi sulla scacchiera) e per
  fascia di valutazione: dove la nostra supera la loro, li' siamo piu' deboli.
- Sconfitte: la mossa che ha perso di piu' e la prima mossa dopo cui la valutazione resta sotto -150.
- Patte con vantaggio: partite patte in cui la valutazione e' stata >= +200 per noi per almeno 8 nostre mosse.
Le prime `--book` semimosse sono il libro d'apertura e si ignorano.
"""
import argparse
import json
import statistics as st
from collections import defaultdict


def phase(pieces):
    if pieces > 24:
        return "apertura/primo medio (>24 pezzi)"
    if pieces > 12:
        return "mediogioco (13-24)"
    if pieces > 6:
        return "finale (7-12)"
    return "finale TB (<=6)"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("json"); ap.add_argument("--book", type=int, default=16); ap.add_argument("--clamp", type=int, default=1000)
    a = ap.parse_args()
    games = [g for g in json.load(open(a.json)) if "error" not in g]
    C = a.clamp
    loss = {"noi": defaultdict(list), "avv": defaultdict(list)}
    loss_band = {"noi": defaultdict(list), "avv": defaultdict(list)}
    big = {"noi": defaultdict(int), "avv": defaultdict(int)}
    lost, drawn_adv = [], []
    for g in games:
        ev = g["evals_white"]; pcs = g["pieces"]; ww = g["we_white"]; n = len(g["moves"])
        if any(e is None for e in ev):
            continue
        evw = [max(-C, min(C, e)) for e in ev]
        res = g["tags"]["Result"]
        our_res = {"1-0": 1, "0-1": 0, "1/2-1/2": .5}[res]
        if not ww:
            our_res = 1 - our_res
        us = [e if ww else -e for e in evw]
        for i in range(a.book, n):
            white_to_move = (i % 2 == 0)
            mover_is_us = (white_to_move == ww)
            before = evw[i] if white_to_move else -evw[i]
            after = evw[i + 1] if white_to_move else -evw[i + 1]
            l = max(0, before - after)
            who = "noi" if mover_is_us else "avv"
            loss[who][phase(pcs[i])].append(l)
            band = "equilibrio (|v|<100)" if abs(before) < 100 else ("in vantaggio (>=100)" if before >= 100 else "in svantaggio (<=-100)")
            loss_band[who][band].append(l)
            if l >= 100:
                big[who][phase(pcs[i])] += 1
        if our_res == 0:
            worst = max(range(a.book, n), key=lambda i: (max(0, (us[i] - us[i + 1])) if ((i % 2 == 0) == ww) else -1))
            first_bad = next((i for i in range(a.book, n + 1) if all(u <= -150 for u in us[i:])), None)
            lost.append((g["tags"].get("White"), g["tags"].get("Black"), n, worst, us[worst], us[worst + 1], pcs[worst], first_bad,
                         pcs[first_bad] if first_bad is not None and first_bad < len(pcs) else None))
        if our_res == 0.5:
            our_idx = [i for i in range(a.book, n + 1) if ((i % 2 == 0) == ww)]
            good = [i for i in our_idx if us[i] >= 200]
            if len(good) >= 8:
                drop = next((i for i in range(good[-1], n) if us[i + 1] < 100), None)
                drawn_adv.append((g["tags"].get("White"), g["tags"].get("Black"), n, max(us), len(good),
                                  pcs[good[0]], pcs[drop] if drop is not None else None))

    print(f"partite analizzate {len(games)}\n")
    print("PERDITA MEDIA PER MOSSA (cp, SF 300k nodi)      noi     avversari   mosse (noi/avv)   errori >=1 pedone (noi/avv)")
    for ph in ["apertura/primo medio (>24 pezzi)", "mediogioco (13-24)", "finale (7-12)", "finale TB (<=6)"]:
        a_, b_ = loss["noi"][ph], loss["avv"][ph]
        if a_ and b_:
            print(f"  {ph:34s} {st.mean(a_):7.1f}   {st.mean(b_):9.1f}   {len(a_):6d}/{len(b_):<6d}      {big['noi'][ph]}/{big['avv'][ph]}")
    print("\nPER FASCIA DI VALUTAZIONE (prima della mossa, per chi muove)")
    for band in ["equilibrio (|v|<100)", "in vantaggio (>=100)", "in svantaggio (<=-100)"]:
        a_, b_ = loss_band["noi"][band], loss_band["avv"][band]
        if a_ and b_:
            print(f"  {band:34s} {st.mean(a_):7.1f}   {st.mean(b_):9.1f}   {len(a_):6d}/{len(b_):<6d}")
    print(f"\nSCONFITTE ({len(lost)}): bianco / nero / semimosse / mossa peggiore (ply, v prima -> dopo, pezzi) / crollo definitivo (ply, pezzi)")
    for w, b, n, worst, v0, v1, pc, fb, pfb in lost:
        print(f"  {w[:24]:24s} - {b[:24]:24s} {n:4d}  ply {worst:3d}: {v0:+5d} -> {v1:+5d} ({pc} pezzi)   crollo ply {fb} ({pfb} pezzi)")
    print(f"\nPATTE CON VANTAGGIO >= +200 per almeno 8 nostre mosse: {len(drawn_adv)} su {sum(1 for g in games if g['tags']['Result']=='1/2-1/2')} patte")
    for w, b, n, mx, ng, p0, pdrop in sorted(drawn_adv, key=lambda x: -x[3])[:25]:
        print(f"  {w[:24]:24s} - {b[:24]:24s} {n:4d} semimosse  max {mx:+5d}  mosse sopra +200: {ng:3d}  pezzi all'inizio {p0}  al calo {pdrop}")


if __name__ == "__main__":
    main()
