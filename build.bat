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
cl /nologo /std:c++17 /utf-8 /EHsc /MT /W3 /O2 /DUNICODE /D_UNICODE main.cpp ^
   /Fe:GitMenuTool.exe ^
   /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib ole32.lib
if errorlevel 1 (
    echo [X] build failed
    exit /b 1
)
echo [OK] GitMenuTool.exe generated
