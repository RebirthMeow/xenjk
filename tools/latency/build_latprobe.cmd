@echo off
rem Builds latprobe.exe into .codex-research\latency\bin (x64, release). Run from any shell.
setlocal
rem "(x86)" would close the for's parentheses: run vswhere from its own folder instead
pushd "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
for /f "usebackq delims=" %%i in (`.\vswhere.exe -latest -property installationPath`) do set "VS=%%i"
popd
call "%VS%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul || exit /b 1
set "OUT=%~dp0..\..\.codex-research\latency\bin"
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /O2 /EHsc /std:c++17 /W3 "%~dp0latprobe.cpp" /Fe"%OUT%\latprobe.exe" /Fo"%OUT%\\"
