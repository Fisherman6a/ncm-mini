#include "Win11TaskbarWindow.h"
#include "TaskbarBinding.h"
#include "TaskbarDpi.h"

#include <windowsx.h>
#include <commctrl.h>

#include <chrono>
#include <cstring>
#include <iostream>

namespace ncmmini
{
namespace
{
constexpr wchar_t ControllerClass[] = L"NCMMini.TaskbarProbe.Controller";
constexpr wchar_t ChildClass[] = L"NCMMini.TaskbarProbe.Child";
constexpr UINT SnapshotMessage = WM_APP + 70;
constexpr UINT PlayerMessage = WM_APP + 71;
constexpr UINT_PTR LifetimeTimer = 1;
constexpr UINT_PTR InputTimer = 2;

bool IsButton(TaskbarHit hit)
{
    return hit == TaskbarHit::Previous || hit == TaskbarHit::Play || hit == TaskbarHit::Next;
}

bool IsLightTaskbar()
{
    DWORD value = 1;
    DWORD size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value != 0;
}
}

Win11TaskbarWindow::~Win11TaskbarWindow() { Stop(); }

bool Win11TaskbarWindow::Start(const std::wstring& coverPath, CommandHandler handler, bool visible)
{
    if (controller_) return true;
    if (!view_.Ready() || !TaskbarDpi::Supported()) return false;
    commandHandler_ = std::move(handler);
    visibleRequested_ = visible;
    if (commandHandler_) visual_ = PlayerVisual({});
    if (!coverPath.empty())
        std::wcout << L"cover=" << (view_.LoadCover(coverPath) ? L"loaded" : L"default (load failed)") << L'\n';
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&controls);
    const auto* controllerClass = commandHandler_ ? Win11ControllerClass : ControllerClass;
    for (const auto* name : {controllerClass, ChildClass})
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpfnWndProc = WindowProcedure;
        wc.lpszClassName = name;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    }
    controller_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, controllerClass,
        L"NCM Mini Taskbar Probe", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
        GetModuleHandleW(nullptr), this);
    if (!controller_) return false;
    stopping_ = false;
    scannerDone_ = false;
    scanner_ = std::thread(&Win11TaskbarWindow::ScanLoop, this);
    return true;
}

int Win11TaskbarWindow::Run(unsigned int durationSeconds, bool selfTest, HWND foregroundWitness)
{
    selfTest_ = selfTest;
    foreground_ = foregroundWitness;
    focusCheckAvailable_ = foregroundWitness != nullptr;
    if (durationSeconds && !SetTimer(controller_, LifetimeTimer, durationSeconds * 1000, nullptr)) return 1;
    MSG message{};
    BOOL result = 0;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    const bool passed = appeared_ && (!selfTest_ || testPassed_);
    Stop();
    return result < 0 || !passed ? 1 : 0;
}

void Win11TaskbarWindow::Stop()
{
    stopping_ = true;
    if (controller_)
    {
        KillTimer(controller_, InputTimer);
        KillTimer(controller_, LifetimeTimer);
    }
#ifdef NCM_TASKBAR_PROBE
    if (testButtonDown_) SendMouseButton(false);
#endif
    scanWake_.notify_all();
    // UIA can query our HWND synchronously; serve messages until COM proxies are released.
    while (scanner_.joinable() && !scannerDone_)
    {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT) continue;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (scanner_.joinable()) scanner_.join();
    if (cursorMoved_)
    {
        POINT current{};
        if (GetCursorPos(&current) && current.x == testCursor_.x && current.y == testCursor_.y)
            SetCursorPos(oldCursor_.x, oldCursor_.y);
        cursorMoved_ = false;
    }
    const HWND oldChild = child_;
    if (tooltip_ && IsWindow(tooltip_)) DestroyWindow(tooltip_);
    tooltip_ = nullptr;
    if (child_ && IsWindow(child_)) DestroyWindow(child_);
    child_ = nullptr;
    HWND oldController = nullptr;
    {
        std::lock_guard lock(playerMutex_);
        oldController = controller_;
        controller_ = nullptr;
    }
    if (oldController && IsWindow(oldController)) DestroyWindow(oldController);
    if (oldChild) std::wcout << L"cleanup child_destroyed=" << !IsWindow(oldChild) << L'\n';
}

