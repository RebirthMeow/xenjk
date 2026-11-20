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
    echo Could not find the Kejim 4v4 demo at:
    echo   %AIM_TEST_HOME%\base\demos\xen_kejim_4v4.dm_26
    pause
    exit /b 1
)

if not exist "%AIM_TEST_HOME%\base\aimfeedback_fc_track_v2.aft" (
    echo The preprocessed enemy-FC timeline is missing:
    echo   %AIM_TEST_HOME%\base\aimfeedback_fc_track_v2.aft
    echo Run run_aimfeedback_demo.bat rescan first.
    pause
    exit /b 1
)

echo <player> - Kejim 4v4 - fixed-frame Golan lesson
echo.
echo The replay fast-forwards to a real 493-unit enemy-FC chase, freezes the
echo frame, captures seven ordered JPGs, and quits. FOV is fixed at 100.
echo.

if exist "%AIM_TEST_HOME%\base\screenshots" (
    "%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%XENJK_ROOT%scripts\archive_golan_lesson_capture.ps1" -WorkspaceRoot "%XENJK_ROOT%."
    if errorlevel 1 (
        echo Could not archive the previous screenshot set. No new capture was started.
        pause
        exit /b 1
    )
)

"%GAME_DATA%\openjk.x86.exe" +set fs_basepath "%GAME_DATA%" +set fs_homepath "%AIM_TEST_HOME%" +set cg_fov 100 +set cg_thirdPerson 0 +set cg_aimFeedback 1 +set cg_aimFeedbackLesson 1 +set cg_aimFeedbackLessonTime 1217444 +set cg_aimFeedbackEnvelope 0 +set cg_aimFeedbackReview 0 +set cg_aimFeedbackCapture 1 +set con_notifytime 0 +set cg_centertime 0 +set timedemo 0 +set s_initsound 0 +set r_fullscreen 0 +set r_screenshotJpegQuality 100 +set nextdemo quit +demo xen_kejim_4v4

echo.
echo Captures are in:
echo   %AIM_TEST_HOME%\base\screenshots
pause

endlocal
