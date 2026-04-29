@echo off
call "%~dp0cat-gatekeeper.bat" quit %*
exit /b %ERRORLEVEL%
