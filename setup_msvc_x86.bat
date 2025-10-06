@echo off
REM Setup MSVC environment for x86 builds before running pip install
REM This avoids the nested quote issues in pip's distutils

echo Setting up MSVC x86 environment...
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86

if errorlevel 1 (
    echo ERROR: Failed to set up MSVC environment
    exit /b 1
)

echo MSVC environment configured successfully
echo.
echo Now run: pip install -e .
echo.