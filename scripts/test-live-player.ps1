[CmdletBinding()]
param(
    [switch]$ClickButtons,
    [switch]$DownloadCover,
    [switch]$PlaybackOnly,
    [switch]$SnapshotOnly
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$output = Join-Path $root 'artifacts\integration-tests'
New-Item -ItemType Directory -Path $output -Force | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class LiveTaskbarTest {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct Mouse { public int X,Y; public uint Data,Flags,Time; public UIntPtr Extra; }
    [StructLayout(LayoutKind.Explicit, Size=40)] public struct Input { [FieldOffset(0)] public uint Type; [FieldOffset(8)] public Mouse Mouse; }
    public delegate bool EnumCallback(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr window, EnumCallback callback, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window, StringBuilder text, int length);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr window, StringBuilder text, int length);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point point);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(Point point);
    [DllImport("user32.dll")] public static extern uint SendInput(uint count, Input[] input, int size);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
    [DllImport("wtsapi32.dll", CharSet=CharSet.Unicode)] static extern bool WTSQuerySessionInformation(IntPtr server, int session, int info, out IntPtr data, out uint bytes);
    [DllImport("wtsapi32.dll")] static extern void WTSFreeMemory(IntPtr data);
    public static bool DesktopReady() {
        IntPtr data; uint bytes;
        if (!WTSQuerySessionInformation(IntPtr.Zero, -1, 25, out data, out bytes)) return false;
        try {
            // WTSINFOEX level 1 on Windows 11: active connection and unlocked session.
            return bytes >= 20 && Marshal.ReadInt32(data) == 1
                && Marshal.ReadInt32(data, 12) == 0 && Marshal.ReadInt32(data, 16) == 1;
        } finally { WTSFreeMemory(data); }
    }
    public static string Title(IntPtr window) { var b=new StringBuilder(2048); GetWindowText(window,b,b.Capacity); return b.ToString(); }
    public static IntPtr Controller() { return FindWindow("NCMMini.Win11.Controller", null); }
    public static IntPtr Child(uint process) {
        IntPtr found=IntPtr.Zero;
        EnumChildWindows(FindWindow("Shell_TrayWnd", null), (w,p)=> {
            uint pid; GetWindowThreadProcessId(w,out pid);
            var b=new StringBuilder(256); GetClassName(w,b,b.Capacity);
            if(pid==process && b.ToString()=="NCMMini.TaskbarProbe.Child") { found=w; return false; }
            return true;
        },IntPtr.Zero);
        return found;
    }
    public static bool Button(bool down) {
        var input=new Input(); input.Type=0; input.Mouse.Flags=down?2u:4u;
        return SendInput(1,new[]{input},Marshal.SizeOf<Input>())==1;
    }
}
'@

