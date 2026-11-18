@echo off
echo Building KTKLegacy for Windows Phone 8.1...

if exist "C:\Program Files (x86)\MSBuild\14.0\Bin\MSBuild.exe" (
    set "MSBUILD=C:\Program Files (x86)\MSBuild\14.0\Bin\MSBuild.exe"
) else (
    set "MSBUILD=C:\Program Files (x86)\MSBuild\12.0\Bin\MSBuild.exe"
)

call "%MSBUILD%" "%~dp0KTKLegacy\KTKLegacy.csproj" /t:Build /p:Configuration=Release /p:Platform=ARM /p:VisualStudioVersion=14.0
if %ERRORLEVEL% NEQ 0 (
    echo Build failed.
    pause
    exit /b %ERRORLEVEL%
)

echo Build succeeded.
echo Output: %~dp0KTKLegacy\Bin\ARM\Release\KTKLegacy_Release_ARM.xap
pause