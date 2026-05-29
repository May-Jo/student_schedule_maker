# build.ps1 - Compile all C files and launch the server
# Run from the project root: .\build.ps1

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$src = Join-Path $root "src"
$bin = Join-Path $root "bin"

if (-not (Test-Path $bin)) {
    New-Item -ItemType Directory -Path $bin | Out-Null
}

# Stop any running server instance to release the file lock for compilation
Stop-Process -Name "server" -Force -ErrorAction SilentlyContinue

Write-Host "Compiling src\study.c..."
& gcc "$src\study.c" -o "$bin\study.exe" -Wall
if ($LASTEXITCODE -ne 0) {
    Write-Error "src\study.c failed to compile."
    exit 1
}

Write-Host "Compiling src\stopwatch.c..."
& gcc "$src\stopwatch.c" -o "$bin\stopwatch.exe" -Wall
if ($LASTEXITCODE -ne 0) {
    Write-Error "src\stopwatch.c failed to compile."
    exit 1
}

Write-Host "Compiling src\server.c..."
& gcc "$src\server.c" -o "$bin\server.exe" -lws2_32 -Wall
if ($LASTEXITCODE -ne 0) {
    Write-Error "src\server.c failed to compile."
    exit 1
}

Write-Host ""
Write-Host "All binaries built successfully."
Write-Host "Starting server -> http://localhost:8080/"
Write-Host ""

Set-Location $root
& "$bin\server.exe"
