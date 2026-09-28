# SPRT delle tre idee di Coda (docs/audit_7.1/J_CODA.md), 27/09/2026. Da lanciare A MACCHINA LIBERA, una alla volta.
# Stesso binario dev (A/B con le opzioni), 40+0.4, [0, 3], convenzioni del progetto (UHO 2024, concurrency 75).
# Uso:  .\sprt_coda_ideas.ps1 -Idea 1     (1 = RootDepthRelax, 2 = TTNearMiss, 3 = TTDamp: idee di Coda;
#                                          4 = PvTTMinDepth, 5 = SingularPlyGuard: consenso dei riferimenti,
#                                          docs/audit_7.1/K_RIFERIMENTI_LTC.md)
# Il binario: build PGO senza -Release, per esempio
#   .\build_pgo_clang_71.ps1 -Arch avx512 -Name Triumviratus_7.1_devJ     (bench atteso 273477)
# Binario predefinito: devL (27/09 sera, PGO completa, bench 273477), che ha tutte e cinque le opzioni.
#       6 = bundle 4+5 (27/09: la 5 da sola giudicata neutra dall'utente, +0,65 ± 8,8 su ~2.700 partite a 10+0.1)
#       7 = bundle 2+3 (TTNearMiss + TTDamp; 28/09: la 2 da sola in positivo ma non chiusa)
#       8 = RootReplyRedPct=50 (28/09, idea nostra: meno LMR sulle risposte alla mossa di radice; binario devM)
#       9 = NullThreatExt=300 (28/09, estensione per minaccia rivelata dalla null move)
#      10 = TMDrift=30 (28/09, +30% di tempo sulla deriva lenta; TC con orologio: 10+0.1 va bene)
#       9 e 10 richiedono un binario compilato dopo il 28/09 sera (devN o successivo): passare -Exe.
param([Parameter(Mandatory = $true)][ValidateSet(1, 2, 3, 4, 5, 6, 7, 8, 9, 10)][int]$Idea,
      # TC: vuoto = predefinito per idea (decisione dell'utente, 27/09): idea 1 a 40+0.4 (agisce solo a radice
      # profonda), le altre a 10+0.1 come screening, e chi passa si conferma poi a TC lungo.
      [string]$TC = "",
      [int]$Hash = 64,
      # RDRKnee per l'idea 1: 17 come Coda. Nota: a 20+0.2 la profondita' mediana e' 14 e solo il 10% delle mosse
      # supera 17 (tools/pgn_depths.py su 1.314 partite); per questo l'idea 1 si fa a 40+0.4.
      [int]$Knee = 17,
      [string]$Exe = "C:\Users\Francesco\Desktop\Triumviratus\Triumviratus_7.1\x64\Release\Triumviratus_7.1_devL_avx512.exe")
$root = "C:\Users\Francesco\Desktop\Triumviratus"
if (-not $TC) { $TC = if ($Idea -eq 1) { "40+0.4" } else { "10+0.1" } }
if ($Idea -eq 1 -and $Hash -lt 128) { $Hash = 128 }
$opts = @{
    1 = @("option.RDRRfp=20", "option.RDRLmp=5", "option.RDRProbCut=5", "option.RDRKnee=$Knee")
    2 = @("option.TTNearMiss=80")
    3 = @("option.TTDamp=31")
    4 = @("option.PvTTMinDepth=true")
    5 = @("option.SingularPlyGuard=true")
    6 = @("option.PvTTMinDepth=true", "option.SingularPlyGuard=true")
    7 = @("option.TTNearMiss=80", "option.TTDamp=31")
    8 = @("option.RootReplyRedPct=50")
    9 = @("option.NullThreatExt=300")
    10 = @("option.TMDrift=30")
}[$Idea]
$name = @{ 1 = "rdr"; 2 = "ttnearmiss"; 3 = "ttdamp"; 4 = "pvttmindepth"; 5 = "singplyguard"; 6 = "pvtt_singguard"; 7 = "nearmiss_damp"; 8 = "rootreply50"; 9 = "nullthreat300"; 10 = "tmdrift30" }[$Idea]
# Via cmd: PowerShell 5.1 che scrive su stdin di un exe premette un BOM e il motore non riconosce "bench".
$bench = "" + (cmd /c "(echo bench& echo quit) | `"$Exe`"" | Select-String "Nodes searched")
if ($bench -notmatch "273477") { throw "bench diverso da 273477: $bench" }
$fcArgs = @("-engine", "cmd=$Exe", "name=$name") + $opts + @(
    "-engine", "cmd=$Exe", "name=base",
    "-each", "tc=$TC", "option.Hash=$Hash", "option.Threads=1",
    "-openings", "file=$root\OpeningBooks\uho_2024\UHO_2024_+085_+094\UHO_2024_8mvs_+085_+094.epd", "format=epd", "order=random",
    "-draw", "movenumber=40", "movecount=8", "score=10",
    "-resign", "movecount=3", "score=600", "twosided=true",
    "-sprt", "elo0=0", "elo1=3", "alpha=0.05", "beta=0.05", "model=normalized",
    "-rounds", "30000", "-games", "2", "-repeat", "-concurrency", "75", "-force-concurrency",
    "-ratinginterval", "20", "-pgnout", "file=$root\sprt_coda_$name.pgn", "-log", "file=$root\sprt_coda_$name.log")
Start-Process "$root\Fastechess_For_SPSA\fastchess.exe" -ArgumentList $fcArgs -WorkingDirectory $root