void Win11TaskbarWindow::ScanLoop()
{
    // UIA returns physical bounds; GetWindowRect must use the same coordinate space.
    const auto previousDpi = TaskbarDpi::SetThread(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    while (!stopping_)
    {
        TaskbarSnapshot snapshot;
        if (SUCCEEDED(com)) snapshot = ScanTaskbar();
        else snapshot.error = L"Scanner COM initialization failed";
        {
            std::lock_guard lock(scanMutex_);
            latest_ = std::move(snapshot);
        }
        if (!stopping_) PostMessageW(controller_, SnapshotMessage, 0, 0);
        std::unique_lock lock(scanMutex_);
        scanWake_.wait_for(lock, std::chrono::seconds(1), [this] { return stopping_.load(); });
    }
    if (SUCCEEDED(com)) CoUninitialize();
    if (previousDpi) TaskbarDpi::SetThread(previousDpi);
    scannerDone_ = true;
}

void Win11TaskbarWindow::ApplySnapshot()
{
    if (stopping_) return;
    if (!visibleRequested_)
    {
        if (child_) ShowWindow(child_, SW_HIDE);
        return;
    }
    TaskbarSnapshot snapshot;
    {
        std::lock_guard lock(scanMutex_);
        snapshot = latest_;
    }
    std::wstring status = snapshot.error;
    const int width = MulDiv(TaskbarViewWidth, snapshot.dpi, 96);
    const int height = MulDiv(TaskbarViewHeight, snapshot.dpi, 96);
    const auto placement = snapshot.valid ? FindTaskbarPlacement(snapshot.bounds,
        snapshot.occupied, width, height, MulDiv(4, snapshot.dpi, 96)) : std::nullopt;
    if (!placement)
    {
        if (snapshot.valid) status = L"Waiting for enough taskbar space";
        if (child_) ShowWindow(child_, SW_HIDE);
    }
    else if (Attach(snapshot))
    {
        const bool dpiChanged = dpi_ != snapshot.dpi;
        dpi_ = snapshot.dpi;
        if (dpiChanged) UpdateTooltips();
        if (selfTest_ && !testStarted_)
        {
            HDC screen = GetDC(nullptr);
            borderBackground_ = GetPixel(screen, placement->left, placement->top);
            ReleaseDC(nullptr, screen);
        }
        POINT origin{placement->left, placement->top};
        SetLastError(ERROR_SUCCESS);
        const int mapped = MapWindowPoints(nullptr, taskbar_, &origin, 1);
        if ((mapped == 0 && GetLastError() != ERROR_SUCCESS)
            || !SetWindowPos(child_, HWND_TOP, origin.x, origin.y, width, height,
                SWP_NOACTIVATE | SWP_NOOWNERZORDER)
            || !Render(width, height))
        {
            status = L"Window placement/render failed: " + std::to_wstring(GetLastError());
            ShowWindow(child_, SW_HIDE);
        }
        else
        {
            ShowWindow(child_, SW_SHOWNOACTIVATE);
            appeared_ = true;
            status = L"Embedded at " + std::to_wstring(placement->left) + L"," + std::to_wstring(placement->top)
                + L" size=" + std::to_wstring(width) + L"x" + std::to_wstring(height)
                + L" dpi=" + std::to_wstring(dpi_);
#ifdef NCM_TASKBAR_PROBE
            if (selfTest_ && !testStarted_)
            {
                testStarted_ = true;
                SetTimer(controller_, InputTimer, 350, nullptr);
            }
#endif
        }
    }
    else status = L"Taskbar attachment failed: " + std::to_wstring(GetLastError());
    if (status != lastStatus_)
    {
        std::wcout << status << std::endl;
#ifndef NCM_TASKBAR_PROBE
        if (commandHandler_) Log(L"Win11 taskbar: " + status);
#endif
        lastStatus_ = std::move(status);
    }
}

bool Win11TaskbarWindow::Attach(const TaskbarSnapshot& snapshot)
{
    DWORD processId = 0;
    GetWindowThreadProcessId(snapshot.window, &processId);
    RECT bounds{};
    if (processId != snapshot.processId || !GetWindowRect(snapshot.window, &bounds)
        || bounds.left != snapshot.bounds.left || bounds.top != snapshot.bounds.top
        || bounds.right != snapshot.bounds.right || bounds.bottom != snapshot.bounds.bottom)
    {
        if (child_) ShowWindow(child_, SW_HIDE);
        return false;
    }
    if (child_ && IsWindow(child_) && taskbar_ == snapshot.window
        && taskbarProcessId_ == processId && GetParent(child_) == taskbar_) return true;
    if (tooltip_ && IsWindow(tooltip_)) DestroyWindow(tooltip_);
    tooltip_ = nullptr;
    if (child_ && IsWindow(child_)) DestroyWindow(child_);
    child_ = nullptr;
    const auto previousDpi = TaskbarDpi::SetThread(TaskbarDpi::WindowContext(snapshot.window));
    HWND child = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        ChildClass, L"NCM Mini Probe", WS_POPUP, 0, 0, TaskbarViewWidth, TaskbarViewHeight,
        nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (!child)
    {
        if (previousDpi) TaskbarDpi::SetThread(previousDpi);
        return false;
    }
    SetLastError(ERROR_SUCCESS);
    const auto oldStyle = SetWindowLongPtrW(child, GWL_STYLE, WS_CHILD | WS_CLIPSIBLINGS);
    const bool styleOk = oldStyle != 0 || GetLastError() == ERROR_SUCCESS;
    SetLastError(ERROR_SUCCESS);
    const HWND previousParent = styleOk ? SetParent(child, snapshot.window) : nullptr;
    const DWORD parentError = GetLastError();
    const bool attached = styleOk && (previousParent || parentError == ERROR_SUCCESS)
        && GetParent(child) == snapshot.window;
    if (previousDpi) TaskbarDpi::SetThread(previousDpi);
    if (!attached)
    {
        DestroyWindow(child);
        SetLastError(parentError);
        return false;
    }
    child_ = child;
    taskbar_ = snapshot.window;
    taskbarProcessId_ = processId;
    visual_.hot = visual_.pressed = TaskbarHit::None;
    visual_.hovered = mouseTracking_ = false;
    SetWindowPos(child_, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    tooltip_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE, TOOLTIPS_CLASSW,
        nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, child_, nullptr, GetModuleHandleW(nullptr), nullptr);
    UpdateTooltips();
    if (commandHandler_) SetWindowTextW(child_, (visual_.title + L" - " + visual_.detail).c_str());
    std::wcout << L"attached hwnd=" << child_ << L" parent=" << taskbar_
        << L" verified=" << (GetParent(child_) == taskbar_) << L'\n';
    return true;
}

void Win11TaskbarWindow::UpdateTooltips()
{
    if (!tooltip_) return;
    const auto layout = MakeTaskbarViewLayout(dpi_);
    const std::array<TaskbarRect, 3> rects{layout.previous, layout.play, layout.next};
    const wchar_t* playText = !visual_.playbackKnown ? L"\u64ad\u653e/\u6682\u505c"
        : visual_.playing ? L"\u6682\u505c" : L"\u64ad\u653e";
    const wchar_t* texts[]{L"\u4e0a\u4e00\u9996", playText, L"\u4e0b\u4e00\u9996"};
    for (std::size_t index = 0; index < rects.size(); ++index)
    {
        TOOLINFOW info{};
        // The unmanifested legacy host can load common-controls v5, which rejects
        // the v6-only trailing field in sizeof(TOOLINFO).
        info.cbSize = TTTOOLINFOW_V2_SIZE;
        info.hwnd = child_;
        info.uId = index + 1;
        SendMessageW(tooltip_, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&info));
        info.uFlags = TTF_SUBCLASS;
        info.rect = {rects[index].left, rects[index].top, rects[index].right, rects[index].bottom};
        info.lpszText = const_cast<wchar_t*>(texts[index]);
        SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
    }
}

