@echo off
call "%~dp0cat-gatekeeper.bat" dismiss %*
exit /b %ERRORLEVEL%
