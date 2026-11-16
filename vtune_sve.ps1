# Pokrece VTune "Microarchitecture Exploration" za svih 6 verzija joina
# i snima tekstualni sazetak svake u docs\<verzija>_summary.txt.
#
# MORA se pokrenuti kao administrator (inace VTune ne moze do sampling drajvera):
#   PowerShell (Run as administrator) ->
#   powershell -ExecutionPolicy Bypass -File C:\Users\Korisnik\Desktop\PRS\radix-hash-join\vtune_sve.ps1
#
# Rezultati (za otvaranje u VTune GUI-ju i screenshotove) su u vtune_rezultati\<verzija>.
# Hipervizor mora biti iskljucen: bcdedit /set hypervisorlaunchtype off + restart.

$ErrorActionPreference = "Continue"
$repo  = $PSScriptRoot
$vtune = "${env:ProgramFiles(x86)}\Intel\oneAPI\vtune\latest\bin64\vtune.exe"
$verzije = "naivni", "radix_cvorovi", "radix_lanac", "radix_csr", "radix_lanac_pool", "radix_lanac_opt"

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { Write-Host "GRESKA: pokreni PowerShell kao administrator." -ForegroundColor Red; exit 1 }
if (-not (Test-Path "$repo\bin\naivni.exe")) { Write-Host "GRESKA: prvo pokreni build.bat." -ForegroundColor Red; exit 1 }

New-Item -ItemType Directory -Force "$repo\docs", "$repo\vtune_rezultati" | Out-Null

foreach ($v in $verzije) {
    $r = "$repo\vtune_rezultati\$v"
    if (Test-Path $r) { Remove-Item -Recurse -Force $r -Confirm:$false }
    Write-Host "`n===== $v =====" -ForegroundColor Cyan
    & $vtune -collect uarch-exploration -r $r -- "$repo\bin\$v.exe"
    if ($LASTEXITCODE -ne 0) { Write-Host "GRESKA kod $v" -ForegroundColor Red; continue }
    & $vtune -report summary -r $r -format text -report-output "$repo\docs\${v}_summary.txt" | Out-Null
    Write-Host "Sazetak: docs\${v}_summary.txt"
}
Write-Host "`nGotovo. Rezultati za VTune GUI su u vtune_rezultati\." -ForegroundColor Green
