@echo off
call "%~dp0build.cmd"
if errorlevel 1 exit /b 1
"%~dp0build\Release\probe.exe" %*
exit /b %errorlevel%
