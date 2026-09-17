[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$CompilerPath,
    [switch]$BuildFocusRegression
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$compiler = Resolve-NcmCompiler -CompilerPath $CompilerPath
$output = Join-Path $root 'artifacts\taskbar-probe'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$flags = @('-std=c++17', '-Wall', '-Wextra', '-DUNICODE', '-D_UNICODE', '-DNOMINMAX', '-DNCM_TASKBAR_PROBE', '-D_WIN32_WINNT=0x0A00', '-static', '-static-libgcc', '-static-libstdc++')
$flags += if ($Configuration -eq 'Debug') { @('-O0', '-g') } else { @('-O2') }
$layout = Join-Path $root 'src\NCMMini.HostCpp\TaskbarLayout.cpp'
& $compiler @flags (Join-Path $root 'tests\taskbar_layout_tests.cpp') $layout '-o' (Join-Path $output 'taskbar_layout_tests.exe')
if ($LASTEXITCODE -ne 0) { throw "Layout test compilation failed: $LASTEXITCODE" }
& (Join-Path $output 'taskbar_layout_tests.exe')
if ($LASTEXITCODE -ne 0) { throw "Layout tests failed: $LASTEXITCODE" }

$view = Join-Path $root 'src\NCMMini.HostCpp\TaskbarView.cpp'
& $compiler @flags (Join-Path $root 'tests\taskbar_view_tests.cpp') $view $layout '-o' (Join-Path $output 'taskbar_view_tests.exe') '-lgdiplus' '-lgdi32' '-lole32'
if ($LASTEXITCODE -ne 0) { throw "View test compilation failed: $LASTEXITCODE" }
& (Join-Path $output 'taskbar_view_tests.exe')
if ($LASTEXITCODE -ne 0) { throw "View tests failed: $LASTEXITCODE" }

$sources = @(
    (Join-Path $root 'tools\TaskbarProbe.cpp'),
    (Join-Path $root 'tools\TaskbarMouseTest.cpp'),
    (Join-Path $root 'src\NCMMini.HostCpp\Win11TaskbarWindow.cpp'),
    (Join-Path $root 'src\NCMMini.HostCpp\TaskbarScanner.cpp'),
    (Join-Path $root 'src\NCMMini.HostCpp\TaskbarBinding.cpp'),
    $layout,
    $view
)
& $compiler @flags '-municode' @sources '-o' (Join-Path $output 'NCMMiniTaskbarProbe.exe') '-lole32' '-loleaut32' '-luuid' '-luiautomationcore' '-luser32' '-lgdi32' '-lgdiplus' '-lcomctl32' '-ladvapi32'
if ($LASTEXITCODE -ne 0) { throw "Taskbar probe compilation failed: $LASTEXITCODE" }
if ($BuildFocusRegression) {
    $focusSources = @((Join-Path $root 'tests\taskbar_focus_loss.cpp')) + $sources[1..($sources.Count - 1)]
    & $compiler @flags '-municode' @focusSources '-o' (Join-Path $output 'taskbar_focus_loss.exe') '-lole32' '-loleaut32' '-luuid' '-luiautomationcore' '-luser32' '-lgdi32' '-lgdiplus' '-lcomctl32' '-ladvapi32'
    if ($LASTEXITCODE -ne 0) { throw "Focus regression compilation failed: $LASTEXITCODE" }
}
if ($Configuration -eq 'Release') {
    & (Join-Path (Split-Path -Parent $compiler) 'strip.exe') (Join-Path $output 'NCMMiniTaskbarProbe.exe')
    if ($LASTEXITCODE -ne 0) { throw "Stripping failed: $LASTEXITCODE" }
}
Write-Host "Taskbar probe output: $output"
