@echo off
call "%~dp0cat-gatekeeper.bat" trigger %*
exit /b %ERRORLEVEL%
