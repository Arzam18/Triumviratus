# Mega-SPSA a TC lungo (LTC1), preparato il 27/09/2026. Non ancora lanciato.

**Perché.** La rianalisi delle partite CCRL Blitz della 7.0 (`docs/audit_8.0/D_CORRETTEZZA.md`) ha trovato
23 errori reali. Sono errori d'orizzonte: la 7.0 ne evita 17 con 10 secondi e 20 con 60 secondi. SF tara
ancora con SPSA a TC lungo, mentre il nostro mega-SPSA "saturo" girava a TC corto e con reti precedenti.

## File
| file | cosa fa |
|---|---|
| `LANCIA_LTC1.ps1` | build dev PGO → controllo canary 273477 e opzioni → check_preset_bounds / check_perturbations / smoke_preset → server + run |
| `gate_ltc1.ps1 -Run <cartella>` | estrae il vettore (media delle ultime 1.100 iterazioni) e lancia l'SPRT `[0, 3]` a 40+0.4 contro il default, in finestra |
| `make_ltc_preset.py` | genera `spsa_lab/presets/LTC1_mega46_40s.json` da `names.json` + `compiled_values.json` |
| `extract_params.py` | legge dal sorgente 8.0 i valori **compilati** e i bound UCI (i default UCI degli LMRF* sono stale) |
| `sim_server_math.py` | simulazione delle formule esatte di `server.py` su una funzione Elo nota |

## Impostazioni
- **46 parametri**: pruning e margini (RFP, futility, razoring, LMP, NMP, ProbCut, SEE, CapFut, QS delta,
  HistPrune), LMR (base, div, LMRFine completa: 14 termini), singolari doppia/tripla, history (bonus, malus, pesi).
