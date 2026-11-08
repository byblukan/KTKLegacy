@echo off
rem KTK for Symbian - builds the phone package from the command line (Qt Creator does the same
rem through its Symbian target). Qt for Symbian builds in the source directory only; the generated
rem files are listed in .gitignore.
rem
rem   build-symbian.cmd          release ARMv5 build, then the self-signed KTKLegacy_<version>.sis
rem                              (Belle; Anna also gets KTKLegacy_installer_<version>.sis)
rem   build-symbian.cmd bump     the same, after raising the patch number in KTKLegacy.pro
rem                              (Symbian may refuse to replace an app with an equal version)
rem   build-symbian.cmd clean    removes the build output
rem
rem The Qt SDK 1.2.1 path is taken from QTSDK, C:\QtSDK by default.
setlocal
if "%QTSDK%"=="" set QTSDK=C:\QtSDK
set ENVBAT=%QTSDK%\Symbian\SDKs\SymbianSR1Qt474\env.bat
if not exist "%ENVBAT%" (
    echo Not found: %ENVBAT%
    echo Install Qt SDK 1.2.1 with the "Qt 4.7.4 for Symbian Anna/Belle" target, or set QTSDK.
    exit /b 1
)
call "%ENVBAT%"
cd /d %~dp0

if "%1"=="clean" (
    call sbs -c arm.v5.urel.gcce4_4_1 clean
    goto :eof
)

if "%1"=="bump" (
    powershell -NoProfile -Command "(Get-Content KTKLegacy.pro) | ForEach-Object { if ($_ -match '^VERSION = (\d+)\.(\d+)\.(\d+)$') { 'VERSION = {0}.{1}.{2}' -f $matches[1],$matches[2],([int]$matches[3]+1) } else { $_ } } | Set-Content -Encoding ASCII KTKLegacy.pro"
    if errorlevel 1 exit /b 1
)

for /f "tokens=3" %%v in ('findstr /b /c:"VERSION = " KTKLegacy.pro') do set VER=%%v
echo === building KTKLegacy %VER%

qmake KTKLegacy.pro -spec symbian-sbsv2 CONFIG+=release
if errorlevel 1 exit /b 1
call sbs -c arm.v5.urel.gcce4_4_1
if errorlevel 1 exit /b 1

rem Self-signed with the SDK's default certificate: the capabilities are user-grantable only.
call createpackage.bat KTKLegacy_template.pkg release-armv5
if errorlevel 1 exit /b 1
if exist KTKLegacy.sis ( move /y KTKLegacy.sis KTKLegacy_%VER%.sis >nul & echo built KTKLegacy_%VER%.sis )
del /q KTKLegacy_unsigned.sis 2>nul

rem Anna: the Smart Installer wrapper fetches Qt and the Qt Quick Components if missing.
if exist KTKLegacy_installer.pkg (
    call createpackage.bat KTKLegacy_installer.pkg release-armv5
    if exist KTKLegacy_installer.sis ( move /y KTKLegacy_installer.sis KTKLegacy_installer_%VER%.sis >nul & echo built KTKLegacy_installer_%VER%.sis )
    del /q KTKLegacy_installer_unsigned.sis 2>nul
)
