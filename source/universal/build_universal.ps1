# =====================================================================================================================
#  build_universal.ps1 -- binario universale di Triumviratus 8.0 con PGO, da questa cartella del sorgente.
#
#  Una unita' di compilazione per variante ISA (variant.cpp, namespace Triumv_v<n>), ingresso con cpuid (entry.cpp),
#  rete incorporata (triumv.rc), runtime C++ statico. Per ogni variante: build strumentata, training deterministico
#  (pgo_train_det.py su pgo_positions.epd, `go nodes` fissi: stesso profilo a ogni build), merge, build ottimizzata.
#  Ogni variante si allena con le SUE istruzioni se questa CPU le ha (su Zen 4 tutte e cinque); altrimenti vnni512 e
#  avx512icl si allenano con quelle di avx512, e senza AVX-512 le varianti 3-5 restano senza profilo.
#
#  Serve: Visual Studio 2022 (o Build Tools) con i componenti "C++ Clang Compiler for Windows" e "MSBuild support for
#  LLVM (clang-cl) toolset", Windows SDK, Python 3, la rete nn-consilium.nnue nella cartella del sorgente (una sopra).
#
#  USO (PowerShell, non serve amministratore):
#    .\build_universal.ps1                         # tutto, data di rilascio = oggi
#    .\build_universal.ps1 -Day 20261009           # data della riga id name
#    .\build_universal.ps1 -NoPgo                  # senza PGO (prova veloce)
#    .\build_universal.ps1 -ExtraFlags "/clang:-mbranches-within-32B-boundaries"
#  Risultato: <Out>\Triumviratus_8.0_<Day>_universal.exe, SHA256SUMS.txt, verifica.txt (bench di ogni variante).
# =====================================================================================================================
param(
    [string]$Day = (Get-Date -Format yyyyMMdd),
    [string]$Out = "",
    [int]$Workers = [Environment]::ProcessorCount,
    [string]$ExtraFlags = "",
    [switch]$NoPgo,
    [switch]$Tune,
    [switch]$NoStrictAliasing,
    [string]$ExpectedBench = "430151"
)
$ErrorActionPreference = "Stop"
$U   = $PSScriptRoot                               # ...\universal
$SRC = (Resolve-Path "$U\..").Path                 # sorgente del motore
if (-not $Out) { $Out = "$SRC\..\universal_out" }
$OBJ = "$Out\obj"; $PROF = "$Out\prof"
New-Item -ItemType Directory -Force $Out, $OBJ, $PROF | Out-Null
$Out = (Resolve-Path $Out).Path; $OBJ = (Resolve-Path $OBJ).Path; $PROF = (Resolve-Path $PROF).Path
$LOG = "$Out\build.log"
function Say($m) { $l = "[$(Get-Date -Format HH:mm:ss)] $m"; Write-Host $l; Add-Content -Encoding utf8 $LOG $l }
function Fail($m) { Say "ERRORE: $m"; throw $m }
Set-Content -Encoding utf8 $LOG "build_universal.ps1 $(Get-Date)"

# --- strumenti ----------------------------------------------------------------------------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { Fail "vswhere non trovato: manca Visual Studio 2022 (vedi build\setup_toolchain.ps1)" }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Llvm.Clang -property installationPath
if (-not $vs) { Fail "Visual Studio senza il componente 'C++ Clang Compiler for Windows'" }
$L = "$vs\VC\Tools\Llvm\x64\bin"
$CXX = "$L\clang-cl.exe"; $LNK = "$L\lld-link.exe"; $RC = "$L\llvm-rc.exe"; $PD = "$L\llvm-profdata.exe"
foreach ($t in $CXX, $LNK, $RC, $PD) { if (-not (Test-Path $t)) { Fail "manca $t" } }
$rt = Get-ChildItem "$vs\VC\Tools\Llvm\x64\lib\clang\*\lib\windows\clang_rt.profile-x86_64.lib" -ErrorAction SilentlyContinue |
      Select-Object -First 1
