# SPRT delle tre idee di Coda (docs/audit_7.1/J_CODA.md), 27/09/2026. Da lanciare A MACCHINA LIBERA, una alla volta.
# Stesso binario dev (A/B con le opzioni), 40+0.4, [0, 3], convenzioni del progetto (UHO 2024, concurrency 75).
# Uso:  .\sprt_coda_ideas.ps1 -Idea 1     (1 = RootDepthRelax, 2 = TTNearMiss, 3 = TTDamp)
# Il binario: build PGO senza -Release, per esempio
#   .\build_pgo_clang_71.ps1 -Arch avx512 -Name Triumviratus_7.1_devJ     (bench atteso 273477)
param([Parameter(Mandatory = $true)][ValidateSet(1, 2, 3)][int]$Idea,
      [string]$Exe = "C:\Users\Francesco\Desktop\Triumviratus\Triumviratus_7.1\x64\Release\Triumviratus_7.1_devJ_avx512.exe")
$root = "C:\Users\Francesco\Desktop\Triumviratus"
$opts = @{
    1 = @("option.RDRRfp=20", "option.RDRLmp=5", "option.RDRProbCut=5")   # RDRKnee resta 17
    2 = @("option.TTNearMiss=80")
    3 = @("option.TTDamp=31")
}[$Idea]
$name = @{ 1 = "rdr"; 2 = "ttnearmiss"; 3 = "ttdamp" }[$Idea]
$bench = ("bench`nquit" | & $Exe | Select-String "Nodes searched").ToString()
if ($bench -notmatch "273477") { throw "bench diverso da 273477: $bench" }
$fcArgs = @("-engine", "cmd=$Exe", "name=$name") + $opts + @(
    "-engine", "cmd=$Exe", "name=base",
    "-each", "tc=40+0.4", "option.Hash=128", "option.Threads=1",
    "-openings", "file=$root\OpeningBooks\uho_2024\UHO_2024_+085_+094\UHO_2024_8mvs_+085_+094.epd", "format=epd", "order=random",
    "-draw", "movenumber=40", "movecount=8", "score=10",
    "-resign", "movecount=3", "score=600", "twosided=true",
    "-sprt", "elo0=0", "elo1=3", "alpha=0.05", "beta=0.05", "model=normalized",
    "-rounds", "30000", "-games", "2", "-repeat", "-concurrency", "75", "-force-concurrency",
    "-ratinginterval", "20", "-pgnout", "file=$root\sprt_coda_$name.pgn", "-log", "file=$root\sprt_coda_$name.log")
Start-Process "$root\Fastechess_For_SPSA\fastchess.exe" -ArgumentList $fcArgs -WorkingDirectory $root
