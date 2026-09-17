[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$compiler = Resolve-NcmCompiler
$output = Join-Path $root 'artifacts\integration-tests'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$sources = @('tests/player_control_tests.cpp','src/NCMMini.HostCpp/Host.cpp','src/NCMMini.HostCpp/PlaybackAccessibility.cpp') | ForEach-Object { Join-Path $root $_ }
$exe = Join-Path $output 'player_control_tests.exe'
& $compiler -std=c++17 -O2 -Wall -Wextra -static -DUNICODE -D_UNICODE @sources -o $exe -lole32 -loleacc -loleaut32 -luuid -lshell32
if ($LASTEXITCODE -ne 0) { throw "Control test compilation failed: $LASTEXITCODE" }
& $exe
if ($LASTEXITCODE -ne 0) { throw "Control tests failed: $LASTEXITCODE" }
