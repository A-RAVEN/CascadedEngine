@echo off
setlocal enabledelayedexpansion

echo === CascadedEngine Build Script ===
echo.

:: Detect Visual Studio installation
set "VS_WHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VS_WHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VS_WHERE%" -latest -property installationPath`) do (
        set "VS_PATH=%%i"
    )
) else (
    :: Fallback: try common VS 2026 path
    set "VS_PATH=%ProgramFiles%\Microsoft Visual Studio\18\Community"
    if not exist "!VS_PATH!" (
        :: Try VS 2022
        set "VS_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
    )
)

if not exist "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat" (
    echo ERROR: Cannot find Visual Studio installation.
    echo Looked for: !VS_PATH!
    pause
    exit /b 1
)

echo Visual Studio: !VS_PATH!
echo.

:: Setup x64 build environment
call "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat" > NUL 2>&1
if !ERRORLEVEL! NEQ 0 (
    echo ERROR: Failed to setup Visual Studio x64 environment.
    pause
    exit /b 1
)

:: Ensure Ninja is available (from VS's bundled CMake)
set "NINJA_PATH=!VS_PATH!\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
if exist "!NINJA_PATH!\ninja.exe" (
    set "PATH=!NINJA_PATH!;!PATH!"
)

:: Get short path for CMake to avoid spaces in generated build files
for %%A in ("C:\Program Files\CMake\bin\cmake.exe") do set "CMAKE_SHORT=%%~sA"

echo Build Configuration: x64-relWithDebugInfo (Ninja)
echo.

:: Configure
echo ========================================
echo [1/2] Configuring CMake...
echo ========================================
cmake --preset x64-relWithDebugInfo
if !ERRORLEVEL! NEQ 0 (
    echo.
    echo ERROR: CMake configuration failed.
    pause
    exit /b 1
)

:: Patch generated build.ninja for CMake 4.x compatibility:
:: 1. Replace CMAKE_COMMAND path (has spaces) with short path to fix quoting in cmd.exe /C
:: 2. Replace relative .cmd paths with absolute paths because cmake -E env in CMake 4.x
::    cannot resolve .cmd files in the current directory (regression from 3.x)
set "BUILD_NINJA=out\build\x64-relWithDebugInfo\build.ninja"
set "CMAKE_LONG_PATH=C:\Program Files\CMake\bin\cmake.exe"

set "SHADER_CMD=%cd%\out\build\x64-relWithDebugInfo\_deps\directxtex-src\DirectXTex\Shaders\CompileShaders.cmd"
set "DDSVIEW_CMD=%cd%\out\build\x64-relWithDebugInfo\_deps\directxtex-src\DDSView\hlsl.cmd"
:: Convert backslashes to forward slashes for paths used in ninja
set "SHADER_CMD=!SHADER_CMD:\=/!"
set "DDSVIEW_CMD=!DDSVIEW_CMD:\=/!"

if exist "!BUILD_NINJA!" (
    echo Patching build.ninja for CMake 4.x compatibility...
    powershell -Command "$c = Get-Content '!BUILD_NINJA!' -Raw; $c = $c -replace [regex]::Escape('!CMAKE_LONG_PATH!'), '!CMAKE_SHORT!'; $c = $c -replace ' CompileShaders\.cmd >', ' !SHADER_CMD! >'; $c = $c -replace ' hlsl\.cmd >', ' !DDSVIEW_CMD! >'; Set-Content '!BUILD_NINJA!' -Value $c -NoNewline"
    if !ERRORLEVEL! NEQ 0 (
        echo WARNING: Failed to patch build.ninja, build may fail.
    )
)

:: Build
echo.
echo ========================================
echo [2/2] Building all targets...
echo ========================================
cmake --build out/build/x64-relWithDebugInfo
if !ERRORLEVEL! NEQ 0 (
    echo.
    echo ERROR: Build failed.
    pause
    exit /b 1
)

echo.
echo ========================================
echo BUILD SUCCESSFUL
echo ========================================
echo.

endlocal
exit /b 0
