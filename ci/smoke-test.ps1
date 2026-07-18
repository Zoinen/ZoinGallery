param(
    [Parameter(Mandatory = $true)]
    [string]$QtRoot
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path
$Exe = Get-ChildItem -Path (Join-Path $RepoRoot "build\bin") -Recurse -Filter "ZoinGallery.exe" |
    Select-Object -First 1

if (!$Exe) {
    throw "Could not find ZoinGallery.exe under build\bin"
}

$QwkBin = Join-Path $RepoRoot "build\qwindowkit-install\bin"
$BuildBin = Split-Path $Exe.FullName -Parent
$env:PATH = "$BuildBin;$QwkBin;$QtRoot\bin;$env:PATH"

$RequiredDeployFiles = @(
    "Qt6Core.dll",
    "Qt6Qml.dll",
    "Qt6Quick.dll",
    "Qt6Svg.dll",
    "QWKCore.dll",
    "QWKQuick.dll",
    "msvcp140.dll",
    "vcruntime140.dll",
    "vcruntime140_1.dll",
    "platforms\qwindows.dll",
    "qml\QtQuick\Controls\qtquickcontrols2plugin.dll"
)

$MissingDeployFiles = @(
    $RequiredDeployFiles | Where-Object {
        !(Test-Path (Join-Path $BuildBin $_))
    }
)

if ($MissingDeployFiles.Count -gt 0) {
    Write-Host "Missing deployed runtime files:"
    $MissingDeployFiles | ForEach-Object { Write-Host "  $_" }
    throw "Windows artifact is missing required runtime files"
}

$Stdout = Join-Path $env:RUNNER_TEMP "zoingallery-smoke.out"
$Stderr = Join-Path $env:RUNNER_TEMP "zoingallery-smoke.err"
$Process = Start-Process -FilePath $Exe.FullName -PassThru -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr
Start-Sleep -Seconds 8

if (!$Process.HasExited) {
    Stop-Process -Id $Process.Id -Force
    Write-Host "ZoinGallery started and stayed alive for the smoke window."
    exit 0
}

Write-Host "--- stdout ---"
if (Test-Path $Stdout) { Get-Content $Stdout -TotalCount 120 }
Write-Host "--- stderr ---"
if (Test-Path $Stderr) { Get-Content $Stderr -TotalCount 160 }

$LogRoot = Join-Path $env:LOCALAPPDATA "Zoin\ZoinGallery\logs"
$LatestLog = Get-ChildItem -Path $LogRoot -Recurse -Filter "ZoinGallery-*.log" -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if ($LatestLog) {
    Write-Host "--- application log: $($LatestLog.FullName) ---"
    Get-Content $LatestLog.FullName -Tail 200
}

throw "ZoinGallery exited with code $($Process.ExitCode) during the smoke window"