if (-not $rt -and -not $NoPgo) { Fail "manca clang_rt.profile-x86_64.lib (serve alla PGO)" }
$msvc = Get-ChildItem "$vs\VC\Tools\MSVC" -Directory | Sort-Object Name -Descending | Select-Object -First 1
$kits = (Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots" -ErrorAction SilentlyContinue).KitsRoot10
if (-not $kits) { Fail "Windows SDK non trovato" }
$sdk = Get-ChildItem "$kits\Lib" -Directory | Where-Object { Test-Path "$($_.FullName)\um\x64\kernel32.lib" } |
       Sort-Object Name -Descending | Select-Object -First 1
$env:LIB = "$($msvc.FullName)\lib\x64;$($sdk.FullName)\um\x64;$($sdk.FullName)\ucrt\x64"
$py = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $py -and -not $NoPgo) { Fail "Python 3 non trovato nel PATH" }
if (-not (Test-Path "$SRC\nn-consilium.nnue")) { Fail "manca la rete $SRC\nn-consilium.nnue" }
Say "clang-cl: $CXX ($((& $CXX --version)[0]))"
Say "MSVC $($msvc.Name), SDK $($sdk.Name), worker $Workers, data $Day"

# --- CPU: quali varianti puo' eseguire ------------------------------------------------------------------------------
$probe = @'
#include <intrin.h>
#include <stdio.h>
#include <string.h>
__attribute__((target("xsave"))) static unsigned long long xcr0(void) { return _xgetbv(0); }
int main(void) {
  int r[4]; char vendor[13] = {0};
  __cpuid(r, 0); memcpy(vendor, &r[1], 4); memcpy(vendor + 4, &r[3], 4); memcpy(vendor + 8, &r[2], 4);
  __cpuid(r, 1); int ecx1 = r[2], fam = ((r[0] >> 8) & 15) + ((r[0] >> 20) & 255);
  __cpuidex(r, 7, 0); unsigned ebx7 = r[1], ecx7 = r[2];
  unsigned long long x = (ecx1 >> 27 & 1) ? xcr0() : 0;
  int avx = (x & 6) == 6, z = (x & 0xE6) == 0xE6;
  int avx2 = avx && (ebx7 >> 5 & 1) && (ebx7 >> 3 & 1);
  int pext = avx2 && (ebx7 >> 8 & 1);
  int f512 = z && (ebx7 >> 16 & 1) && (ebx7 >> 17 & 1) && (ebx7 >> 30 & 1) && (ebx7 >> 31 & 1);
  int vnni = f512 && (ecx7 >> 11 & 1);
  int icl = vnni && (ecx7 >> 1 & 1) && (ecx7 >> 6 & 1) && (ecx7 >> 12 & 1);
  printf("%s%s%s%s%s %s %d\n", avx2 ? "1" : "", pext ? "2" : "", f512 ? "3" : "", vnni ? "4" : "", icl ? "5" : "",
         vendor, fam);
  return 0;
}
'@
Set-Content -Encoding ascii "$OBJ\probe.c" $probe
& $CXX /nologo /O2 "$OBJ\probe.c" "/Fe$OBJ\probe.exe" "/Fo$OBJ\probe.obj" | Out-Null
$cpu = (& "$OBJ\probe.exe").Trim().Split(' ')
$can = $cpu[0]
Say "CPU $($cpu[1]) famiglia $($cpu[2]): varianti eseguibili '$can' (1 avx2-nopext, 2 avx2, 3 avx512, 4 vnni512, 5 avx512icl)"
if (-not $can.Contains('1')) { Fail "questa CPU non ha AVX2: il motore non gira" }
$names = @{ 1 = 'avx2-nopext'; 2 = 'avx2'; 3 = 'avx512'; 4 = 'vnni512'; 5 = 'avx512icl' }

