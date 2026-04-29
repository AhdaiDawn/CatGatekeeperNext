@echo off
call "%~dp0cat-gatekeeper.bat" status %*
exit /b %ERRORLEVEL%
