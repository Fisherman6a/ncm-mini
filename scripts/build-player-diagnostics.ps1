[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$compiler = Resolve-NcmCompiler
$output = Join-Path $root 'artifacts\integration-tests'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$sources = @('tools/PlayerDiagnostics.cpp', 'src/NCMMini.HostCpp/Host.cpp', 'src/NCMMini.HostCpp/PlaybackAccessibility.cpp', 'src/NCMMini.HostCpp/Media.cpp', 'src/NCMMini.HostCpp/Json.cpp') | ForEach-Object { Join-Path $root $_ }
& $compiler '-std=c++17' '-O2' '-Wall' '-Wextra' '-municode' '-DUNICODE' '-D_UNICODE' '-DNOMINMAX' '-static' @sources '-o' (Join-Path $output 'PlayerDiagnostics.exe') '-lole32' '-loleacc' '-loleaut32' '-luuid' '-lshell32' '-lwinhttp' '-lwindowscodecs'
if ($LASTEXITCODE -ne 0) { throw "Player diagnostics compilation failed: $LASTEXITCODE" }
