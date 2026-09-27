"""Preset del mega-SPSA a TC lungo per Triumviratus 7.1 (27/09/2026).

- init = valori COMPILATI in Triumviratus_7.1 (compiled_values.json da extract_params.py), mai i default UCI
  (per gli LMRF* il default UCI e' stale: es. LMRFCut annuncia 4629, compilato 3687).
- bound = init x [0.5, 1.5], intersecati con i bound UCI del motore.
- c_end = 20% del range del preset (= 20% dell'init quando i bound non sono tagliati), limitato dall'anti-clamp.
  In server.py c_end e' la perturbazione INIZIALE: a 22.000 iterazioni scende di 22000^0.101 = 2,75x.
- lr = 0.02, non 0.06-0.08 dei run di settembre: vedi sim_server_math.py e LEGGIMI.md.
"""
import json
vals = json.load(open("compiled_values.json"))
names = [p["name"] for p in json.load(open("names.json"))["params"]]
params = []
for n in names:
    v = vals[n]; init = v["compiled"]; _, ulo, uhi = v["uci"]
    lo = max(ulo, round(init * 0.5)); hi = min(uhi, round(init * 1.5))
    c = round(min(0.20 * (hi - lo), init - lo, hi - init), 1)
    assert c / 22000 ** 0.101 >= 0.5, n
    params.append({"name": n, "init": init, "min": lo, "max": hi, "c_end": c})

# 28/09/2026 (decisione dell'utente): le idee di Coda non reggono "innestate" (bolt-on) con gli SPRT a 10+0.1
# (TTNearMiss +2,4 ± 7,2 su 4.216; bundle NearMiss+Damp −1,6 ± 18,8 su 634) perche' i parametri intorno sono stati
# tarati senza di loro (es. TTCutRefine chiede +1 ply ai fail-high, il near-miss ne accetta -1). Entrano quindi
# ACCESE e si ritarano insieme ai vicini (re-basin). Ognuna ha una via continua verso lo "spento", cosi' e' lo SPSA a
# giudicarla: margine near-miss alto = quasi mai; peso TTDamp alto = score TT quasi puro; coefficienti RDR -> 0.
# Il verdetto resta il gate: vettore tarato (novita' accese) contro la 7.1 di default (spente).
extra = [  # nome, init (valore acceso), min, max
    ("TTNearMiss", 80, 30, 400),
    ("TTDamp", 31, 10, 200),
    ("RDRRfp", 20, 0, 60),
    ("RDRLmp", 5, 0, 20),
    ("RDRProbCut", 5, 0, 20),
    ("RDRKnee", 17, 12, 24),
    ("TTCutFifty", 89, 60, 100),        # vicini TT tarati senza near-miss
    ("TTCutBonusScale", 111, 55, 166),
]
for n, init, lo, hi in extra:
    c = round(min(0.20 * (hi - lo), init - lo, hi - init), 1)
    assert c / 22000 ** 0.101 >= 0.5, n
    params.append({"name": n, "init": init, "min": lo, "max": hi, "c_end": c})
base = r"C:\Users\Francesco\Desktop\Triumviratus"
preset = {
    "name": "LTC2_mega54_40s",
    "label": "Mega-SPSA LTC 7.1 -- 54 parametri (46 + idee di Coda accese e vicini TT), 40+0.4, 76 partite concorrenti, 22k iterazioni",
    "description": ("SPSA a TC lungo richiesto dopo l'analisi CCRL del 26/09 (23 errori d'orizzonte: la 7.0 li evita con piu' tempo). "
                    "46 leve continue di pruning, LMR (LMRFine completa), estensioni singolari e history, init dai valori compilati "
                    "della 7.1 (canary 273477), piu' TTNearMiss, TTDamp e RDR* ACCESI e TTCutFifty/TTCutBonusScale (re-basin del "
                    "28/09: innestate da sole non reggono). Esclusi: feature spente (LMRDeepK=0, QFutility, BrilliantSac...), TM v1, "
                    "LMR legacy, interi con range <= 4. lr 0.02 e c al 20% del range da sim_server_math.py. Il verdetto e' SOLO "
                    "l'SPRT del vettore finale contro il default (novita' spente) a 40+0.4 (gate_ltc1.ps1)."),
    "mode": "mirror",
    "params": params,
    "spsa": {"lr_default": 0.02, "c_mult": 1.0, "alpha": 0.602, "gamma": 0.101, "astab_frac": 0.1, "max_iters": 22000},
    "match": {"tc": "40+0.4", "games_per_side": 4, "conc_per_side": 4, "async_workers": 19,
              "resign_score": 600, "resign_movecount": 3, "hash_mb": 128, "move_overhead": 100, "timemargin": 150,
              "book": base + r"\OpeningBooks\UHO_4060_v4\UHO_4060_v4.epd", "book_lines": 240000},
    "engines": {"fastchess": base + r"\Fastechess_For_SPSA\fastchess.exe",
                "test": {"cmd": base + r"\Triumviratus_7.1\x64\Release\Triumviratus_7.1_spsaltc_avx512.exe", "fixed_options": {}},
                "opponent": {"cmd": base + r"\Triumviratus_7.1\x64\Release\Triumviratus_7.1_spsaltc_avx512.exe", "fixed_options": {}}},
}
json.dump(preset, open(r"..\spsa_lab\presets\LTC2_mega54_40s.json", "w"), indent=1)

# 28/09/2026, decisione dell'utente: "tarare tutto con lo SPSA, tranne i parametri per cui serve uno SPSA a 40 s o
# piu'". Preset M20: 20+0.2 (il minimo della metodologia; a 12+0.12 il vecchio mega-SPSA si era sovra-adattato),
# SENZA le RDR* (agiscono solo con root depth > soglia: a 20+0.2 la radice supera 17 nel 10% delle mosse).
# Le RDR restano per LTC2 a 40+0.4 o per l'SPRT dedicato.
import copy
m20 = copy.deepcopy(preset)
m20["params"] = [p for p in params if not p["name"].startswith("RDR")]
m20["name"] = "M20_mega50_20s"
m20["label"] = "Mega-SPSA 7.1 -- 50 parametri (senza RDR*), 20+0.2, 76 partite concorrenti, 22k iterazioni (~1 giorno)"
m20["description"] = preset["description"].replace("RDR* ACCESI e ", "").replace("a 40+0.4 (gate_ltc1.ps1)", "a 20+0.2, poi conferma a 40+0.4 (gate_ltc1.ps1 -TC)")
m20["match"]["tc"] = "20+0.2"
m20["match"]["hash_mb"] = 64
json.dump(m20, open(r"..\spsa_lab\presets\M20_mega50_20s.json", "w"), indent=1)
print(len(m20["params"]), "parametri nel preset M20 (20+0.2)")
for p in params:
    print(f"{p['name']:22s} {p['init']:6d}  [{p['min']:6d}, {p['max']:6d}]  c_end {p['c_end']:7.1f} ({100*p['c_end']/p['init']:.0f}% init)")
print(len(params), "parametri")
