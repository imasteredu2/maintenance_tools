@echo off
REM Build GUI version with Win32 interface
REM Requires Visual Studio Build Tools or full Visual Studio installed
REM Usage: build_gui.bat [Y|N]  - Y to auto-increment version, N to skip prompt

REM Create built folder if it doesn't exist
if not exist "built" mkdir built

REM Read current version from version.txt or start at 0.0.1
if exist "version.txt" (
    set /p VERSION=<version.txt
) else (
    set VERSION=0.0.1
)

set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist %VSWHERE% (
    for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set VS_PATH=%%i
    )
)

if defined VS_PATH (
    call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x64
    cl /std:c++17 /O2 /EHsc /D_WINDOWS maintenance_gui.cpp /Fe:maintenance_tool_gui.exe user32.lib gdi32.lib shell32.lib comdlg32.lib comctl32.lib ole32.lib /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:maintenance_tool_gui.exe.manifest
    cl /std:c++17 /O2 /EHsc updater.cpp /Fe:updater.exe user32.lib shell32.lib
    if %ERRORLEVEL% EQU 0 (
        REM Copy to built folder with version
        copy /Y maintenance_tool_gui.exe "built\maintenance_tool_gui_v%VERSION%.exe" >nul
        copy /Y maintenance_tool_gui.exe maintenance_tool_gui.exe >nul
        copy /Y updater.exe "built\updater_v%VERSION%.exe" >nul
        copy /Y updater.exe updater.exe >nul
        echo Build successful: maintenance_tool_gui.exe
        echo Versioned copy: built\maintenance_tool_gui_v%VERSION%.exe
        
        REM Check if increment parameter was passed
        if "%1"=="" (
            REM Ask if user wants to increment version
            set /p INCREMENT="Increment version from %VERSION%? (Y/N): "
        ) else (
            set INCREMENT=%1
        )
        
        if /i "%INCREMENT%"=="Y" (
            REM Increment version (last digit)
            for /f "tokens=1,2,3 delims=." %%a in ("%VERSION%") do (
                set MAJOR=%%a
                set MINOR=%%b
                set /a PATCH=%%c+1
            )
            echo %MAJOR%.%MINOR%.%PATCH%>version.txt
            echo Version updated: %MAJOR%.%MINOR%.%PATCH%
        ) else (
            echo Version kept at: %VERSION%
        )
    ) else (
        echo Build failed
    )
) else (
    echo Visual Studio Build Tools not found
    echo Please install from: https://visualstudio.microsoft.com/downloads/
    exit /b 1
)
