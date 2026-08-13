@echo off
set BUILD_TYPE=Debug

if "%1"=="release" set BUILD_TYPE=Release
if "%1"=="--release" set BUILD_TYPE=Release

echo ========================================
echo  Building Ensemble in %BUILD_TYPE% mode (Windows)
echo ========================================

cmake -B build -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
if %ERRORLEVEL% NEQ 0 goto error

cmake --build build --config %BUILD_TYPE% --parallel
if %ERRORLEVEL% NEQ 0 goto error

echo Launching Ensemble...
if exist "build\%BUILD_TYPE%\ensemble.exe" (
    start build\%BUILD_TYPE%\ensemble.exe
) else (
    start build\ensemble.exe
)
goto end

:error
echo Build failed. Please ensure CMake and Qt 6 are installed.
exit /b 1

:end
