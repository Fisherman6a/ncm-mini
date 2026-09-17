[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$compiler = Resolve-NcmCompiler
$output = Join-Path $root 'artifacts\binding-tests'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$sources = @('tests/taskbar_binding_tests.cpp', 'src/NCMMini.HostCpp/TaskbarBinding.cpp', 'src/NCMMini.HostCpp/TaskbarView.cpp', 'src/NCMMini.HostCpp/TaskbarLayout.cpp', 'src/NCMMini.HostCpp/Host.cpp', 'src/NCMMini.HostCpp/PlaybackAccessibility.cpp') | ForEach-Object { Join-Path $root $_ }
& $compiler '-std=c++17' '-O2' '-Wall' '-Wextra' '-DUNICODE' '-D_UNICODE' '-DNOMINMAX' '-static' @sources '-o' (Join-Path $output 'taskbar_binding_tests.exe') '-lgdiplus' '-lgdi32' '-lole32' '-loleacc' '-loleaut32' '-luuid' '-lshell32'
if ($LASTEXITCODE -ne 0) { throw "Binding tests compilation failed: $LASTEXITCODE" }
& (Join-Path $output 'taskbar_binding_tests.exe')
if ($LASTEXITCODE -ne 0) { throw "Binding tests failed: $LASTEXITCODE" }