# --- flag (come build_pgo_clang_80.ps1 e il prototipo dello Xeon) ---------------------------------------------------
$base = @('/c', '/nologo', '/std:c++17', '/EHsc', '/MT', '/O2', '/Oy', '/GS-', '/Gy', '/Gw', '/clang:-O3',
          '/clang:-ffp-contract=off', '-w', '-DNDEBUG', '-D_CONSOLE', '-DTRIUMV_EMBED_RESOURCE', '-DTRIUMV5_THREATS_INCR',
          '-DUSE_AVX2', '-DUSE_SSE41', '-DUSE_SSSE3', '-DUSE_SSE2', '-DUSE_SSE', '-DTRIUMV_RELEASE',
          "-DTRIUMV_RELEASE_DAY=$Day", "-I$SRC\fathom")
# Analisi degli alias per tipo: gcc la usa di default, clang-cl per Windows no. Misurata il 08/10/2026 sera (xperf,
# 6 giri): -1,06% cicli/nodo in mediogioco, -0,76% nei finali; verificata con tutte le macro VERIFY_* e con gcc.
if (-not $NoStrictAliasing) { $base += '/clang:-fstrict-aliasing' }
# Cicli allineati a 64 byte: questo binario (un'unica unita' di compilazione per variante) perdeva il 2,2% di cicli
# rispetto alle build separate in avx512 a istruzioni identiche; l'allineamento ne recupera 0,7 (xperf 08/10/2026).
$base += '/clang:-falign-loops=64'
if ($ExtraFlags) { $base += $ExtraFlags.Split(' ', [StringSplitOptions]::RemoveEmptyEntries) }
function VFlags($n) {
    switch ($n) {
        1 { @('/arch:AVX2', '-UUSE_PEXT', '-mno-bmi2', '-mno-avx512f') }
        2 { @('/arch:AVX2', '-DUSE_PEXT', '-mbmi2', '-mno-avx512f') }
        3 { @('/arch:AVX2', '-DUSE_PEXT', '-DUSE_AVX512', '-mavx512f', '-mavx512bw', '-mavx512dq', '-mavx512vl', '-mbmi2',
              '-mno-avx512vnni') }
        4 { @('/arch:AVX2', '-DUSE_PEXT', '-DUSE_AVX512', '-DUSE_VNNI', '-mavx512f', '-mavx512bw', '-mavx512dq', '-mavx512vl',
              '-mbmi2', '-mavx512vnni') }
        5 { @('/arch:AVX2', '-DUSE_PEXT', '-DUSE_AVX512', '-DUSE_VNNI', '-DUSE_AVX512ICL', '-mavx512f', '-mavx512bw',
              '-mavx512dq', '-mavx512vl', '-mbmi2', '-mavx512vnni', '-mavx512vbmi', '-mavx512vbmi2', '-mavx512bitalg') }
    }
}
# Messa a punto per i processori a cui la variante e' destinata. SPENTA di default (-Tune per accenderla): sullo Xeon
# -mtune=skylake-avx512 dava +0,5% di cicli rispetto alla messa a punto generica (xperf 08/10/2026).
function VTune($n) {
    if (-not $Tune) { return @() }
    switch ($n) { 1 { @('/clang:-mtune=znver2') } 3 { @('/clang:-mtune=skylake-avx512') } 4 { @('/clang:-mtune=cascadelake') }
                  default { @() } }
}