if (-not [LiveTaskbarTest]::DesktopReady()) { throw 'Desktop is locked or unavailable. Unlock it before running live UI tests; no input was sent.' }
$existing = @(Get-Process -Name NCMMini -ErrorAction SilentlyContinue)
if ($SnapshotOnly -and ($ClickButtons -or $PlaybackOnly)) { throw 'SnapshotOnly cannot be combined with mouse tests.' }
if ($existing.Count -gt 1 -or ($existing.Count -and -not $SnapshotOnly)) { throw 'Close the existing NCMMini instance before this test.' }
$exe = Join-Path $root 'artifacts\win11-integration\NCMMini.exe'
$diagnostics = Join-Path $output 'PlayerDiagnostics.exe'
function Read-Playback {
    $state = & $diagnostics --playback-only
    if ($LASTEXITCODE -ne 0 -or $state -notin @('playing','paused','unknown')) { throw 'Cannot read playback status.' }
    return $state
}
function Save-TaskbarImage([IntPtr]$window, [string]$name) {
    if (-not [LiveTaskbarTest]::DesktopReady()) { throw 'Desktop became locked or unavailable; screenshot is not valid evidence.' }
    $rect = [LiveTaskbarTest+Rect]::new()
    if (-not [LiveTaskbarTest]::GetWindowRect($window, [ref]$rect)) { throw 'Cannot read screenshot bounds.' }
    $bitmap = [System.Drawing.Bitmap]::new($rect.Right-$rect.Left, $rect.Bottom-$rect.Top)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
        $bitmap.Save((Join-Path $output $name), [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $graphics.Dispose(); $bitmap.Dispose() }
}
$arguments = '--taskbar win11 --no-launch --keep-player --no-lyrics --duration 90'
if (-not $DownloadCover) { $arguments += ' --no-cover-download' }
$previousDpi = [LiveTaskbarTest]::SetThreadDpiAwarenessContext([IntPtr]::new(-4))
$original = [LiveTaskbarTest+Point]::new()
$cursorSaved = [LiveTaskbarTest]::GetCursorPos([ref]$original)
$last = [LiveTaskbarTest+Point]::new()
$cursorMoved = $false
$mouseDown = $false
$process = $null
$ownsHost = $false
$initialPlayback = $null
$playbackChanged = $false
try {
    if ($existing.Count -eq 1) {
        $process = $existing[0]
        Write-Output "Observing existing host: pid=$($process.Id)"
    } else {
        $process = Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
        $ownsHost = $true
        Write-Output "Host started: pid=$($process.Id)"
    }
    $child = [IntPtr]::Zero
    $deadline = [DateTime]::UtcNow.AddSeconds(12)
    do {
        if ($process.HasExited) { throw "Host exited early: $($process.ExitCode)" }
        $child = [LiveTaskbarTest]::Child([uint32]$process.Id)
        $title = [LiveTaskbarTest]::Title($child)
        if ($child -ne [IntPtr]::Zero -and [LiveTaskbarTest]::IsWindowVisible($child) -and $title -notlike 'NCM Mini*') { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($child -eq [IntPtr]::Zero -or -not [LiveTaskbarTest]::IsWindowVisible($child) -or $title -like 'NCM Mini*') { throw "Live song did not reach visible taskbar: hwnd=$child title=$title" }
    Write-Output "PASS live metadata visible: $title"
    Start-Sleep -Seconds 2
    $bounds = [LiveTaskbarTest+Rect]::new()
    if (-not [LiveTaskbarTest]::GetWindowRect($child, [ref]$bounds)) { throw 'Cannot read taskbar bounds.' }
    Save-TaskbarImage $child 'live-taskbar.png'
    if ($SnapshotOnly) {
        $observedPlayback = Read-Playback
        Save-TaskbarImage $child "observed-$observedPlayback.png"
        Write-Output "Observed playback=$observedPlayback; no playback commands sent. Inspect observed-$observedPlayback.png."
    }
    if ($ClickButtons -or $PlaybackOnly) {
        if (-not $cursorSaved) { throw 'Cannot save cursor.' }
        $initialPlayback = Read-Playback
        if ($initialPlayback -eq 'unknown') { throw 'Playback status is unknown; no toggle test was attempted.' }
        Save-TaskbarImage $child "live-$initialPlayback.png"
        $buttons = @(@('playpause',302), @('playpause',302))
        if ($ClickButtons -and -not $PlaybackOnly) { $buttons = @(@('next',338), @('previous',266)) + $buttons }
        foreach ($button in $buttons) {
            if (-not [LiveTaskbarTest]::DesktopReady()) { throw 'Desktop became locked or unavailable; no further mouse input was sent.' }
            $before = [LiveTaskbarTest]::Title($child)
            $beforePlayback = Read-Playback
            [void][LiveTaskbarTest]::GetWindowRect($child, [ref]$bounds)
            $last.X = $bounds.Left + [int](($bounds.Right-$bounds.Left)*$button[1]/360)
            $last.Y = ($bounds.Top+$bounds.Bottom)/2
            if (-not [LiveTaskbarTest]::SetCursorPos($last.X,$last.Y)) { throw 'Cannot move cursor.' }
            $cursorMoved = $true
            Start-Sleep -Milliseconds 200
            if ([LiveTaskbarTest]::WindowFromPoint($last) -ne $child) { throw 'Cursor does not hit taskbar control.' }
            if (-not [LiveTaskbarTest]::Button($true)) { throw 'Mouse down failed.' }
            $mouseDown = $true
            Start-Sleep -Milliseconds 80
            if (-not [LiveTaskbarTest]::Button($false)) { throw 'Mouse up failed.' }
            $mouseDown = $false
            $playbackChanged = $true
            if ($button[0] -ne 'playpause') {
                $deadline = [DateTime]::UtcNow.AddSeconds(8)
                do {
                    Start-Sleep -Milliseconds 150
                    $after = [LiveTaskbarTest]::Title($child)
                } while ($after -eq $before -and [DateTime]::UtcNow -lt $deadline)
                if ($after -eq $before) { throw "No real track change after $($button[0])." }
                Write-Output "PASS actual $($button[0]) track update: $after"
            } else {
                $expectedPlayback = if ($beforePlayback -eq 'playing') { 'paused' } else { 'playing' }
                $started = [DateTime]::UtcNow
                $deadline = $started.AddSeconds(5)
                $lastReport = $started
                do {
                    Start-Sleep -Milliseconds 250
                    $afterPlayback = Read-Playback
                    if (([DateTime]::UtcNow-$lastReport).TotalSeconds -ge 5) {
                        Write-Output ("Playback readback: {0}, elapsed={1:N1}s" -f $afterPlayback,([DateTime]::UtcNow-$started).TotalSeconds)
                        $lastReport = [DateTime]::UtcNow
                    }
                } while ($afterPlayback -ne $expectedPlayback -and [DateTime]::UtcNow -lt $deadline)
                if ($afterPlayback -ne $expectedPlayback) { throw "Actual playback did not change: $beforePlayback -> $afterPlayback" }
                $last.X = $bounds.Left + 20
                $last.Y = $bounds.Top - 20
                [void][LiveTaskbarTest]::SetCursorPos($last.X,$last.Y)
                Start-Sleep -Milliseconds 750
                Save-TaskbarImage $child "live-$afterPlayback.png"
                Write-Output ("PASS actual playback and taskbar screenshot: {0} -> {1}, readback={2:N1}s" -f $beforePlayback,$afterPlayback,([DateTime]::UtcNow-$started).TotalSeconds)
            }
        }
        if ((Read-Playback) -ne $initialPlayback) { throw 'Final playback state differs from the initial state.' }
        $playbackChanged = $false
        Write-Output "PASS restored initial playback: $initialPlayback"
    }
} finally {
    if ($mouseDown) { [void][LiveTaskbarTest]::Button($false) }
    if ($playbackChanged -and $initialPlayback) {
        Write-Warning "Test interrupted after a command; check player status. Initial state was $initialPlayback."
    }
    if ($ownsHost -and $process -and -not $process.HasExited) {
        $controller = [LiveTaskbarTest]::Controller()
        [uint32]$owner = 0
        if ($controller -ne [IntPtr]::Zero) { [void][LiveTaskbarTest]::GetWindowThreadProcessId($controller, [ref]$owner) }
        if ($owner -eq $process.Id) { [void][LiveTaskbarTest]::PostMessage($controller,0x10,[IntPtr]::Zero,[IntPtr]::Zero) }
        if (-not $process.WaitForExit(55000)) { Write-Warning 'Host has not exited; no forced termination was used.' }
        else { Write-Output "Host exit code: $($process.ExitCode)" }
    }
    $current = [LiveTaskbarTest+Point]::new()
    if ($cursorMoved -and [LiveTaskbarTest]::GetCursorPos([ref]$current) -and $current.X -eq $last.X -and $current.Y -eq $last.Y) {
        [void][LiveTaskbarTest]::SetCursorPos($original.X,$original.Y)
    }
    [void][LiveTaskbarTest]::SetThreadDpiAwarenessContext($previousDpi)
}
