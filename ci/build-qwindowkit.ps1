param(
    [Parameter(Mandatory = $true)]
    [string]$QtRoot,
    [string]$BuildType = "RelWithDebInfo"
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path
$QwkSource = Join-Path $RepoRoot "build\qwindowkit-src"
$QwkBuild = Join-Path $RepoRoot "build\qwindowkit-build"
$QwkInstall = Join-Path $RepoRoot "build\qwindowkit-install"

Remove-Item -Recurse -Force $QwkSource, $QwkBuild, $QwkInstall -ErrorAction SilentlyContinue
git clone --recursive --branch main https://github.com/stdware/qwindowkit.git $QwkSource

cmake -S $QwkSource -B $QwkBuild -G Ninja `
    -DCMAKE_BUILD_TYPE=$BuildType `
    -DCMAKE_PREFIX_PATH="$QtRoot" `
    -DCMAKE_INSTALL_PREFIX="$QwkInstall" `
    -DQWINDOWKIT_BUILD_QUICK=TRUE `
    -DQWINDOWKIT_BUILD_WIDGETS=FALSE `
    -DQWINDOWKIT_BUILD_EXAMPLES=FALSE `
    -DQWINDOWKIT_BUILD_DOCUMENTATIONS=FALSE

cmake --build $QwkBuild --parallel
cmake --install $QwkBuild

if (!(Test-Path "$QwkInstall\lib\cmake\QWindowKit\QWindowKitConfig.cmake")) {
    throw "QWindowKit package config was not installed"
}

Select-String -Path "$QwkInstall\lib\cmake\QWindowKit\*.cmake" -Pattern "QWindowKit::Quick" | Out-Host
