# Gate del mega-SPSA LTC1: SPRT del vettore finale contro i default, stesso binario, 40+0.4. Decide solo questo.
# Uso: .\gate_ltc1.ps1 -Run <cartella del run in spsa_lab\runs>
# Il vettore e' la media delle ultime 1.100 iterazioni (5% del run), come nella simulazione.
param([Parameter(Mandatory = $true)][string]$Run)
$root = "C:\Users\Francesco\Desktop\Triumviratus"
$exe  = "$root\Triumviratus_7.1\x64\Release\Triumviratus_7.1_spsaltc_avx512.exe"
$log  = "$root\Tuning_SPSA\spsa_lab\runs\$Run\log.csv"
$vec  = python "$root\Tuning_SPSA\spsa_lab\extract_vector.py" $log 1100
$vec | Out-File -Encoding utf8 "$root\Tuning_SPSA\spsa_ltc\vettore_ltc1.txt"
$opts = @()
foreach ($l in $vec) { if ($l -match '^(\S+)\s+=\s+(-?[\d.]+)') { $opts += "option.$($Matches[1])=$([math]::Round([double]$Matches[2]))" } }
if ($opts.Count -lt 40) { throw "vettore incompleto ($($opts.Count) parametri)" }
$fc = "$root\Fastechess_For_SPSA\fastchess.exe"
$book = "$root\OpeningBooks\uho_2024\UHO_2024_+085_+094\UHO_2024_8mvs_+085_+094.epd"
$fcArgs = @("-engine", "cmd=$exe", "name=LTC1") + $opts + @(
    "-engine", "cmd=$exe", "name=default",
    "-each", "tc=40+0.4", "option.Hash=128", "option.Threads=1",
    "-openings", "file=$book", "format=epd", "order=random",
    "-draw", "movenumber=40", "movecount=8", "score=10",
    "-resign", "movecount=3", "score=600", "twosided=true",
    "-sprt", "elo0=0", "elo1=3", "alpha=0.05", "beta=0.05", "model=normalized",
    "-rounds", "30000", "-games", "2", "-repeat", "-concurrency", "75", "-force-concurrency",
    "-ratinginterval", "20", "-pgnout", "file=$root\sprt_ltc1_gate.pgn", "-log", "file=$root\sprt_ltc1_gate.log")
Start-Process $fc -ArgumentList $fcArgs -WorkingDirectory $root
