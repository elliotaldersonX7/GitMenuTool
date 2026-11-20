@echo off
rem Git Menu Tool (C++/Win32) build script. /MT static link, Win7~Win11.
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [X] vswhere not found. Install Visual Studio / BuildTools first.
    exit /b 1
)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo [X] MSVC toolset not found.
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
rem Version alignment: generate app_version.h from git describe (tag the HEAD commit before a release build).
rem NOTE: keep this .bat ASCII-only, cmd parses it with the ANSI codepage.
set "APPTAG="
for /f "usebackq delims=" %%i in (`git -C "%~dp0." describe --tags 2^>nul`) do set "APPTAG=%%i"
if defined APPTAG (
    echo [OK] inject APP_VERSION_STR = %APPTAG%
    > "%~dp0app_version.h" echo #define APP_VERSION_STR L"%APPTAG%"
) else (
    echo [WARN] git describe failed, fallback APP_VERSION from main.cpp
    if exist "%~dp0app_version.h" del "%~dp0app_version.h"
)
cl /nologo /std:c++17 /utf-8 /EHsc /MT /W3 /O2 /DUNICODE /D_UNICODE main.cpp ^
   /Fe:GitMenuTool.exe ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:app.manifest user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib ole32.lib wininet.lib
if errorlevel 1 (
    echo [X] build failed
    exit /b 1
)
echo [OK] GitMenuTool.exe generated
