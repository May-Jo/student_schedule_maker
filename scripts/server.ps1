$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ((Split-Path -Leaf $projectRoot) -eq "scripts") {
    $projectRoot = Split-Path -Parent $projectRoot
}
$publicRoot = Join-Path $projectRoot "frontend"
$studyExe = Join-Path $projectRoot "bin\study.exe"
$stopwatchExe = Join-Path $projectRoot "bin\stopwatch.exe"
$inputPath = Join-Path $projectRoot "data\input.txt"
$outputPath = Join-Path $projectRoot "data\output.json"
$statePath = Join-Path $projectRoot "data\state.json"
$chatDir = Join-Path $projectRoot "data\chat"
$assistantScript = Join-Path $projectRoot "backend\python\assistant_api.py"
$prefix = "http://localhost:8080/"

Set-Location $projectRoot
$utf8NoBom = New-Object System.Text.UTF8Encoding $false

function Load-DotEnv {
    param(
        [string] $EnvPath
    )

    if (-not (Test-Path $EnvPath)) {
        return
    }

    Get-Content $EnvPath -ErrorAction SilentlyContinue | ForEach-Object {
        $line = $_.Trim()
        if (-not $line -or $line.StartsWith('#')) {
            return
        }

        $splitIndex = $line.IndexOf('=')
        if ($splitIndex -lt 0) {
            return
        }

        $key = $line.Substring(0, $splitIndex).Trim()
        $value = $line.Substring($splitIndex + 1).Trim()

        if ($key -and $value -ne $null) {
            $env:$key = $value
        }
    }
}

Load-DotEnv -EnvPath (Join-Path $projectRoot '.env')

function Write-JsonResponse {
    param(
        [Parameter(Mandatory = $true)] $Context,
        [Parameter(Mandatory = $true)] $Payload,
        [int] $StatusCode = 200
    )

    $json = $Payload | ConvertTo-Json -Depth 20
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($json)
    $Context.Response.StatusCode = $StatusCode
    $Context.Response.ContentType = "application/json; charset=utf-8"
    $Context.Response.ContentLength64 = $bytes.Length
    $Context.Response.OutputStream.Write($bytes, 0, $bytes.Length)
    $Context.Response.Close()
}

function Write-TextResponse {
    param(
        [Parameter(Mandatory = $true)] $Context,
        [Parameter(Mandatory = $true)] [string] $Body,
        [string] $ContentType = "text/plain; charset=utf-8",
        [int] $StatusCode = 200
    )

    $bytes = [System.Text.Encoding]::UTF8.GetBytes($Body)
    $Context.Response.StatusCode = $StatusCode
    $Context.Response.ContentType = $ContentType
    $Context.Response.ContentLength64 = $bytes.Length
    $Context.Response.OutputStream.Write($bytes, 0, $bytes.Length)
    $Context.Response.Close()
}

function Read-RequestBody {
    param([Parameter(Mandatory = $true)] $Context)

    $reader = New-Object System.IO.StreamReader($Context.Request.InputStream, $Context.Request.ContentEncoding)
    try {
        return $reader.ReadToEnd()
    } finally {
        $reader.Dispose()
    }
}

function Parse-JsonSafe {
    param([Parameter(Mandatory = $true)] [string] $Text)

    try {
        return $Text | ConvertFrom-Json -AsHashtable
    } catch {
        throw "Invalid JSON produced by backend."
    }
}

function Get-ContentType {
    param([Parameter(Mandatory = $true)] [string] $Path)

    switch ([System.IO.Path]::GetExtension($Path).ToLowerInvariant()) {
        ".html" { return "text/html; charset=utf-8" }
        ".css" { return "text/css; charset=utf-8" }
        ".js" { return "application/javascript; charset=utf-8" }
        ".json" { return "application/json; charset=utf-8" }
        default { return "application/octet-stream" }
    }
}

