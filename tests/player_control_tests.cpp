#include "../src/NCMMini.HostCpp/Host.h"
#include <array>
#include <atomic>
#include <iostream>
#include <thread>

namespace
{
std::array<std::atomic_uint, 3> received{};
HANDLE ready = nullptr;
HWND target = nullptr;
LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_COMMAND && HIWORD(wparam) == 0x1800 && LOWORD(wparam) < 3)
    {
        // Check the queueing contract, not the closed-source player's command semantics.
        if (InSendMessageEx(nullptr) == ISMEX_NOSEND) ++received[LOWORD(wparam)];
        return 0;
    }
    if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wparam, lparam);
}
}
int main()
{
    ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ready) return 2;
    std::thread windowThread([] {
        WNDCLASSW cls{};
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpfnWndProc = Procedure;
        cls.lpszClassName = L"icon";
        RegisterClassW(&cls);
        target = CreateWindowW(L"icon", L"NCM test receiver", 0, 0, 0, 10, 10,
            nullptr, nullptr, cls.hInstance, nullptr);
        SetEvent(ready);
        MSG message{};
        while (target && GetMessageW(&message, nullptr, 0, 0) > 0) DispatchMessageW(&message);
    });
    WaitForSingleObject(ready, INFINITE);
    ncmmini::AppOptions options;
    options.launchCloudMusic = false; options.closeCloudMusicOnExit = false;
    ncmmini::PlayerController player(options);
    bool sent = target != nullptr;
    for (const auto command : {ncmmini::BandCommand::Previous, ncmmini::BandCommand::PlayPause, ncmmini::BandCommand::Next})
        sent = player.Send(command, GetCurrentProcessId()) && sent;
    for (unsigned i = 0; i < 100 && (received[0] != 1 || received[1] != 1 || received[2] != 1); ++i) Sleep(10);
    const bool dispatched = received[0] == 1 && received[1] == 1 && received[2] == 1;
    const bool missing = !player.Send(ncmmini::BandCommand::PlayPause, 0);
    const bool invalid = !player.Send(ncmmini::BandCommand::Options, GetCurrentProcessId());
    if (target) PostMessageW(target, WM_CLOSE, 0, 0);
    windowThread.join();
    CloseHandle(ready);
    std::cout << (sent && dispatched ? "PASS " : "FAIL ") << "queued previous/play/next commands reach the target exactly once\n"
        << (missing && invalid ? "PASS " : "FAIL ") << "missing target and invalid commands fail closed\n";
    return sent && dispatched && missing && invalid ? 0 : 1;
}
