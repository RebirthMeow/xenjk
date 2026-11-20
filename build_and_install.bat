@echo off
REM ============================================================================
REM xenjk: build, then show (or do) the copy into a Jedi Academy GameData folder
REM
REM   build_and_install.bat                  build the 64-bit client, list what it would copy
REM   build_and_install.bat install          the same, then copy (replaced files are backed up)
REM   build_and_install.bat win32            the 32-bit client instead
REM   build_and_install.bat debug            Debug instead of Release
REM   build_and_install.bat theora           with Ogg Theora cinematics (experimental)
REM   build_and_install.bat clean            wipe this preset's build folder first
REM
REM Words combine in any order: build_and_install.bat win32 clean install
REM
REM It uses CMakePresets.json (x64 / win32), so the same build works from the
REM command line or from Visual Studio's "Open Folder". Needs Visual Studio 2022
REM with the C++ workload, and CMake 3.21 or newer on PATH.
REM
REM Folders (set these first to change them):
REM   JK_GAMEDATA   your Jedi Academy GameData folder
REM                 (default: game_directory\Jedi Academy\GameData beside this script)
REM   XENJK_BUILD   where to build (default: build\<preset> in this checkout)
REM
REM Nothing is copied without "install". With it, every file it replaces is
REM moved first to <GameData>\..\xenjk_backup\<date-time>\, so you can put the
REM old one back by hand.
REM ============================================================================
setlocal EnableExtensions EnableDelayedExpansion

set "PRESET=x64"
set "CONFIG=Release"
set "DO_INSTALL="
set "DO_CLEAN="
set "THEORA=OFF"
for %%A in (%*) do (
    if /I "%%~A"=="x64"     set "PRESET=x64"
    if /I "%%~A"=="win32"   set "PRESET=win32"
    if /I "%%~A"=="x86"     set "PRESET=win32"
    if /I "%%~A"=="debug"   set "CONFIG=Debug"
    if /I "%%~A"=="release" set "CONFIG=Release"
    if /I "%%~A"=="install" set "DO_INSTALL=1"
    if /I "%%~A"=="clean"   set "DO_CLEAN=1"
    if /I "%%~A"=="theora"  set "THEORA=ON"
)
if "%PRESET%"=="x64" (set "SUFFIX=x86_64") else (set "SUFFIX=x86")

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
if defined XENJK_BUILD (set "BUILD_DIR=%XENJK_BUILD%\%PRESET%") else (set "BUILD_DIR=%SCRIPT_DIR%\build\%PRESET%")
set "OUT=%BUILD_DIR%\%CONFIG%"
if not defined JK_GAMEDATA set "JK_GAMEDATA=%SCRIPT_DIR%\game_directory\Jedi Academy\GameData"

echo.
echo [xenjk] preset %PRESET%, %CONFIG%, Theora %THEORA%
echo [xenjk] source   %SCRIPT_DIR%
echo [xenjk] build    %BUILD_DIR%
echo [xenjk] GameData %JK_GAMEDATA%
echo.

where cmake >nul 2>&1
if errorlevel 1 (
    echo [xenjk] cmake was not found on PATH. Install CMake 3.21 or newer from https://cmake.org/
    exit /b 1
)

if defined DO_CLEAN if exist "%BUILD_DIR%" (
    echo [xenjk] removing the build folder %BUILD_DIR%
    rmdir /S /Q "%BUILD_DIR%"
)

echo [xenjk] configuring...
cmake --preset %PRESET% -B "%BUILD_DIR%" -DBuildTheora=%THEORA%
if errorlevel 1 (
    echo [xenjk] configure failed; the messages above say why.
    exit /b 1
)
echo [xenjk] building...
cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel
if errorlevel 1 (
    echo [xenjk] build failed; the first error above is the one to fix.
    exit /b 1
)