bool Win11TaskbarWindow::Render(int width, int height)
{
    visual_.light = IsLightTaskbar();
    const auto pixels = view_.Draw(dpi_, visual_);
    if (pixels.size() != static_cast<std::size_t>(width) * height) return false;
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* data = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &data, nullptr, 0);
    bool success = false;
    if (screen && memory && bitmap && data)
    {
        std::memcpy(data, pixels.data(), pixels.size() * sizeof(pixels[0]));
        const auto oldBitmap = SelectObject(memory, bitmap);
        SIZE size{width, height};
        POINT origin{};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        success = UpdateLayeredWindow(child_, screen, nullptr, &size, memory,
            &origin, 0, &blend, ULW_ALPHA) != FALSE;
        SelectObject(memory, oldBitmap);
    }
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
    if (screen) ReleaseDC(nullptr, screen);
    return success;
}

void Win11TaskbarWindow::Repaint()
{
    RECT client{};
    if (child_ && GetClientRect(child_, &client)) Render(client.right, client.bottom);
}

void Win11TaskbarWindow::Publish(const BandState& state)
{
    std::lock_guard lock(playerMutex_);
    pendingState_ = state;
    if (!playerMessagePending_ && controller_ && !stopping_)
        playerMessagePending_ = PostMessageW(controller_, PlayerMessage, 0, 0) != FALSE;
}

