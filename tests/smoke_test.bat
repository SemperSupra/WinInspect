@echo off
REM WinInspect Smoke Test Suite
REM Runs a series of end-to-end checks against a running daemon.
REM Usage: smoke_test.bat [daemon_port] [daemon_path]
setlocal enabledelayedexpansion

set PORT=%~1
if "%PORT%"=="" set PORT=19999
set DAEMON=%~2
if "%DAEMON%"=="" set DAEMON=build\wininspectd.exe
set CLI=build\wininspect.exe

set PASS=0
set FAIL=0

echo === WinInspect Smoke Tests ===
echo Daemon: %DAEMON% on port %PORT%
echo.

REM 1. Start daemon
echo [TEST] Starting daemon...
start /B "" "%DAEMON%" --headless --port %PORT% --http-port 8088
timeout /t 3 /nobreak >nul

REM 2. Health check
echo [TEST] daemon.health...
for /f "delims=" %%a in ('"%CLI%" --tcp 127.0.0.1:%PORT% health 2^>nul') do set HEALTH=%%a
echo !HEALTH! | findstr "ok.*true" >nul
if !errorlevel! equ 0 ( set /a PASS+=1 & echo   PASS ) else ( set /a FAIL+=1 & echo   FAIL: !HEALTH! )

REM 3. Capabilities
echo [TEST] daemon.capabilities...
for /f "delims=" %%a in ('"%CLI%" --tcp 127.0.0.1:%PORT% capabilities 2^>nul') do set CAP=%%a
echo !CAP! | findstr "features" >nul
if !errorlevel! equ 0 ( set /a PASS+=1 & echo   PASS ) else ( set /a FAIL+=1 & echo   FAIL )

REM 4. Window list (top)
echo [TEST] window.listTop...
for /f "delims=" %%a in ('"%CLI%" --tcp 127.0.0.1:%PORT% top 2^>nul') do set TOP=%%a
echo !TOP! | findstr "ok" >nul
if !errorlevel! equ 0 ( set /a PASS+=1 & echo   PASS ) else ( set /a FAIL+=1 & echo   FAIL )

REM 5. Desktop info
echo [TEST] screen.desktopInfo...
for /f "delims=" %%a in ('"%CLI%" --tcp 127.0.0.1:%PORT% desktop-info 2^>nul') do set DI=%%a
echo !DI! | findstr "width" >nul
if !errorlevel! equ 0 ( set /a PASS+=1 & echo   PASS ) else ( set /a FAIL+=1 & echo   FAIL )

REM 6. HTTP API
echo [TEST] HTTP /api/v1/health...
for /f "delims=" %%a in ('curl -s http://127.0.0.1:8088/api/v1/health 2^>nul') do set HTTP=%%a
echo !HTTP! | findstr "ok" >nul
if !errorlevel! equ 0 ( set /a PASS+=1 & echo   PASS ) else ( set /a FAIL+=1 & echo   FAIL )

REM 7. HTTP Dashboard
echo [TEST] HTTP /dashboard...
for /f "delims=" %%a in ('curl -s http://127.0.0.1:8088/dashboard 2^>nul') do set DASH=%%a
echo !DASH! | findstr "WinInspect" >nul
if !errorlevel! equ 0 ( set /a PASS+=1 & echo   PASS ) else ( set /a FAIL+=1 & echo   FAIL )

REM 8. Process list
echo [TEST] process.list...
for /f "delims=" %%a in ('"%CLI%" --tcp 127.0.0.1:%PORT% ps 2^>nul') do set PS=%%a
echo !PS! | findstr "pid" >nul
if !errorlevel! equ 0 ( set /a PASS+=1 & echo   PASS ) else ( set /a FAIL+=1 & echo   FAIL )

REM 9. Cleanup daemon
echo [TEST] Stopping daemon...
taskkill /f /im wininspectd.exe >nul 2>&1
timeout /t 2 /nobreak >nul

echo.
echo === Results: %PASS% passed, %FAIL% failed ===
if %FAIL% gtr 0 exit /b 1
exit /b 0
