@echo off
REM Ensemble Build Wrapper (Windows CMD)
REM Forwards arguments directly to the unified Python build orchestrator build.py

python "%~dp0build.py" %*
if %ERRORLEVEL% NEQ 0 (
    exit /b %ERRORLEVEL%
)