void Win11TaskbarWindow::ApplyPlayerState()
{
    BandState state;
    {
        std::lock_guard lock(playerMutex_);
        state = pendingState_;
        playerMessagePending_ = false;
    }
    const auto next = PlayerVisual(state);
    const bool playbackChanged = visual_.playing != next.playing || visual_.playbackKnown != next.playbackKnown;
    visual_.title = next.title;
    visual_.detail = next.detail;
    visual_.controlsEnabled = next.controlsEnabled;
    visual_.playbackKnown = next.playbackKnown;
    visual_.playing = next.playing;
    if (playbackChanged) UpdateTooltips();
    if (!state.running) state.cover.clear();
    if (state.cover != displayedCover_)
    {
        view_.SetCoverPixels(state.cover);
        displayedCover_ = std::move(state.cover);
    }
    if (!state.running)
    {
        visual_.pressed = TaskbarHit::None;
        if (GetCapture() == child_) ReleaseCapture();
    }
    if (child_) SetWindowTextW(child_, (visual_.title + L" - " + visual_.detail).c_str());
    Repaint();
}

void Win11TaskbarWindow::RequestClose()
{
    std::lock_guard lock(playerMutex_);
    if (controller_) PostMessageW(controller_, WM_CLOSE, 0, 0);
}

TaskbarHit Win11TaskbarWindow::HitAt(int x, int y) const { return MakeTaskbarViewLayout(dpi_).Hit(x, y); }

void Win11TaskbarWindow::UpdateHover(int x, int y)
{
    const auto next = HitAt(x, y);
    RECT rect{};
    GetClientRect(child_, &rect);
    const POINT point{x, y};
    const bool inside = PtInRect(&rect, point);
    if (next == visual_.hot && inside == visual_.hovered) return;
    visual_.hot = next;
    visual_.hovered = inside;
    Repaint();
}

