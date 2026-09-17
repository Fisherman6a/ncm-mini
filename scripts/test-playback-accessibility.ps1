[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$compiler = Resolve-NcmCompiler
$output = Join-Path $root 'artifacts\integration-tests'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$sources = @('tests/playback_accessibility_tests.cpp','src/NCMMini.HostCpp/PlaybackAccessibility.cpp') | ForEach-Object { Join-Path $root $_ }
$exe = Join-Path $output 'playback_accessibility_tests.exe'
& $compiler '-std=c++17' '-O2' '-Wall' '-Wextra' '-static' '-DUNICODE' '-D_UNICODE' @sources '-o' $exe '-loleacc' '-lole32' '-loleaut32' '-luuid'
if ($LASTEXITCODE -ne 0) { throw "Playback test compilation failed: $LASTEXITCODE" }
& $exe
if ($LASTEXITCODE -ne 0) { throw "Playback tests failed: $LASTEXITCODE" }
