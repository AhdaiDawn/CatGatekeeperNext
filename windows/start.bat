@echo off
call "%~dp0cat-gatekeeper.bat" start %*
exit /b %ERRORLEVEL%
