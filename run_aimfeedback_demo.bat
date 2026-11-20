@echo off
setlocal

set "XENJK_ROOT=%~dp0"
set "GAME_DATA=%XENJK_ROOT%game_directory\Jedi Academy\GameData"
set "AIM_TEST_HOME=%XENJK_ROOT%build\aimfeedback-kejim-xen-4v4-20200829"

if not exist "%GAME_DATA%\openjk.x86.exe" (
    echo Could not find OpenJK at:
    echo   %GAME_DATA%\openjk.x86.exe
    pause
    exit /b 1
)

if not exist "%AIM_TEST_HOME%\base\cgamex86.dll" (
    echo Could not find the aim-feedback test build at:
    echo   %AIM_TEST_HOME%\base\cgamex86.dll
    pause
    exit /b 1
)

if not exist "%AIM_TEST_HOME%\base\demos\xen_kejim_4v4.dm_26" (
    echo Could not find the test demo at:
    echo   %AIM_TEST_HOME%\base\demos\xen_kejim_4v4.dm_26
    pause
    exit /b 1
)

echo <player> - Kejim 4v4 enemy-FC hit envelope
echo.
if /I not "%~1"=="rescan" if exist "%AIM_TEST_HOME%\base\aimfeedback_fc_track_v2.aft" goto playback

echo Pass 1 of 2: preprocessing the complete enemy flag-carrier timeline.
echo The game window is minimized and timedemo runs as quickly as possible.
echo.

start "" /wait /min "%GAME_DATA%\openjk.x86.exe" +set fs_basepath "%GAME_DATA%" +set fs_homepath "%AIM_TEST_HOME%" +set cg_aimFeedback 1 +set cg_aimFeedbackEnvelope 1 +set cg_aimFeedbackReview 0 +set timedemo 1 +set s_initsound 0 +set r_fullscreen 0 +set nextdemo quit +demo xen_kejim_4v4

if not exist "%AIM_TEST_HOME%\base\aimfeedback_fc_track_v2.aft" (
    echo.
    echo Preprocessing did not produce an enemy-FC timeline.
    pause
    exit /b 1
)

:playback
echo Pass 2 of 2: launching the continuous enemy-FC hit envelope.
echo The replay fast-forwards when nobody carries your flag and returns to
echo normal speed one second before the next enemy pickup.
echo Run this BAT with the argument rescan to rebuild the full-demo timeline.
echo.
echo   green field = validated direct-hit aim directions
echo   bright dot  = most robust validated direction
echo   Golan field opacity = modeled two-ball hit probability
echo.

"%GAME_DATA%\openjk.x86.exe" +set fs_basepath "%GAME_DATA%" +set fs_homepath "%AIM_TEST_HOME%" +set cg_fov 100 +set cg_aimFeedback 1 +set cg_aimFeedbackEnvelope 2 +set cg_aimFeedbackReview 0 +set timedemo 0 +set s_initsound 1 +set r_fullscreen 0 +set nextdemo quit +demo xen_kejim_4v4

endlocal
