# Ensemble Build Wrapper (PowerShell)
# Forwards arguments directly to the unified Python build orchestrator build.py

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

python "$ScriptDir\build.py" @args
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
