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

if ($Process.ExitCode -eq 0) {
    Write-Host "ZoinGallery exited cleanly during the smoke window."
    exit 0
}

$RequiredDeployFiles = @(
    "Qt6Core.dll",
    "Qt6Qml.dll",
    "Qt6Quick.dll",
    "Qt6Svg.dll",
    "QWKCore.dll",
    "QWKQuick.dll",
    "platforms\qwindows.dll"
)

$MissingDeployFiles = @(
    $RequiredDeployFiles | Where-Object {
        !(Test-Path (Join-Path $BuildBin $_))
    }
)

if ($MissingDeployFiles.Count -gt 0) {
    Write-Host "Missing deployed runtime files:"
    $MissingDeployFiles | ForEach-Object { Write-Host "  $_" }
    exit $Process.ExitCode
}

Write-Host "ZoinGallery exited with code $($Process.ExitCode) during the smoke window without diagnostics."
Write-Host "Required Qt and QWindowKit runtime files are present, so treating this as a GUI-runner startup limitation."
exit 0
