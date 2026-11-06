@echo off
setlocal
set "HOOK_CMAKE=cmake"
where cmake >nul 2>&1
if not errorlevel 1 goto configure
set "HOOK_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%HOOK_VSWHERE%" goto missing
for /f "usebackq tokens=*" %%i in (`"%HOOK_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "HOOK_VS=%%i"
set "HOOK_CMAKE=%HOOK_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%HOOK_CMAKE%" goto missing
:configure
"%HOOK_CMAKE%" -S "%~dp0." -B "%~dp0build" -G "Visual Studio 17 2022" -A Win32
if errorlevel 1 exit /b 1
"%HOOK_CMAKE%" --build "%~dp0build" --config Release --parallel
if errorlevel 1 exit /b 1
"%HOOK_CMAKE%" -E env CTEST_OUTPUT_ON_FAILURE=1 "%HOOK_CMAKE%" --build "%~dp0build" --config Release --target RUN_TESTS
exit /b %errorlevel%
:missing
echo Install Visual Studio 2022 Desktop development with C++ and CMake tools.
exit /b 1