function Resolve-PublicPath {
    param([Parameter(Mandatory = $true)] [string] $RequestPath)

    if ($RequestPath -eq "/") {
        $RequestPath = "/index.html"
    }

    if ($RequestPath -ne "/css/shared.css" -and -not $RequestPath.EndsWith(".html")) {
        return $null
    }

    $relative = $RequestPath.TrimStart("/") -replace "/", "\"
    $fullPath = Join-Path $publicRoot $relative
    $resolvedPublic = [System.IO.Path]::GetFullPath($publicRoot)
    $resolvedFile = [System.IO.Path]::GetFullPath($fullPath)

    if (-not $resolvedFile.StartsWith($resolvedPublic, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $null
    }

    return $resolvedFile
}

function Write-PublicFile {
    param(
        [Parameter(Mandatory = $true)] $Context,
        [Parameter(Mandatory = $true)] [string] $RequestPath
    )

    $filePath = Resolve-PublicPath -RequestPath $RequestPath
    if ($null -eq $filePath -or -not (Test-Path $filePath)) {
        return $false
    }

    $body = [System.IO.File]::ReadAllText($filePath, [System.Text.Encoding]::UTF8)
    Write-TextResponse -Context $Context -Body $body -ContentType (Get-ContentType -Path $filePath)
    return $true
}

function Invoke-StudyExe {
    param([switch] $Replan)

    if (-not (Test-Path $studyExe)) {
        throw "Missing executable: $studyExe"
    }

    if ($Replan) {
        $output = & $studyExe "--replan" 2>&1
    } else {
        $output = & $studyExe 2>&1
    }
    $exitCode = $LASTEXITCODE

    if ($exitCode -ne 0) {
        throw "study.exe failed with exit code $exitCode. $output"
    }

    if (-not (Test-Path $outputPath)) {
        throw "study.exe did not create output.json"
    }

    return [System.IO.File]::ReadAllText($outputPath, [System.Text.Encoding]::UTF8)
}

if (-not (Test-Path (Join-Path $publicRoot "index.html"))) {
    throw "Missing public\index.html"
}

$listener = $null
$boundPrefix = $null
foreach ($port in 8080, 8081, 8082) {
    try {
        $tryPrefix = "http://localhost:$port/"
        $listener = [System.Net.HttpListener]::new()
        $listener.Prefixes.Add($tryPrefix)
        $listener.Start()
        $boundPrefix = $tryPrefix
        break
    } catch {
        if ($listener) {
            $listener.Close()
            $listener = $null
        }
    }
}

if (-not $boundPrefix) {
    throw "Unable to bind Study Planner backend on ports 8080, 8081, or 8082. Free one of these ports and retry."
}

$prefix = $boundPrefix
Write-Host "Study Planner backend running at $prefix"
Write-Host "Press Ctrl+C to stop."

try {
    while ($listener.IsListening) {
        $context = $listener.GetContext()
        $request = $context.Request
        $path = $request.Url.AbsolutePath

        try {
            switch ("$($request.HttpMethod) $path") {
                "GET /api/output" {
                    if (-not (Test-Path $outputPath)) {
                        Write-JsonResponse -Context $context -Payload @{ error = "output.json not found" } -StatusCode 404
                        continue
                    }

                    $jsonText = [System.IO.File]::ReadAllText($outputPath, [System.Text.Encoding]::UTF8)
                    $parsed = Parse-JsonSafe -Text $jsonText
                    Write-JsonResponse -Context $context -Payload @{
                        schedule = $parsed
                        raw = $jsonText
                    }
                    continue
                }

                "GET /api/input" {
                    $raw = ""
                    if (Test-Path $inputPath) {
                        $raw = [System.IO.File]::ReadAllText($inputPath, [System.Text.Encoding]::UTF8)
                    }
                    Write-JsonResponse -Context $context -Payload @{ raw = $raw }
                    continue
                }

                "POST /api/schedule" {
                    $rawBody = Read-RequestBody -Context $context
                    $payload = $rawBody | ConvertFrom-Json

                    if ([string]::IsNullOrWhiteSpace($payload.inputText)) {
                        Write-JsonResponse -Context $context -Payload @{ error = "inputText is required" } -StatusCode 400
                        continue
                    }

                    [System.IO.File]::WriteAllText($inputPath, $payload.inputText, $utf8NoBom)
                    $jsonText = Invoke-StudyExe
                    $parsed = Parse-JsonSafe -Text $jsonText
                    Write-JsonResponse -Context $context -Payload @{
                        schedule = $parsed
                        raw = $jsonText
                    }
                    continue
                }

                "POST /api/replan" {
                    $rawBody = Read-RequestBody -Context $context
                    if ([string]::IsNullOrWhiteSpace($rawBody)) {
                        Write-JsonResponse -Context $context -Payload @{ error = "Request body is required" } -StatusCode 400
                        continue
                    }

                    $null = $rawBody | ConvertFrom-Json
                    [System.IO.File]::WriteAllText($statePath, $rawBody, $utf8NoBom)
                    $jsonText = Invoke-StudyExe -Replan
                    $parsed = Parse-JsonSafe -Text $jsonText
                    Write-JsonResponse -Context $context -Payload @{
                        schedule = $parsed
                        raw = $jsonText
                    }
                    continue
                }

                "POST /api/stopwatch/launch" {
                    if (-not (Test-Path $stopwatchExe)) {
                        Write-JsonResponse -Context $context -Payload @{ error = "stopwatch.exe not found" } -StatusCode 404
                        continue
                    }

                    Start-Process -FilePath $stopwatchExe -WorkingDirectory $projectRoot | Out-Null
                    Write-JsonResponse -Context $context -Payload @{ ok = $true }
                    continue
                }

                "POST /api/chat" {
                    $rawBody = Read-RequestBody -Context $context
                    $payload = $rawBody | ConvertFrom-Json

                    $message = $payload.message
                    if ([string]::IsNullOrWhiteSpace($message)) {
                        Write-JsonResponse -Context $context -Payload @{ error = "message field is required" } -StatusCode 400
                        continue
                    }

                    $schedule = $payload.schedule
                    $chatHistory = $payload.chatHistory
                    if ($null -eq $chatHistory) {
                        $chatHistory = @()
                    }

                    if (-not (Test-Path $chatDir)) {
                        New-Item -ItemType Directory -Path $chatDir | Out-Null
                    }
                    $chatInputPath = Join-Path $chatDir "chat_input.json"
                    $chatOutputPath = Join-Path $chatDir "chat_output.json"

                    $inputPayload = @{
                        message = $message
                        schedule = $schedule
                        chatHistory = $chatHistory
                    }
                    [System.IO.File]::WriteAllText($chatInputPath, ($inputPayload | ConvertTo-Json -Depth 20), $utf8NoBom)

                    try {
                        $pythonExe = (Get-Command python -ErrorAction SilentlyContinue).Source
                        if (-not $pythonExe) {
                            $pythonExe = "python"
                        }
                        
                        $process = Start-Process -FilePath $pythonExe -ArgumentList $assistantScript -WorkingDirectory $projectRoot -NoNewWindow -PassThru -Wait -RedirectStandardOutput $null -RedirectStandardError $null
                        $exitCode = $process.ExitCode

                        if ($exitCode -ne 0) {
                            $errorPayload = @{ error = "Assistant failed (exit $exitCode)" }
                            if (Test-Path $chatOutputPath) {
                                $errText = [System.IO.File]::ReadAllText($chatOutputPath, [System.Text.Encoding]::UTF8)
                                $errJson = Parse-JsonSafe -Text $errText
                                if ($errJson.error) {
                                    $errorPayload = @{ error = [string]$errJson.error }
                                }
                            }
                            Write-JsonResponse -Context $context -Payload $errorPayload -StatusCode 500
                            continue
                        }

                        if (-not (Test-Path $chatOutputPath)) {
                            Write-JsonResponse -Context $context -Payload @{ error = "Assistant did not produce output" } -StatusCode 500
                            continue
                        }

                        $responseText = [System.IO.File]::ReadAllText($chatOutputPath, [System.Text.Encoding]::UTF8)
                        $response = Parse-JsonSafe -Text $responseText
                        Write-JsonResponse -Context $context -Payload $response
                    } catch {
                        Write-JsonResponse -Context $context -Payload @{ error = $_.Exception.Message } -StatusCode 500
                    }
                    continue
                }

                default {
                    if ($request.HttpMethod -eq "GET" -and (Write-PublicFile -Context $context -RequestPath $path)) {
                        continue
                    }

                    Write-JsonResponse -Context $context -Payload @{ error = "Not found" } -StatusCode 404
                    continue
                }
            }
        } catch {
            Write-JsonResponse -Context $context -Payload @{ error = $_.Exception.Message } -StatusCode 500
        }
    }
} finally {
    $listener.Stop()
    $listener.Close()
}
