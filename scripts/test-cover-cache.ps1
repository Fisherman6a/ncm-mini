[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$CompilerPath,
    [switch]$VerifyLocalCache
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$compiler = Resolve-NcmCompiler -CompilerPath $CompilerPath
$output = Join-Path $root 'artifacts\cover-tests'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$flags = @('-std=c++17', '-Wall', '-Wextra', '-DUNICODE', '-D_UNICODE', '-DNOMINMAX', '-D_WIN32_WINNT=0x0A00', '-static', '-static-libgcc', '-static-libstdc++')
$flags += if ($Configuration -eq 'Debug') { @('-O0', '-g') } else { @('-O2') }
$sources = @(
    (Join-Path $root 'tests\cover_cache_tests.cpp'),
    (Join-Path $root 'src\NCMMini.HostCpp\Media.cpp'),
    (Join-Path $root 'src\NCMMini.HostCpp\Json.cpp'),
    (Join-Path $root 'src\NCMMini.HostCpp\Host.cpp'),
    (Join-Path $root 'src\NCMMini.HostCpp\PlaybackAccessibility.cpp')
)
$executable = Join-Path $output 'cover_cache_tests.exe'
& $compiler @flags @sources '-o' $executable '-lole32' '-loleacc' '-loleaut32' '-luuid' '-luser32' '-lshell32' '-lwinhttp' '-lwindowscodecs'
if ($LASTEXITCODE -ne 0) { throw "Cover test compilation failed: $LASTEXITCODE" }
& $executable
if ($LASTEXITCODE -ne 0) { throw "Cover cache tests failed: $LASTEXITCODE" }
if ($VerifyLocalCache) {
    & $executable '--verify-local-cache'
    if ($LASTEXITCODE -ne 0) { throw "Local offline cover cache check failed: $LASTEXITCODE" }
}
Write-Host "Cover cache test output: $output"
