"""Per ogni parametro del preset MEGA70: valore COMPILATO in Triumviratus_8.0 (non il default UCI) e bound UCI.
Riusa le regex di spsa_lab/sync_uci_defaults.py."""
import re, os, json, sys
ROOT = r"C:\Users\Francesco\Desktop\Triumviratus\Triumviratus_8.0"
src = {f: open(os.path.join(ROOT, f), encoding="utf-8", errors="replace").read() for f in ("threads.cpp", "uci_mt.cpp", "nnue_bridge.cpp", "tt.h")}
allsrc = "\n".join(src.values())
DISPATCH = re.compile(r'strcmp\(name,\s*"([A-Za-z0-9_]+)"\)\s*\)\s*\{?\s*([A-Za-z_][A-Za-z0-9_]*)\s*=')
DEFINT = re.compile(r'^\s*(?:static\s+)?(?:int|bool)\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([^;]+);', re.M)
OPT = re.compile(r'option name (\S+) type spin default (-?\d+) min (-?\d+) max (-?\d+)')
disp = dict(DISPATCH.findall(allsrc))
defs = {}
for v, e in DEFINT.findall(allsrc):
    e = re.sub(r"//.*", "", e).strip()
    defs.setdefault(v, e)
opts = {n: (int(d), int(lo), int(hi)) for n, d, lo, hi in OPT.findall(src["uci_mt.cpp"])}
names = [p["name"] for p in json.load(open(sys.argv[1]))["params"]]
out = {}
for n in names:
    var = disp.get(n); comp = defs.get(var) if var else None
    o = opts.get(n)
    try: comp_i = int(comp)
    except (TypeError, ValueError): comp_i = None
    out[n] = {"var": var, "compiled": comp_i, "compiled_raw": comp, "uci": o}
    print(f"{n:24s} {str(var):28s} compiled={comp!s:10s} uci={o}")
json.dump(out, open("compiled_values.json", "w"), indent=1)
