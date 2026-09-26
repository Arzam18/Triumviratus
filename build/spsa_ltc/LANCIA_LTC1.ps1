# Mega-SPSA a TC lungo (LTC1) per Triumviratus 7.1 — lancio in 4 passi. Preparato il 27/09/2026.
# Durata prevista ~48 ore a macchina libera (76 partite concorrenti a 40+0.4, 22.000 iterazioni da 4 partite).
# Uso: .\Tuning_SPSA\spsa_ltc\LANCIA_LTC1.ps1   (dalla cartella Triumviratus). Si ferma al primo controllo fallito.
$ErrorActionPreference = "Stop"
$root   = "C:\Users\Francesco\Desktop\Triumviratus"
$lab    = "$root\Tuning_SPSA\spsa_lab"
$preset = "$lab\presets\LTC1_mega46_40s.json"
$exe    = "$root\Triumviratus_7.1\x64\Release\Triumviratus_7.1_spsaltc_avx512.exe"

# 1. Binario di tuning: build PGO SENZA -Release (servono tutte le ~440 opzioni, non le 12 della release).
if (-not (Test-Path $exe)) {
    & "$root\build_pgo_clang_71.ps1" -Arch avx512 -Name Triumviratus_7.1_spsaltc
}

# 2. Il binario deve essere il sorgente attuale ai default: canary 273477 e ~440 opzioni.
$bench = ("bench`nquit" | & $exe | Select-String "Nodes searched").ToString()
$nopt  = ("uci`nquit" | & $exe | Select-String "^option name").Count
Write-Host "bench: $bench   opzioni: $nopt"
if ($bench -notmatch "273477") { throw "canary diverso da 273477: binario non allineato al sorgente" }
if ($nopt -lt 400) { throw "binario di release (solo $nopt opzioni): serve la build senza -Release" }

# 3. Controlli obbligatori (SPSA_METHODOLOGY.md): bound, perturbazioni, ogni leva deve muovere il bench.
python "$lab\check_preset_bounds.py" $preset --exe $exe
python "$lab\check_perturbations.py" $preset
if ($LASTEXITCODE -ne 0) { throw "check_perturbations fallito" }
python "$lab\smoke_preset.py" $preset
Write-Host "`nSe smoke_preset ha segnalato LEVE MORTE: toglierle da names.json, rigenerare con make_ltc_preset.py e rilanciare." -ForegroundColor Yellow
Read-Host "Invio per avviare lo SPSA (Ctrl+C per fermarsi qui)"

# 4. Server SPSA Lab in una finestra visibile, poi creazione e avvio del run.
Start-Process powershell -ArgumentList "-NoExit", "-Command", "cd '$lab'; python server.py"
Start-Sleep 5
python "$lab\launch_run.py" $preset
Write-Host "Interfaccia: http://localhost:8765  (Pausa, mai Stop: lo Stop scarta le iterazioni in corso)"
