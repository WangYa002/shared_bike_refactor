<#
.SYNOPSIS
  Spawn N bike_bench.py QPS workers in parallel, then aggregate their JSON outputs.

.PARAMETER Workers
  Number of parallel Python processes (default: 4).

.PARAMETER Concurrency
  Concurrency PER WORKER (default: 500). Total concurrency = Workers * Concurrency.

.PARAMETER Duration
  Bench duration in seconds (default: 20).

.PARAMETER Host
  Target host (default: 124.220.92.243).

.Parameter Port
  Target port (default: 8888).

.PARAMETER Tag
  Output filename tag (default: 'run'). Produces <Tag>_w0.json, <Tag>_w1.json, ...

.EXAMPLE
  .\run_bench.ps1 -Workers 4 -Concurrency 125 -Duration 20 -Tag 3B-single
#>
[CmdletBinding()]
param(
    [int]$Workers = 4,
    [int]$Concurrency = 500,
    [int]$Duration = 20,
    [string]$Host = '124.220.92.243',
    [int]$Port = 8888,
    [string]$Tag = 'run'
)

$ErrorActionPreference = 'Stop'
Set-Location -Path (Split-Path -Parent $MyInvocation.MyCommand.Path)

Write-Host "=== run_bench: $Workers workers x $Concurrency concurrency = $($Workers * $Concurrency) total, ${Duration}s ==="

# 1. Launch all workers in parallel
$jobs = @()
for ($w = 0; $w -lt $Workers; $w++) {
    $outFile = "${Tag}_w${w}.json"
    Write-Host "  starting worker $w -> $outFile"
    $jobs += Start-Process -FilePath 'python' `
        -ArgumentList @(
            'bike_bench.py', 'qps',
            '--host', $Host,
            '--port', $Port,
            '-c', $Concurrency,
            '-d', $Duration,
            '--worker-id', $w,
            '--total-workers', $Workers,
            '--json-out', $outFile
        ) -NoNewWindow -PassThru
}

# 2. Wait for all to finish
Write-Host "  waiting for $Workers workers..."
$jobs | Wait-Process | Out-Null
$failed = $jobs | Where-Object { $_.ExitCode -ne 0 }
if ($failed) {
    Write-Error "$($failed.Count) worker(s) exited non-zero"
    exit 1
}

# 3. Aggregate
$combined = "${Tag}_combined.json"
Write-Host "  aggregating -> $combined"
python aggregate_workers.py --glob "${Tag}_w*.json" -o $combined
if ($LASTEXITCODE -ne 0) {
    Write-Error "aggregation failed"
    exit 1
}

Write-Host "=== done. see $combined ==="
