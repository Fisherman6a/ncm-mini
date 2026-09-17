$ErrorActionPreference = 'Stop'

function Resolve-NcmCompiler {
    param([string]$CompilerPath)

    if ($CompilerPath) {
        $resolved = (Resolve-Path -LiteralPath $CompilerPath).Path
    } else {
        $command = Get-Command g++.exe -ErrorAction SilentlyContinue
        if ($command) {
            $resolved = $command.Source
        } else {
            $candidate = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\g++.exe'
            if (-not (Test-Path -LiteralPath $candidate)) {
                throw 'g++.exe was not found. Reopen PowerShell 7 after installation or pass -CompilerPath. See docs/toolchain-setup.md.'
            }
            $resolved = $candidate
        }
    }
    $env:PATH = (Split-Path -Parent $resolved) + [IO.Path]::PathSeparator + $env:PATH
    return $resolved
}
