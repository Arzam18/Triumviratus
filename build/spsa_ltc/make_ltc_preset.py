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
base = r"C:\Users\Francesco\Desktop\Triumviratus"
preset = {
    "name": "LTC1_mega46_40s",
    "label": "Mega-SPSA LTC 7.1 -- 46 parametri, 40+0.4, 76 partite concorrenti, 22k iterazioni",
    "description": ("SPSA a TC lungo richiesto dopo l'analisi CCRL del 26/09 (23 errori d'orizzonte: la 7.0 li evita con piu' tempo). "
                    "46 leve continue di pruning, LMR (LMRFine completa), estensioni singolari e history, init dai valori compilati "
                    "della 7.1 (canary 273477). Esclusi: feature spente (LMRDeepK=0, QFutility, BrilliantSac...), TM v1, LMR legacy, "
                    "interi con range <= 4. lr 0.02 e c al 20% del range da sim_server_math.py. Il verdetto e' SOLO l'SPRT del "
                    "vettore finale contro il default a 40+0.4 (gate_ltc1.ps1)."),
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
json.dump(preset, open(r"..\spsa_lab\presets\LTC1_mega46_40s.json", "w"), indent=1)
for p in params:
    print(f"{p['name']:22s} {p['init']:6d}  [{p['min']:6d}, {p['max']:6d}]  c_end {p['c_end']:7.1f} ({100*p['c_end']/p['init']:.0f}% init)")
print(len(params), "parametri")
