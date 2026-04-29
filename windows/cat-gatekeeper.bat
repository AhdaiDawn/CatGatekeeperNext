@echo off
setlocal

set "APP_DIR=%~dp0"
set "BIN_DIR=%APP_DIR%bin"
set "DAEMON=%BIN_DIR%\cat-gatekeeperd.exe"
set "CTL=%BIN_DIR%\cat-gatekeeperctl.exe"

set "COMMAND=%~1"
if "%COMMAND%"=="" set "COMMAND=start"

if /I "%COMMAND%"=="start" goto start
if /I "%COMMAND%"=="status" goto status
if /I "%COMMAND%"=="trigger" goto trigger
if /I "%COMMAND%"=="dismiss" goto dismiss
if /I "%COMMAND%"=="quit" goto quit
if /I "%COMMAND%"=="stop" goto quit
if /I "%COMMAND%"=="-h" goto usage
if /I "%COMMAND%"=="--help" goto usage
if /I "%COMMAND%"=="help" goto usage

echo Unknown command: "%COMMAND%" 1>&2
goto usage_error

:start
if not "%~2"=="" goto usage_error
if not exist "%DAEMON%" (
    echo Missing daemon: "%DAEMON%" 1>&2
    exit /b 1
)
if exist "%CTL%" (
    "%CTL%" status >nul 2>nul
    if not errorlevel 1 (
        echo cat-gatekeeperd is already running.
        exit /b 0
    )
)
start "" "%DAEMON%"
if errorlevel 1 exit /b %ERRORLEVEL%
echo cat-gatekeeperd started in the background.
exit /b 0

:status
if not "%~2"=="" goto usage_error
call :run_ctl status
exit /b %ERRORLEVEL%

:trigger
if not "%~2"=="" goto usage_error
call :run_ctl trigger
exit /b %ERRORLEVEL%

:dismiss
if not "%~2"=="" goto usage_error
call :run_ctl dismiss
exit /b %ERRORLEVEL%

:quit
if not "%~2"=="" goto usage_error
call :run_ctl quit
exit /b %ERRORLEVEL%

:run_ctl
if not exist "%CTL%" (
    echo Missing control client: "%CTL%" 1>&2
    exit /b 1
)
"%CTL%" %1
exit /b %ERRORLEVEL%

:usage
echo Usage:
echo   %~nx0 [start]
echo   %~nx0 status
echo   %~nx0 trigger
echo   %~nx0 dismiss
echo   %~nx0 quit
exit /b 0

:usage_error
call :usage
exit /b 2
