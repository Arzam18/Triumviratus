"""Simulazione delle formule ESATTE di spsa_lab/server.py (righe 222-263) su una funzione Elo nota.

Serve a controllare lr e c_mult prima del run LTC (27/09/2026): non si puo' giudicarli dal run vero,
dove l'ottimo e' ignoto. Modello: P parametri, Elo(theta) = -sum K*((x_i - x*_i)/range_i)^2;
partite trinomiali (patta D, misurata sui log: 36-37% di iterazioni a gradiente 0 con 4 partite
=> ~72% di patte), modo mirror (theta+ contro theta-), 4 partite per iterazione.
Ogni punto: R repliche indipendenti. Si stampa l'Elo del vettore finale (media delle ultime 5%
iterazioni, come extract_vector.py) contro l'init, e la deriva di un parametro morto (K=0).
Uso: python sim_server_math.py
"""
import numpy as np

def run(P=30, K=10.0, off=0.2, lr=0.06, cfrac=0.10, c_mult=1.0, iters=10000, gps=4, D=0.72,
        R=200, alpha=0.602, gamma=0.101, astab_frac=0.1, seed=1, dead=5):
    rng = np.random.default_rng(seed)
    rng_ = 1000.0                                   # range di ogni parametro
    lo, hi = 0.0, rng_
    init = np.full((R, P), 500.0)
    opt = init + off * rng_ * np.where(rng.random((R, P)) < .5, -1, 1)
    Kv = np.full(P, K); Kv[:dead] = 0.0             # i primi `dead` parametri non contano
    c_end = cfrac * rng_ * c_mult
    A = astab_frac * iters
    a0 = lr * c_end**2 * (1 + A)**alpha
    th = init.copy()
    tail = int(iters * 0.95); acc = np.zeros_like(th); n = 0
    def elo(x): return -(Kv * ((x - opt) / rng_)**2).sum(axis=1)
    for k in range(1, iters + 1):
        ck = c_end / k**gamma; ak = a0 / (k + A)**alpha
        d = np.where(rng.random((R, P)) < .5, -1.0, 1.0)
        plus = np.clip(th + ck * d, lo, hi); minus = np.clip(th - ck * d, lo, hi)
        de = elo(plus) - elo(minus)                 # Elo di theta+ su theta-
        E = 2 / (1 + 10**(-de / 400)) - 1           # (w-l)/g atteso
        pw = (1 - D + E) / 2; pl = (1 - D - E) / 2
        u = rng.random((R, gps))
        w = (u < pw[:, None]).sum(1); l = ((u >= pw[:, None]) & (u < (pw + pl)[:, None])).sum(1)
        grad = (w - l) / gps
        th = np.clip(th + ak * grad[:, None] / (ck * d), lo, hi)
        if k > tail: acc += th; n += 1
    fin = acc / n
    gain = elo(fin) - elo(init)
    dead_drift = np.abs(fin[:, :dead] - init[:, :dead]).mean() / rng_ if dead else float('nan')
    live_err = np.abs(fin[:, dead:] - opt[:, dead:]).mean() / rng_
    return gain.mean(), gain.std(), dead_drift, live_err, -elo(init).mean()

if __name__ == "__main__":
    import sys
    print("P=30 (5 morti), ottimo a 20% del range dall'init, 4 partite/iter, patte 72%")
    print(f"{'K':>4} {'iters':>6} {'lr':>6} {'c/range':>7} | {'Elo max':>7} {'Elo presi':>9} {'sd':>5} | {'errore vivi':>11} {'deriva morti':>12}")
    for K in (10.0, 40.0):
        for iters in (10000, 23000):
            for lr in (0.015, 0.03, 0.06, 0.12):
                for cf in (0.05, 0.10, 0.20):
                    g, s, dd, le, mx = run(K=K, lr=lr, cfrac=cf, iters=iters, R=100)
                    print(f"{K:4.0f} {iters:6d} {lr:6.3f} {cf:7.2f} | {mx:7.2f} {g:9.2f} {s:5.2f} | {le:11.3f} {dd:12.3f}", flush=True)
