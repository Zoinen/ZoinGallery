param(
    [Parameter(Mandatory = $true)]
    [string]$QtRoot,
    [Parameter(Mandatory = $true)]
    [ValidateSet("x64", "arm64")]
    [string]$TargetArch
)

$ErrorActionPreference = "Stop"
if ($PSVersionTable.PSVersion.Major -ge 7) {
    $PSNativeCommandUseErrorActionPreference = $true
}

$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path
$Exe = Get-ChildItem -Path (Join-Path $RepoRoot "build\bin") -Recurse -Filter "ZoinGallery.exe" |
    Select-Object -First 1

if (!$Exe) {
    throw "Could not find ZoinGallery.exe under build\bin"
}

$AppDir = Split-Path $Exe.FullName -Parent
& "$QtRoot\bin\windeployqt.exe" `
    --no-translations `
    --no-compiler-runtime `
    --qmldir (Join-Path $RepoRoot "qml") `
    $Exe.FullName

$QwkBin = Join-Path $RepoRoot "build\qwindowkit-install\bin"
if (Test-Path $QwkBin) {
    Copy-Item "$QwkBin\*.dll" -Destination $AppDir -Force -ErrorAction SilentlyContinue
}

if (!$env:VCToolsRedistDir) {
    throw "VCToolsRedistDir is not set by the MSVC environment"
}

$CrtDir = Join-Path $env:VCToolsRedistDir "$TargetArch\Microsoft.VC143.CRT"
if (!(Test-Path $CrtDir)) {
    $CrtDir = Get-ChildItem -Path (Join-Path $env:VCToolsRedistDir $TargetArch) -Directory -Filter "Microsoft.VC*.CRT" |
        Sort-Object Name -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}

if (!$CrtDir -or !(Test-Path $CrtDir)) {
    throw "Could not find the app-local MSVC CRT for $TargetArch under $env:VCToolsRedistDir"
}

Copy-Item (Join-Path $CrtDir "*.dll") -Destination $AppDir -Force

$RequiredRuntimeFiles = @(
    "msvcp140.dll",
    "vcruntime140.dll",
    "vcruntime140_1.dll"
)
$MissingRuntimeFiles = @(
    $RequiredRuntimeFiles | Where-Object { !(Test-Path (Join-Path $AppDir $_)) }
)

if ($MissingRuntimeFiles.Count -gt 0) {
    throw "Missing app-local MSVC runtime files: $($MissingRuntimeFiles -join ', ')"
}

Write-Host "Bundled app-local MSVC runtime from $CrtDir"
Get-ChildItem -Path $AppDir -Filter "*.dll" |
    Where-Object { $_.Name -match '^(msvcp|vcruntime|concrt)' } |
    Sort-Object Name |
    ForEach-Object { Write-Host "  $($_.Name)" }