- **Esclusi** secondo la blacklist di `SPSA_METHODOLOGY.md`:
  - feature spente (`LMRDeepK=0` ha init = min, quindi l'anti-clamp non lo farebbe mai muovere);
  - TM v1 e LMR legacy (`AggrLMR*`, `TTPvAmount`);
  - interi con range ≤ 4, che vanno decisi con SPRT discreti.
- **Bound**: init × [0,5; 1,5], dentro i bound UCI.
- **c_end**: 20% del range, cioè ~20% dell'init. È la perturbazione **iniziale**: alla fine è 2,75 volte più piccola.
- **Match**: 40+0.4, mirror, 4 partite per iterazione, 19 worker × 4 = 76 partite concorrenti, Hash 128, libro UHO_4060_v4.
- **Iterazioni**: 22.000 (~88k partite, ~48 ore).
- **lr 0,02.**

## Verifica di lr e c (richiesta dall'utente, 27/09)
Il codice di `server.py` è stato letto riga per riga (222–263):
- `c_k = c_end/k^0,101`;
- `a_k = lr·c_end²·(1+A)^0,602/(k+A)^0,602`;
- `θ += a_k·grad/(c_k·Δ)`.

Le formule sono quelle di Spall. Ci sono tre particolarità:
1. `c_end` è la perturbazione **iniziale**, non quella finale come in fishtest.
2. Il passo iniziale vale `lr·c_end·grad`, dove `grad` è la media per partita.
3. In mirror, `grad` è il punteggio di θ+ contro θ−, cioè il doppio di (y+ − y−)/2. Rispetto a external,
   il lr effettivo è quindi raddoppiato.

Non ci sono bug nel codice. **Il problema è la regola "lr non tocca l'SNR"** (legge 1 della metodologia).
Vale solo se il gradiente è costante. Vicino all'ottimo il gradiente si annulla, e il rumore fa vagare θ
con un'ampiezza che cresce con lr. Ogni parametro che vaga via dall'ottimo costa Elo.

Simulazione (`sim_server_math.py`):
- 30 parametri, di cui 5 morti;
- ottimo al 20% del range dall'init;
- 72% di patte, il valore misurato sui log di settembre (gradiente nullo nel 36–37% delle iterazioni);
- Elo del vettore finale, mediato su 100 repliche.

| paesaggio | iterazioni | lr 0,015 · c 20% | lr 0,03 · c 20% | lr 0,06 · c 10% (run di settembre) | lr 0,12 · c 10% |
|---|---|---|---|---|---|
| piatto (10 Elo disponibili) | 10k | +3,1 | +3,3 | **−0,3** | −5,1 |
| piatto | 23k | **+5,0** | +4,4 | **−0,9** | −4,4 |
| ripido (40 Elo) | 10k | +33,3 | +35,3 | +27,0 | +23,8 |
| ripido | 23k | **+37,4** | +35,7 | +30,4 | +24,1 |

Cosa dice la tabella:
- Con lr 0,06–0,08, cioè come i run L1/L2 di settembre, in un paesaggio piatto il vettore finale **perde Elo**:
  il rumore vince sul segnale.
- Con c al 20% del range e lr fra 0,015 e 0,03 si guadagna in tutti e due i casi.
- La deriva dei parametri morti cresce con lr: circa 0,09 del range a lr 0,015, circa 0,24 a lr 0,12.

Scelta: **lr 0,02 e c al 20%**. Può spiegare perché gli SPSA recenti non hanno reso: il vettore LMRFine del
07/09 è stato annullato.

⚠️ Il modello non ha interazioni fra i parametri e ha un ottimo solo. È un controllo di plausibilità sulle
scale, non una previsione di Elo.

## 28/09/2026 (sera): preset M20, predefinito di `LANCIA_LTC1.ps1`
Decisione dell'utente: "tarare tutto con lo SPSA, tranne i parametri per cui serve uno SPSA a 40 s o più".
- **Preset `M20_mega50_20s`:** i 54 di LTC2 **senza le 4 RDR** (agiscono solo a radice profonda), 20+0.2 (il minimo
  della metodologia), Hash 64, 22.000 iterazioni, circa 1 giorno a macchina libera.
- **Lancio:** `LANCIA_LTC1.ps1` (predefinito M20; `-Preset LTC2_mega54_40s` per l'altro).
- **Gate:** prima `gate_ltc1.ps1 -Run <cartella> -TC 20+0.2`, poi, se passa, conferma a 40+0.4.
- **Le RDR:** restano per LTC2 o per l'SPRT dedicato a 40+0.4 con soglia 17.

## 28/09/2026: preset LTC2 (54 parametri)
Gli SPRT "innestati" a 10+0.1 hanno dato:
- `TTNearMiss`: +2,4 ± 7,2 su 4.216 partite;
- bundle con `TTDamp`: −1,6 ± 18,8 su 634;
- bundle `PvTTMinDepth` + `SingularPlyGuard`: −5,7 ± 15,4 su 918.

Decisione dell'utente: la ritaratura serve sempre, quindi le idee continue entrano **accese** nello SPSA a TC lungo,
con limiti che permettono di tornare verso lo "spento" (tabella sotto, colonna limiti aggiornata). Si aggiungono
anche `TTCutFifty` (89, 60–100) e `TTCutBonusScale` (111, 55–166), vicini TT tarati senza near-miss.
Generatore: `make_ltc_preset.py` (lista `extra`). Il gate resta vettore tarato contro default (novità spente).

## Idee di Coda: prima gli SPRT, poi eventualmente nel preset (27/09/2026)
`sprt_coda_ideas.ps1 -Idea 1|2|3` (40+0.4, `[0, 3]`, stesso binario dev). Le opzioni sono spente di default e hanno
init 0 = min. Un parametro spento non si mette nel preset: l'anti-clamp gli darebbe c_end 0 e lo SPSA non lo
muoverebbe mai. Chi passa l'SPRT entra nel preset LTC1 **acceso**, con questi limiti (in `names.json` +
`compiled_values.json`, oppure a mano):

| opzione | init (valore dell'SPRT) | limiti SPSA | nota |
|---|---|---|---|
| `RDRRfp` | 20 | 5–60 | cp per ply di radice oltre la soglia, × profondità / 100 |
| `RDRLmp` | 5 | 1–20 | decimi di mossa per ply di radice oltre la soglia |
| `RDRProbCut` | 5 | 1–20 | cp per ply di radice oltre la soglia |
| `RDRKnee` | 17 | 12–24 | soglia di profondità di radice; si tara insieme ai tre sopra |
| `TTNearMiss` | 80 | 30–200 | margine in cp |
| `TTDamp` | 31 | 10–100 | peso dello score TT in decimi (31 = 76% score, 24% beta) |

## Dopo il run
1. Il verdetto è solo il gate (`gate_ltc1.ps1`), mai l'andamento dell'SPSA.
2. Se passa, il vettore va bakato in `threads.cpp`, aggiornando anche i default UCI, e il canary va
   aggiornato in `CLAUDE.md` e in `STATO_PROGETTO.md`.
3. Si usa **Pausa**, mai Stop.
