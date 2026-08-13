param(
    [string]$Mode = "Debug"
)

$ErrorActionPreference = "Stop"

if ($Mode -eq "release" -or $Mode -eq "--release") {
    $BuildType = "Release"
} else {
    $BuildType = "Debug"
}

Write-Host "========================================" -ForegroundColor Cyan
Write-Host " Building Ensemble in $BuildType mode (Windows)" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

cmake -B build -DCMAKE_BUILD_TYPE=$BuildType
cmake --build build --config $BuildType --parallel

Write-Host "Launching Ensemble..." -ForegroundColor Green
if (Test-Path "build\$BuildType\ensemble.exe") {
    Start-Process "build\$BuildType\ensemble.exe"
} else {
    Start-Process "build\ensemble.exe"
}
