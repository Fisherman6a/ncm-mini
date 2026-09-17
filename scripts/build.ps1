[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$OutputDirectory,
    [string]$CompilerPath
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$output = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $root 'artifacts\publish' }
$staging = Join-Path $root ('artifacts\build-' + [guid]::NewGuid().ToString('N'))
$bandSource = Join-Path $root 'src\NCMMini.Band'
$hostSource = Join-Path $root 'src\NCMMini.HostCpp'
$commonSource = Join-Path $root 'src\Common'

. (Join-Path $PSScriptRoot 'toolchain.ps1')
$compiler = Resolve-NcmCompiler -CompilerPath $CompilerPath
New-Item $staging -ItemType Directory -Force | Out-Null

$commonFlags = @(
    '-std=c++17',
    '-DUNICODE',
    '-D_UNICODE',
    '-DNOMINMAX',
    '-static',
    '-static-libgcc',
    '-static-libstdc++'
)
$commonFlags += if ($Configuration -eq 'Debug') { @('-O0', '-g') } else { @('-O2') }
$bandLibraries = @(
    '-lole32',
    '-luuid',
    '-ladvapi32',
    '-luser32',
    '-lgdi32',
    '-luxtheme'
)

& $compiler '-shared' (Join-Path $bandSource 'NCMMiniBand.cpp') (Join-Path $commonSource 'Settings.cpp') '-o' (Join-Path $staging 'NCMMiniBand.dll') @commonFlags @bandLibraries
if ($LASTEXITCODE -ne 0) { throw "DeskBand compilation failed with exit code $LASTEXITCODE." }

& $compiler '-municode' (Join-Path $bandSource 'NCMMiniBandCtl.cpp') '-o' (Join-Path $staging 'NCMMiniBandCtl.exe') @commonFlags @bandLibraries
if ($LASTEXITCODE -ne 0) { throw "DeskBand controller compilation failed with exit code $LASTEXITCODE." }

$hostSources = @(
    (Join-Path $hostSource 'main.cpp'),
    (Join-Path $hostSource 'Host.cpp'),
    (Join-Path $hostSource 'PlaybackAccessibility.cpp'),
    (Join-Path $hostSource 'Json.cpp'),
    (Join-Path $hostSource 'Media.cpp'),
    (Join-Path $hostSource 'PipeServer.cpp'),
    (Join-Path $hostSource 'SettingsWindow.cpp'),
    (Join-Path $hostSource 'TaskbarLayout.cpp'),
    (Join-Path $hostSource 'TaskbarScanner.cpp'),
    (Join-Path $hostSource 'TaskbarView.cpp'),
    (Join-Path $hostSource 'TaskbarBinding.cpp'),
    (Join-Path $hostSource 'Win11TaskbarWindow.cpp'),
    (Join-Path $hostSource 'TaskbarPresenter.cpp'),
    (Join-Path $commonSource 'Settings.cpp')
)
$hostLibraries = @('-lole32', '-loleacc', '-loleaut32', '-luuid', '-luser32', '-lgdi32', '-lcomdlg32', '-lshell32', '-lwinhttp', '-lwindowscodecs', '-luiautomationcore', '-lgdiplus', '-lcomctl32', '-ladvapi32')
& $compiler '-municode' '-mwindows' '-D_WIN32_WINNT=0x0A00' @hostSources '-o' (Join-Path $staging 'NCMMini.exe') @commonFlags @hostLibraries
if ($LASTEXITCODE -ne 0) { throw "Native host compilation failed with exit code $LASTEXITCODE." }

if ($Configuration -eq 'Release') {
    & (Join-Path (Split-Path -Parent $compiler) 'strip.exe') (Join-Path $staging 'NCMMini.exe') (Join-Path $staging 'NCMMiniBand.dll') (Join-Path $staging 'NCMMiniBandCtl.exe')
    if ($LASTEXITCODE -ne 0) { throw "Stripping failed with exit code $LASTEXITCODE." }
}

Copy-Item (Join-Path $PSScriptRoot 'install.ps1') $staging
Copy-Item (Join-Path $PSScriptRoot 'uninstall.ps1') $staging
Copy-Item (Join-Path $PSScriptRoot 'config.ini') $staging
New-Item $output -ItemType Directory -Force | Out-Null
try {
    Get-ChildItem -LiteralPath $staging -File | ForEach-Object {
        if ($_.Name -ne 'config.ini' -or -not (Test-Path -LiteralPath (Join-Path $output 'config.ini'))) {
            Copy-Item -LiteralPath $_.FullName -Destination $output -Force
        }
    }
} catch {
    throw "Build succeeded, but copying output failed. Use a different -OutputDirectory or close the program that holds the output. Compiled files remain at $staging. $($_.Exception.Message)"
}
# Only remove the unique staging directory created by this invocation.
$stagingFullPath = (Resolve-Path -LiteralPath $staging).Path
$artifactsPrefix = [IO.Path]::GetFullPath((Join-Path $root 'artifacts')) + [IO.Path]::DirectorySeparatorChar
if (-not $stagingFullPath.StartsWith($artifactsPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Unexpected staging path; cleanup refused.'
}
Remove-Item -LiteralPath $stagingFullPath -Recurse -Force
Write-Host "NCM Mini native build output: $output"
