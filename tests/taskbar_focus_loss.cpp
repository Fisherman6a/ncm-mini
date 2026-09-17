#include "../src/NCMMini.HostCpp/Win11TaskbarWindow.h"

// Deliberately supply a non-foreground witness. The self-test must fail, never skip.
int wmain()
{
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return 2;
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 2;
    int result = 2;
    {
        ncmmini::Win11TaskbarWindow window;
        if (window.Start()) result = window.Run(20, true, GetDesktopWindow());
    }
    CoUninitialize();
    return result;
}
