#include "../src/NCMMini.HostCpp/Win11TaskbarWindow.h"

#include <cwchar>
#include <iostream>
#include <string>

int wmain(int argc, wchar_t** argv)
{
    bool scanOnly = false;
    bool selfTest = false;
    std::wstring coverPath;
    unsigned int duration = 30;
    for (int index = 1; index < argc; ++index)
    {
        const std::wstring argument = argv[index];
        if (argument == L"--scan-only") scanOnly = true;
        else if (argument == L"--self-test") selfTest = true;
        else if (argument == L"--cover" && index + 1 < argc) coverPath = argv[++index];
        else if (argument == L"--duration" && index + 1 < argc)
        {
            wchar_t* end = nullptr;
            const long seconds = wcstol(argv[++index], &end, 10);
            if (end == argv[index] || *end || seconds < 1 || seconds > 3600) return 2;
            duration = static_cast<unsigned int>(seconds);
        }
        else
        {
            std::wcerr << L"Usage: NCMMiniTaskbarProbe.exe [--scan-only] [--self-test] [--cover image.png] [--duration 1..3600]\n";
            return 2;
        }
    }
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
    {
        std::wcerr << L"DPI initialization failed: " << GetLastError() << L'\n';
        return 1;
    }
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) return 1;
    int exitCode = 0;
    if (scanOnly)
    {
        const auto snapshot = ncmmini::ScanTaskbar(true);
        const auto placement = snapshot.valid ? ncmmini::FindTaskbarPlacement(snapshot.bounds,
            snapshot.occupied, MulDiv(ncmmini::TaskbarViewWidth, snapshot.dpi, 96), MulDiv(ncmmini::TaskbarViewHeight, snapshot.dpi, 96),
            MulDiv(4, snapshot.dpi, 96)) : std::nullopt;
        std::wcout << L"scan valid=" << snapshot.valid << L" taskbar=" << snapshot.window
            << L" dpi=" << snapshot.dpi << L" occupied=" << snapshot.occupied.size()
            << L" placement=" << placement.has_value() << L" error=" << snapshot.error << L'\n';
        exitCode = snapshot.valid && placement ? 0 : 1;
    }
    else
    {
        HANDLE instance = CreateMutexW(nullptr, TRUE, L"Local\\NCMMini.TaskbarProbe");
        if (!instance || GetLastError() == ERROR_ALREADY_EXISTS)
        {
            if (instance) CloseHandle(instance);
            CoUninitialize();
            return 2;
        }
        HWND witness = nullptr;
        const HWND previousForeground = GetForegroundWindow();
        if (selfTest)
        {
            WNDCLASSW wc{};
            wc.lpfnWndProc = DefWindowProcW;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"NCMMini.TaskbarProbe.FocusTest";
            RegisterClassW(&wc);
            witness = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName,
                L"NCM Mini - focus test", WS_OVERLAPPED | WS_CAPTION | WS_VISIBLE,
                100, 100, 320, 100, nullptr, nullptr, wc.hInstance, nullptr);
            if (witness) SetForegroundWindow(witness);
            if (!witness || GetForegroundWindow() != witness)
            {
                std::wcerr << L"Foreground focus check unavailable; continuing with mouse and repaint tests\n";
                if (witness) DestroyWindow(witness);
                witness = nullptr;
            }
        }
        ncmmini::Win11TaskbarWindow window;
        exitCode = window.Start(coverPath) ? window.Run(duration, selfTest, witness) : 1;
        window.Stop();
        if (witness)
        {
            const bool restoreForeground = GetForegroundWindow() == witness;
            DestroyWindow(witness);
            if (restoreForeground && IsWindow(previousForeground)) SetForegroundWindow(previousForeground);
        }
        ReleaseMutex(instance);
        CloseHandle(instance);
    }
    CoUninitialize();
    return exitCode;
}
