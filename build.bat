@echo off
rem Prevodi sest verzija hash joina u sest .exe fajlova u bin\ (za Intel VTune).
rem Pokretanje: dvoklik ili "build.bat" iz bilo kojeg Command Prompta.
rem Ako cl.exe nije u PATH-u, sam ucita Visual Studio okruzenje (vcvars64).
setlocal
cd /d "%~dp0"

rem Putanje se postavljaju izvan ( ) blokova jer "(x86)" zbunjuje cmd.
set "PF86=%ProgramFiles(x86)%"
set "VSWHERE=%PF86%\Microsoft Visual Studio\Installer\vswhere.exe"
set "ITT=%PF86%\Intel\oneAPI\vtune\latest\sdk"

where cl >nul 2>nul
if not errorlevel 1 goto ima_cl
set "VSDIR="
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR goto nema_vs
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
:ima_cl

rem VTune ITT API: ako postoji, profilira se samo join (__itt_pause / __itt_resume).
set "ITT_FLAGS="
set "ITT_LIB="
if not exist "%ITT%\include\ittnotify.h" goto nema_itt
set ITT_FLAGS=/DKORISTI_ITT /I"%ITT%\include"
set ITT_LIB="%ITT%\lib64\libittnotify.lib"
echo VTune ITT API nadjen: profilira se samo join.
goto dalje
:nema_itt
echo VTune ITT API nije nadjen: profilira se cijeli program.
:dalje

if not exist bin\obj mkdir bin\obj

rem /O2 = optimizacije (Release), /Zi + /DEBUG = simboli da VTune pokaze imena funkcija
rem /Qvec-report:2 = izvjestaj koje petlje su vektorizovane i zasto nisu (u bin\obj\<verzija>.log)
set FLAGS=/nologo /O2 /Zi /EHsc /std:c++17 /W3 /Qvec-report:2

call :prevedi 1 naivni           || exit /b 1
call :prevedi 2 radix_cvorovi    || exit /b 1
call :prevedi 3 radix_lanac      || exit /b 1
call :prevedi 4 radix_csr        || exit /b 1
call :prevedi 5 radix_lanac_pool || exit /b 1
call :prevedi 6 radix_lanac_opt  || exit /b 1

echo.
echo Gotovo. Programi su u bin\:
dir /b bin\*.exe
exit /b 0

:nema_vs
echo GRESKA: nije nadjen Visual Studio sa "Desktop development with C++".
exit /b 1

:prevedi
echo Prevodim %2.exe ...
cl %FLAGS% %ITT_FLAGS% /DVARIJANTA=%1 vtune_main.cpp /Fo:bin\obj\%2.obj /Fd:bin\obj\%2.pdb /Fe:bin\%2.exe /link /DEBUG %ITT_LIB% > bin\obj\%2.log
if errorlevel 1 goto greska_prevodjenja
exit /b 0
:greska_prevodjenja
type bin\obj\%2.log
echo GRESKA pri prevodjenju %2.exe
exit /b 1
