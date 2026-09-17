#pragma once

#include <windows.h>
#include <cstring>

namespace ncmmini
{
// Resolve modern APIs lazily so loading the host does not break older DeskBand systems.
class TaskbarDpi
{
    template <typename Function>
    static Function Resolve(const char* name)
    {
        const auto address = GetProcAddress(GetModuleHandleW(L"user32.dll"), name);
        Function function = nullptr;
        static_assert(sizeof(function) == sizeof(address));
        std::memcpy(&function, &address, sizeof(function));
        return function;
    }

    using SetContext = DPI_AWARENESS_CONTEXT (WINAPI*)(DPI_AWARENESS_CONTEXT);
    using GetContext = DPI_AWARENESS_CONTEXT (WINAPI*)(HWND);
    using GetDpi = UINT (WINAPI*)(HWND);

public:
    static bool Supported()
    {
        return Resolve<SetContext>("SetThreadDpiAwarenessContext")
            && Resolve<GetContext>("GetWindowDpiAwarenessContext")
            && Resolve<GetDpi>("GetDpiForWindow");
    }

    static DPI_AWARENESS_CONTEXT SetThread(DPI_AWARENESS_CONTEXT context)
    {
        static const auto function = Resolve<SetContext>("SetThreadDpiAwarenessContext");
        return function ? function(context) : nullptr;
    }

    static DPI_AWARENESS_CONTEXT WindowContext(HWND window)
    {
        static const auto function = Resolve<GetContext>("GetWindowDpiAwarenessContext");
        return function ? function(window) : nullptr;
    }

    static UINT WindowDpi(HWND window)
    {
        static const auto function = Resolve<GetDpi>("GetDpiForWindow");
        return function ? function(window) : 96;
    }
};
}
