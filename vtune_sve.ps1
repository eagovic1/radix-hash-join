# Pokrece VTune "Microarchitecture Exploration" za svih 6 verzija joina
# i snima tekstualni sazetak svake u docs\<kompajler>\<verzija>_summary.txt.
#
# MORA se pokrenuti kao administrator (inace VTune ne moze do sampling drajvera):
#   PowerShell (Run as administrator) ->
#   cd C:\dev\radix-hash-join
#   powershell -ExecutionPolicy Bypass -File .\vtune_sve.ps1               (GCC, bin\gcc\)
#   powershell -ExecutionPolicy Bypass -File .\vtune_sve.ps1 -Kompajler msvc   (MSVC, bin\)
#
# Exe-ovi se prave prije: build_gcc.sh (GCC) ili build.bat (MSVC).
# Rezultati (za otvaranje u VTune GUI-ju: File -> Open -> Result) su u
# vtune_rezultati\<kompajler>\<verzija>.
# Hipervizor mora biti iskljucen: bcdedit /set hypervisorlaunchtype off + restart.

param(
    [ValidateSet("gcc", "msvc")]
    [string]$Kompajler = "gcc"
)

$ErrorActionPreference = "Continue"
$repo  = $PSScriptRoot
$vtune = "${env:ProgramFiles(x86)}\Intel\oneAPI\vtune\latest\bin64\vtune.exe"
$verzije = "v1_naivni", "v2_radix_cvorovi", "v3_radix_lanac", "v4_radix_csr", "v5_radix_lanac_pool", "v6_radix_lanac_opt"

if ($Kompajler -eq "gcc") { $bin = "$repo\bin\gcc"; $build = "build_gcc.sh (MSYS2 UCRT64)" }
else                      { $bin = "$repo\bin";     $build = "build.bat" }
$rezultati = "$repo\vtune_rezultati\$Kompajler"
$docs      = "$repo\docs\$Kompajler"

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { Write-Host "GRESKA: pokreni PowerShell kao administrator." -ForegroundColor Red; exit 1 }
if (-not (Test-Path "$bin\v1_naivni.exe")) { Write-Host "GRESKA: nema exe-ova u $bin, prvo pokreni $build." -ForegroundColor Red; exit 1 }

Write-Host "Kompajler: $Kompajler, exe-ovi iz $bin"
New-Item -ItemType Directory -Force $docs, $rezultati | Out-Null

foreach ($v in $verzije) {
    $r = "$rezultati\$v"
    if (Test-Path $r) { Remove-Item -Recurse -Force $r -Confirm:$false }
    Write-Host "`n===== $v ($Kompajler) =====" -ForegroundColor Cyan
    & $vtune -collect uarch-exploration -r $r -- "$bin\$v.exe"
    if ($LASTEXITCODE -ne 0) { Write-Host "GRESKA kod $v" -ForegroundColor Red; continue }
    & $vtune -report summary -r $r -format text -report-output "$docs\${v}_summary.txt" | Out-Null
    Write-Host "Sazetak: docs\$Kompajler\${v}_summary.txt"
}
Write-Host "`nGotovo. Rezultati za VTune GUI su u vtune_rezultati\$Kompajler\." -ForegroundColor Green