# Compila una variante: $as = variante di cui usare le istruzioni (training su CPU che non ha quelle di $n).
function Start-Variant($n, $objOut, $mode, $as, $profdata) {
    $a = $base + (VFlags $as) + (VTune $n) + @("-DTRIUMV_VID=$n")
    if ($mode -eq 'gen') { $a += @('-DTRIUMV_STANDALONE', '-fprofile-generate', '-DCLANG_PGO_GEN') }
    if ($mode -eq 'use') { $a += @("-fprofile-use=$profdata", '-Wno-profile-instr-out-of-date', '-Wno-profile-instr-unprofiled') }
    $a += @("$U\variant.cpp", "/Fo$objOut")
    # Start-Process unisce gli argomenti con spazi senza virgolette: quelli con spazi (percorsi) vanno quotati qui.
    $a = $a | ForEach-Object { if ($_ -match ' ') { "`"$_`"" } else { $_ } }
    $p = Start-Process -FilePath $CXX -ArgumentList $a -NoNewWindow -PassThru -RedirectStandardOutput "$objOut.out" `
                       -RedirectStandardError "$objOut.err"
    Keep-Handle $p
}
# PowerShell 5.1: con -NoNewWindow il processo restituito non tiene un handle, e se finisce prima che qualcuno lo apra il
# codice di uscita va perso (ExitCode vuoto, letto come errore da Wait-All). Successo sul Ryzen 8845HS il 09/10/2026:
# le cinque compilazioni finivano prima che Wait-All arrivasse ad aspettarle. Leggere Handle subito lo tiene aperto.
function Keep-Handle($p) { $null = $p.Handle; $p }
function Wait-All($procs, $what) {
    foreach ($p in $procs) { $p.WaitForExit() }
    foreach ($p in $procs) { if ($p.ExitCode -ne 0) { Fail "$what fallita (codice $($p.ExitCode)): vedi i file .err in $OBJ" } }
}
function Link($exe, $objs, [switch]$Gen) {
    $a = @('/nologo', "/OUT:$exe", '/SUBSYSTEM:CONSOLE', '/STACK:8388608', '/LARGEADDRESSAWARE') + $objs +
         @("$OBJ\tbprobe.obj", "$OBJ\triumv.res", 'advapi32.lib')
    if ($Gen) { $a += $rt.FullName } else { $a += @('/OPT:REF', '/OPT:ICF') }
    & $LNK @a 2>&1 | Out-File -Append -Encoding utf8 $LOG
    if ($LASTEXITCODE -ne 0) { Fail "collegamento di $exe fallito" }
}
function Bench($exe, $isa) {
    $env:TRIUMV_ISA = $isa
    $o = "bench`nquit`n" | & $exe 2>&1 | Select-String "Nodes searched" | Select-Object -First 1
    Remove-Item Env:TRIUMV_ISA -ErrorAction SilentlyContinue
    if ($o -and "$o" -match '(\d+)\s*$') { return $Matches[1] } else { return "?" }
}

# --- parti comuni: fathom (C, indipendente dall'ISA), ingresso, risorse ---------------------------------------------
Say "parti comuni"
& $CXX /nologo /c /O2 /MT /arch:AVX2 /std:c11 -w "-I$SRC\fathom" "$SRC\fathom\tbprobe.c" "/Fo$OBJ\tbprobe.obj"
if ($LASTEXITCODE -ne 0) { Fail "fathom" }
& $CXX /nologo /c /std:c++17 /O2 /MT -w "$U\entry.cpp" "/Fo$OBJ\entry.obj"
if ($LASTEXITCODE -ne 0) { Fail "entry.cpp" }
Push-Location $U; & $RC /FO "$OBJ\triumv.res" triumv.rc; $rcOk = $LASTEXITCODE; Pop-Location
if ($rcOk -ne 0) { Fail "risorse (triumv.rc)" }

# --- PGO ----------------------------------------------------------------------------------------------------------
$profs = @{}
if (-not $NoPgo) {
    $train = @{}
    foreach ($n in 1..5) {
        if ($can.Contains("$n")) { $train[$n] = $n }
        elseif ($n -ge 4 -and $can.Contains('3')) { $train[$n] = 3 }   # istruzioni di avx512, namespace della variante
    }
    Say "build strumentate: $(($train.Keys | Sort-Object | ForEach-Object { "$($names[$_])<-$($names[$train[$_]])" }) -join ', ')"
    $ps = foreach ($n in $train.Keys) { Start-Variant $n "$OBJ\g$n.obj" 'gen' $train[$n] '' }
    Wait-All $ps "build strumentata"
    foreach ($n in $train.Keys) { Link "$OBJ\g$n.exe" @("$OBJ\g$n.obj") -Gen }
    Say "training ($Workers worker per variante, in parallelo)"
    $ps = foreach ($n in $train.Keys) {
        Remove-Item -Recurse -Force "$PROF\v$n" -ErrorAction SilentlyContinue; New-Item -ItemType Directory "$PROF\v$n" | Out-Null
        $env:LLVM_PROFILE_FILE = "$PROF\v$n\p_%p.profraw"
        Keep-Handle (Start-Process -FilePath $py -ArgumentList @("`"$U\pgo_train_det.py`"", "`"$OBJ\g$n.exe`"", '0', "`"$U\pgo_positions.epd`"",
                      '200', '--workers', "$([Math]::Max(1, [int]($Workers / $train.Count)))") -NoNewWindow -PassThru `
                      -WorkingDirectory $OBJ -RedirectStandardOutput "$OBJ\train$n.log" -RedirectStandardError "$OBJ\train$n.err")
    }
    Remove-Item Env:LLVM_PROFILE_FILE -ErrorAction SilentlyContinue
    Wait-All $ps "training"
    foreach ($n in $train.Keys) {
        $raw = @(Get-ChildItem "$PROF\v$n\*.profraw")
        if (-not $raw.Count) { Fail "nessun profilo per la variante $n" }
        & $PD merge -o "$PROF\v$n.profdata" @($raw | ForEach-Object FullName)
        if ($LASTEXITCODE -ne 0) { Fail "merge del profilo $n" }
        $profs[$n] = "$PROF\v$n.profdata"
    }
    Say "profili: $(($profs.Keys | Sort-Object | ForEach-Object { $names[$_] }) -join ', ')"
}

# --- build ottimizzate e collegamento (la variante piu' bassa per prima: le copie condivise della libreria standard
#     vengono da avx2-nopext e girano su ogni CPU) ---------------------------------------------------------------------
Say "build ottimizzate"
$ps = foreach ($n in 1..5) {
    if ($profs.ContainsKey($n)) { Start-Variant $n "$OBJ\p$n.obj" 'use' $n $profs[$n] }
    else { Start-Variant $n "$OBJ\p$n.obj" 'none' $n '' }
}
Wait-All $ps "build ottimizzata"
$exe = "$Out\Triumviratus_8.0_${Day}_universal.exe"
Link $exe @("$OBJ\entry.obj", "$OBJ\p1.obj", "$OBJ\p2.obj", "$OBJ\p3.obj", "$OBJ\p4.obj", "$OBJ\p5.obj")

# --- verifica -----------------------------------------------------------------------------------------------------
$ver = @()
$ok = $true
foreach ($n in 1..5) {
    if (-not $can.Contains("$n")) { $ver += "{0,-12} non eseguibile su questa CPU" -f $names[$n]; continue }
    $b = Bench $exe $names[$n]
    $tag = if ($b -eq $ExpectedBench) { "ok" } else { $ok = $false; "BENCH DIVERSO (atteso $ExpectedBench)" }
    $pg = if ($profs.ContainsKey($n)) { "PGO" } else { "senza PGO" }
    $ver += "{0,-12} bench {1,-8} {2,-10} {3}" -f $names[$n], $b, $pg, $tag
}
$auto = "uci`nquit`n" | & $exe 2>&1 | Select-String "^id name" | Select-Object -First 1
# Variante che l'ingresso sceglie qui (stessa regola di entry.cpp: PEXT lento su AMD famiglie 15h, 17h e Hygon 18h).
$best = [int]("$($can[-1])")
if ($best -eq 2 -and (($cpu[1] -eq 'AuthenticAMD' -and ($cpu[2] -eq '21' -or $cpu[2] -eq '23')) -or
                      ($cpu[1] -eq 'HygonGenuine' -and $cpu[2] -eq '24'))) { $best = 1 }
$ver += "$auto; su questa CPU parte la variante $($names[$best]) (in console: riga 'info string Build: universal')"
Set-Content -Encoding utf8 "$Out\verifica.txt" $ver
(Get-FileHash -Algorithm SHA256 $exe).Hash.ToLower() + " *" + (Split-Path $exe -Leaf) |
    Set-Content -Encoding ascii "$Out\SHA256SUMS.txt"
$ver | ForEach-Object { Say $_ }
if (-not $ok) { Fail "una variante non da' il bench atteso" }
Say "PRONTO: $exe"
