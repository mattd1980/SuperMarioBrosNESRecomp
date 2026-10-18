# Package SuperMarioBrosRecomp for Android (see README.md).
#
# Expects the NDK CMake builds of the game in <BuildRoot>\build-arm64 and
# build-x86_64, and a Windows build_release for the staged UI assets and mod
# packages. Stages them into the Gradle project, then runs Gradle
# (release: signed with the debug key, personal sideload; debug: also accepts
# test-harness launch arguments, see SmbActivity.EXTRA_ARGS).
param(
    [string]$Game = (Split-Path $PSScriptRoot -Parent),
    [string]$BuildRoot = $PSScriptRoot,
    [string]$SdlRoot = (Join-Path $PSScriptRoot 'third_party\SDL2-2.30.12'),   # for SDL's Java classes
    [string[]]$Abis = @('arm64-v8a', 'x86_64'),
    [string]$Sdk = $(if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { Join-Path $env:LOCALAPPDATA 'Android\Sdk' }),
    [string]$Ndk = '28.2.13676358',
    [string]$Gradle = $(if ($env:GRADLE) { $env:GRADLE } else { 'gradle' }),   # Gradle 8.13+
    [string]$Jdk = $env:JAVA_HOME,                                             # JDK 17
    [ValidateSet('release', 'debug')] [string]$Variant = 'release'
)
$ErrorActionPreference = 'Stop'
$app = Join-Path $PSScriptRoot 'app-project\app'
$native = Join-Path $app 'build-native'
$ndkStrip = Join-Path $Sdk "ndk\$Ndk\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-strip.exe"
if (-not $Jdk) { throw 'set JAVA_HOME (JDK 17) or pass -Jdk' }

if (Test-Path -LiteralPath $native) { Remove-Item -LiteralPath $native -Recurse -Force }
$buildDir = @{ 'arm64-v8a' = 'build-arm64'; 'x86_64' = 'build-x86_64' }
$packed = @()
foreach ($abi in $Abis) {
    $b = Join-Path $BuildRoot $buildDir[$abi]
    $main = Join-Path $b 'libmain.so'
    $sdl = Join-Path $b 'sdl_build\libSDL2.so'
    if (-not (Test-Path -LiteralPath $main) -or -not (Test-Path -LiteralPath $sdl)) { Write-Warning "skipping $abi (no native build in $b)"; continue }
    $dst = Join-Path $native "jniLibs\$abi"
    New-Item -ItemType Directory -Force $dst | Out-Null
    foreach ($lib in $main, $sdl) {
        $out = Join-Path $dst (Split-Path $lib -Leaf)
        & $ndkStrip --strip-unneeded -o $out $lib          # 30+ MB of symbols are not shipped
        if ($LASTEXITCODE) { throw "strip failed for $lib" }
    }
    $packed += $abi
}
if (-not $packed) { throw 'no ABI has a native build' }

# UI fonts/images for the in-game menu, and the mod packages (manifests).
$release = Join-Path $Game 'build_release\Release'
$assets = Join-Path $native 'assets'
New-Item -ItemType Directory -Force (Join-Path $assets 'assets'), (Join-Path $assets 'mods') | Out-Null
Copy-Item -Recurse -Force (Join-Path $release 'assets\fonts'), (Join-Path $release 'assets\img') (Join-Path $assets 'assets')
Copy-Item -Recurse -Force (Join-Path $release 'mods\packages') (Join-Path $assets 'mods')

$env:JAVA_HOME = $Jdk
$env:ANDROID_HOME = $Sdk
Push-Location (Join-Path $PSScriptRoot 'app-project')
try {
    $ErrorActionPreference = 'Continue'   # Gradle/SDK warnings go to stderr
    $task = if ($Variant -eq 'debug') { 'assembleDebug' } else { 'assembleRelease' }
    $sdlJava = Join-Path $SdlRoot 'android-project\app\src\main\java'
    & $Gradle --no-daemon --console=plain "-PsdlJava=$sdlJava" $task 2>&1 | ForEach-Object { "$_" }
    $ErrorActionPreference = 'Stop'
    if ($LASTEXITCODE) { throw "gradle failed ($LASTEXITCODE)" }
} finally { Pop-Location }
$apk = Get-ChildItem (Join-Path $app "build\outputs\apk\$Variant") -Filter *.apk | Select-Object -First 1
"APK: $($apk.FullName) ($([math]::Round($apk.Length / 1MB, 1)) MB, ABIs: $($packed -join ', '))"
