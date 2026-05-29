# build.ps1 - Compile C backends and start the HTTP server
# Run from project root: .\build.ps1  (or .\scripts\build.ps1)

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ((Split-Path -Leaf $projectRoot) -eq "scripts") {
    $projectRoot = Split-Path -Parent $projectRoot
}

$srcC = Join-Path $projectRoot "backend\c"
$bin = Join-Path $projectRoot "bin"
$dataDir = Join-Path $projectRoot "data"

if (-not (Test-Path $bin)) {
    New-Item -ItemType Directory -Path $bin | Out-Null
}
if (-not (Test-Path $dataDir)) {
    New-Item -ItemType Directory -Path $dataDir | Out-Null
}
if (-not (Test-Path (Join-Path $dataDir "chat"))) {
    New-Item -ItemType Directory -Path (Join-Path $dataDir "chat") | Out-Null
}

Stop-Process -Name "server" -Force -ErrorAction SilentlyContinue

Write-Host "Compiling backend\c\study.c..."
& gcc (Join-Path $srcC "study.c") -o (Join-Path $bin "study.exe") -Wall
if ($LASTEXITCODE -ne 0) { Write-Error "study.c failed."; exit 1 }

Write-Host "Compiling backend\c\stopwatch.c..."
& gcc (Join-Path $srcC "stopwatch.c") -o (Join-Path $bin "stopwatch.exe") -Wall
if ($LASTEXITCODE -ne 0) { Write-Error "stopwatch.c failed."; exit 1 }

Write-Host "Compiling backend\c\server.c..."
& gcc (Join-Path $srcC "server.c") -o (Join-Path $bin "server.exe") -lws2_32 -Wall
if ($LASTEXITCODE -ne 0) { Write-Error "server.c failed."; exit 1 }

Write-Host ""
Write-Host "All binaries built successfully."
Write-Host "Starting server -> http://localhost:8080/"
Write-Host ""

Set-Location $projectRoot
& (Join-Path $bin "server.exe")
