[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$compiler = Resolve-NcmCompiler
$output = Join-Path $root 'artifacts\integration-tests'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$sources = @('tests/taskbar_transport_tests.cpp', 'src/NCMMini.HostCpp/Host.cpp', 'src/NCMMini.HostCpp/PlaybackAccessibility.cpp', 'src/NCMMini.HostCpp/TaskbarPresenter.cpp', 'src/NCMMini.HostCpp/TaskbarBinding.cpp', 'src/NCMMini.HostCpp/TaskbarView.cpp', 'src/NCMMini.HostCpp/TaskbarLayout.cpp', 'src/NCMMini.HostCpp/TaskbarScanner.cpp', 'src/NCMMini.HostCpp/Win11TaskbarWindow.cpp') | ForEach-Object { Join-Path $root $_ }
& $compiler '-std=c++17' '-O2' '-Wall' '-Wextra' '-DUNICODE' '-D_UNICODE' '-DNOMINMAX' '-D_WIN32_WINNT=0x0A00' '-static' @sources '-o' (Join-Path $output 'taskbar_transport_tests.exe') '-lgdiplus' '-lgdi32' '-lole32' '-loleacc' '-loleaut32' '-luuid' '-luiautomationcore' '-lcomctl32' '-ladvapi32' '-lshell32'
if ($LASTEXITCODE -ne 0) { throw "Transport tests compilation failed: $LASTEXITCODE" }