REM ------- What goes where ------------------------------------------------------
REM 64-bit: the exe, renderer and SDL2 in GameData\bin64, the modules in base.
REM 32-bit: the exe, renderer and SDL2 in GameData, the modules in base.
if "%PRESET%"=="x64" (set "BIN=bin64\") else (set "BIN=")
if "%PRESET%"=="x64" (set "SDL=%SCRIPT_DIR%\lib\SDL2\bin\x64\SDL2.dll") else (set "SDL=%SCRIPT_DIR%\lib\SDL2\bin\x86\SDL2.dll")

if not exist "%JK_GAMEDATA%\base" (
    echo.
    echo [xenjk] Built into %OUT%.
    echo [xenjk] No Jedi Academy GameData at "%JK_GAMEDATA%" (it needs a base folder^).
    echo [xenjk] Set JK_GAMEDATA to yours to see where the files go.
    exit /b 0
)

if defined DO_INSTALL (
    for /f %%T in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set "STAMP=%%T"
    for %%P in ("%JK_GAMEDATA%\..") do set "BACKUP=%%~fP\xenjk_backup\!STAMP!"
    echo.
    echo [xenjk] copying into GameData:
) else (
    echo.
    echo [xenjk] would copy into GameData:
)
set "FAILED="
call :Item "%OUT%\xenjk.%SUFFIX%.exe"        "%BIN%xenjk.%SUFFIX%.exe"        always
call :Item "%OUT%\rd-vulkan_%SUFFIX%.dll"    "%BIN%rd-vulkan_%SUFFIX%.dll"    always
call :Item "%OUT%\cgame%SUFFIX%.dll"         "base\cgame%SUFFIX%.dll"         always
call :Item "%OUT%\jampgame%SUFFIX%.dll"      "base\jampgame%SUFFIX%.dll"      always
call :Item "%OUT%\ui%SUFFIX%.dll"            "base\ui%SUFFIX%.dll"            always
call :Item "%SDL%"                           "%BIN%SDL2.dll"                  ifmissing
if defined FAILED exit /b 1

echo.
if defined DO_INSTALL (
    if exist "!BACKUP!" echo [xenjk] replaced files are in !BACKUP!
    echo [xenjk] done.
) else (
    echo [xenjk] nothing copied. Run again with "install" to copy these.
)
echo [xenjk] to play, from GameData:
if "%PRESET%"=="x64" (
    echo     cd bin64
    echo     xenjk.x86_64.exe +set fs_basepath "%JK_GAMEDATA%" +set cl_renderer rd-vulkan
) else (
    echo     xenjk.x86.exe +set cl_renderer rd-vulkan
)
echo [xenjk] the dedicated server, openjkded.%SUFFIX%.exe, is in %OUT% if you want it.
exit /b 0

REM ===========================================================================
REM  :Item <source> <path under GameData> <always|ifmissing>
REM ===========================================================================
:Item
set "SRC=%~1"
set "REL=%~2"
set "DST=%JK_GAMEDATA%\%~2"
if not exist "%SRC%" (
    echo     MISSING  %REL%   ^(the build did not make %SRC%^)
    set "FAILED=1"
    exit /b 0
)
if exist "%DST%" (
    if /I "%~3"=="ifmissing" (
        echo     keep     %REL%   ^(already there^)
        exit /b 0
    )
    fc /b "%SRC%" "%DST%" >nul 2>&1 && (
        echo     same     %REL%
        exit /b 0
    )
    set "VERB=replace "
) else (
    set "VERB=new     "
)
echo     %VERB% %REL%
if not defined DO_INSTALL exit /b 0
for %%D in ("%DST%") do if not exist "%%~dpD" mkdir "%%~dpD"
if exist "%DST%" (
    for %%B in ("%BACKUP%\%REL%") do if not exist "%%~dpB" mkdir "%%~dpB"
    move /Y "%DST%" "%BACKUP%\%REL%" >nul
    if errorlevel 1 (
        echo     ! could not back up %REL%; left it in place
        set "FAILED=1"
        exit /b 0
    )
)
copy /Y "%SRC%" "%DST%" >nul
if errorlevel 1 (
    echo     ! copy failed for %REL%
    set "FAILED=1"
)
exit /b 0