LRESULT CALLBACK Win11TaskbarWindow::WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* self = reinterpret_cast<Win11TaskbarWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        self = static_cast<Win11TaskbarWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wParam, lParam);
    switch (message)
    {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: ValidateRect(window, nullptr); return 0;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT)
        {
            SetCursor(LoadCursorW(nullptr, self->visual_.controlsEnabled && IsButton(self->visual_.hot) ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE:
        self->UpdateHover(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (!self->mouseTracking_)
        {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
            self->mouseTracking_ = TrackMouseEvent(&tracking) != FALSE;
        }
        return 0;
    case WM_MOUSELEAVE:
        self->mouseTracking_ = false;
        self->visual_.hot = TaskbarHit::None;
        self->visual_.hovered = false;
        ++self->mouseLeaves_;
        self->Repaint();
        return 0;
    case WM_LBUTTONDOWN:
        self->visual_.pressed = self->HitAt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (self->visual_.controlsEnabled && IsButton(self->visual_.pressed)) SetCapture(window);
        else self->visual_.pressed = TaskbarHit::None;
        self->Repaint();
        return 0;
    case WM_LBUTTONUP:
        {
            const auto hit = self->HitAt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            const bool activate = self->visual_.controlsEnabled && IsButton(hit) && self->visual_.pressed == hit;
            self->visual_.pressed = TaskbarHit::None;
            if (GetCapture() == window) ReleaseCapture();
            if (activate)
            {
                if (self->commandHandler_)
                {
                    const auto command = hit == TaskbarHit::Previous ? BandCommand::Previous
                        : hit == TaskbarHit::Next ? BandCommand::Next : BandCommand::PlayPause;
                    self->commandHandler_(command);
                }
                else
                {
                if (hit == TaskbarHit::Previous) ++self->buttonClicks_[0];
                else if (hit == TaskbarHit::Next) ++self->buttonClicks_[2];
                else
                {
                    ++self->buttonClicks_[1];
                    self->visual_.playing = !self->visual_.playing;
                    self->UpdateTooltips();
                }
                std::wcout << L"probe_button=" << static_cast<int>(hit) << L" playing=" << self->visual_.playing << std::endl;
                }
            }
            self->Repaint();
        }
        return 0;
    case WM_CANCELMODE:
        if (GetCapture() == window) ReleaseCapture();
        [[fallthrough]];
    case WM_CAPTURECHANGED:
        self->visual_.pressed = TaskbarHit::None;
        self->Repaint();
        return 0;
    case WM_RBUTTONUP:
        if (!self->commandHandler_) PostQuitMessage(0);
        else
        {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, L"\u8bbe\u7f6e");
            AppendMenuW(menu, MF_STRING, 2, L"\u9000\u51fa NCM Mini");
            POINT point{};
            GetCursorPos(&point);
            SetForegroundWindow(self->controller_);
            const auto selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                point.x, point.y, 0, self->controller_, nullptr);
            DestroyMenu(menu);
            if (selected) self->commandHandler_(selected == 1 ? BandCommand::Options : BandCommand::Exit);
            if (selected == 2) PostQuitMessage(0);
            PostMessageW(self->controller_, WM_NULL, 0, 0);
        }
        return 0;
    case SnapshotMessage: self->ApplySnapshot(); return 0;
    case PlayerMessage: self->ApplyPlayerState(); return 0;
    case Win11StatusMessage:
        return self->child_ && IsWindowVisible(self->child_) ? 1 : 2;
    case Win11ShowMessage:
        self->visibleRequested_ = true;
        self->ApplySnapshot();
        return 1;
    case WM_TIMER:
        if (wParam == LifetimeTimer) PostQuitMessage(0);
#ifdef NCM_TASKBAR_PROBE
        else if (wParam == InputTimer && !self->stopping_) self->CheckInput();
#endif
        return 0;
    case WM_CLOSE: PostQuitMessage(0); return 0;
    case WM_NCDESTROY:
        if (window == self->child_) self->child_ = nullptr;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}
