@echo off
rem SPDX-License-Identifier: MIT
setlocal
echo OpenDesertVR M0 - standalone stereo cubes
echo Connect and wake your headset in PCVR mode before continuing.
echo Reports remain in the reports folder next to this launcher.
echo.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-headset-test.ps1"
set "odv_result=%ERRORLEVEL%"
echo.
echo Test finished with exit code %odv_result%.
echo Exit 0 means technical checks passed; visual checks remain manual.
echo Exit 3 means a headset or runtime prerequisite is missing.
pause
exit /b %odv_result%
