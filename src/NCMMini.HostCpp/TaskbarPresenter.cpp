#include "TaskbarPresenter.h"
#include "TaskbarDpi.h"

#include <future>
#include <cstring>

namespace ncmmini
{
DWORD WindowsBuild()
{
    using VersionFunction = LONG (WINAPI*)(OSVERSIONINFOW*);
    const auto address = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    VersionFunction version = nullptr;
    static_assert(sizeof(version) == sizeof(address));
    std::memcpy(&version, &address, sizeof(version));
    OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    return version && version(&info) == 0 ? info.dwBuildNumber : 0;
}

bool HasModernTaskbar()
{
    const auto taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    return taskbar && FindWindowExW(taskbar, nullptr,
        L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr);
}

TaskbarPresenter::~TaskbarPresenter() { Stop(); }

bool TaskbarPresenter::Start(Win11TaskbarWindow::CommandHandler handler, bool visible)
{
    if (thread_.joinable()) return false;
    stopping_ = false;
    std::promise<bool> ready;
    auto result = ready.get_future();
    thread_ = std::thread([this, handler = std::move(handler), visible, ready = std::move(ready)]() mutable {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(com)) { ready.set_value(false); return; }
        const auto dpi = TaskbarDpi::SetThread(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        if (!dpi)
        {
            ready.set_value(false);
            CoUninitialize();
            return;
        }
        {
            Win11TaskbarWindow window;
            const bool started = window.Start({}, handler, visible);
            Log(started ? L"Win11 window thread initialized" : L"Win11 window thread initialization failed");
            {
                std::lock_guard lock(mutex_);
                if (started) window_ = &window;
            }
            ready.set_value(started);
            if (started) window.Run(0, false);
            {
                std::lock_guard lock(mutex_);
                window_ = nullptr;
            }
            window.Stop();
            if (started && !stopping_) handler(BandCommand::Exit);
        }
        CoUninitialize();
        TaskbarDpi::SetThread(dpi);
    });
    if (result.get()) return true;
    Stop();
    return false;
}

void TaskbarPresenter::Publish(const BandState& state)
{
    std::lock_guard lock(mutex_);
    if (window_) window_->Publish(state);
}

void TaskbarPresenter::Stop()
{
    stopping_ = true;
    {
        std::lock_guard lock(mutex_);
        if (window_) window_->RequestClose();
    }
    if (thread_.joinable()) thread_.join();
}
}
