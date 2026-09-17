#include "../src/NCMMini.HostCpp/TaskbarPresenter.h"

#include <array>
#include <chrono>
#include <iostream>
#include <commctrl.h>

namespace
{
HWND FindControl()
{
    HWND found = nullptr;
    EnumChildWindows(FindWindowW(L"Shell_TrayWnd", nullptr), [](HWND window, LPARAM data) -> BOOL {
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        wchar_t name[128]{};
        GetClassNameW(window, name, 128);
        if (pid == GetCurrentProcessId() && std::wstring(name) == L"NCMMini.TaskbarProbe.Child")
        {
            *reinterpret_cast<HWND*>(data) = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&found));
    return found;
}

std::wstring Title(HWND window)
{
    wchar_t title[1024]{};
    GetWindowTextW(window, title, 1024);
    return title;
}

std::wstring PlayHint(HWND child)
{
    HWND tooltip = nullptr;
    struct Search { HWND child; HWND* tooltip; } search{child, &tooltip};
    EnumThreadWindows(GetWindowThreadProcessId(child, nullptr), [](HWND window, LPARAM data) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(data);
        wchar_t cls[80]{};
        GetClassNameW(window, cls, 80);
        // Win32 promotes a child owner to its top-level parent. Restrict by the
        // presenter's own window thread, which creates exactly one tooltip.
        if (std::wstring(cls) == TOOLTIPS_CLASSW)
        {
            *search.tooltip = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    wchar_t text[256]{};
    TOOLINFOW info{};
    info.cbSize = TTTOOLINFOW_V2_SIZE; info.hwnd = child; info.uId = 2; info.lpszText = text;
    DWORD_PTR result = 0;
    if (tooltip) SendMessageTimeoutW(tooltip, TTM_GETTEXTW, 256, reinterpret_cast<LPARAM>(&info),
        SMTO_ABORTIFHUNG, 1000, &result);
    static unsigned probes = 0;
    if (probes++ < 3) std::cout << "tooltip=" << tooltip << " text=" << ncmmini::WideToUtf8(text) << '\n';
    return text;
}

std::vector<COLORREF> PlayPixels(HWND child, const ncmmini::TaskbarRect& play)
{
    RECT bounds{}; GetWindowRect(child, &bounds);
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    const int width = play.right - play.left, height = play.bottom - play.top;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* data = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &data, nullptr, 0);
    std::vector<COLORREF> pixels;
    if (memory && bitmap && data)
    {
        const auto previous = SelectObject(memory, bitmap);
        if (BitBlt(memory, 0, 0, width, height, screen, bounds.left + play.left,
            bounds.top + play.top, SRCCOPY | CAPTUREBLT))
        {
            GdiFlush();
            const auto* colors = static_cast<COLORREF*>(data);
            pixels.assign(colors, colors + width * height);
        }
        SelectObject(memory, previous);
    }
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return pixels;
}

template <typename Predicate>
bool Wait(Predicate condition)
{
    for (int count = 0; count < 100; ++count)
    {
        if (condition()) return true;
        Sleep(50);
    }
    return false;
}

bool Click(HWND window, const ncmmini::TaskbarRect& area, POINT& last)
{
    RECT rect{};
    if (!GetWindowRect(window, &rect)) return false;
    last = {rect.left + area.left + 4, rect.top + (area.top + area.bottom) / 2};
    if (!SetCursorPos(last.x, last.y)) return false;
    Sleep(180);
    if (WindowFromPoint(last) != window) return false;
    INPUT down{};
    down.type = INPUT_MOUSE;
    down.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    if (SendInput(1, &down, sizeof(down)) != 1) return false;
    Sleep(80);
    down.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    return SendInput(1, &down, sizeof(down)) == 1;
}
}

int main()
{
    using namespace ncmmini;
    // The legacy host is DPI-unaware; each new taskbar thread must select its own context.
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::array<std::atomic_uint, 3> commands{};
    int failures = 0;
    const auto check = [&](bool value, const char* name) {
        std::cout << (value ? "PASS " : "FAIL ") << name << std::endl;
        if (!value) ++failures;
        return value;
    };
    TaskbarPresenter presenter;
    if (!check(presenter.Start([&](BandCommand command) {
        if (command >= BandCommand::Previous && command <= BandCommand::Next)
            ++commands[static_cast<int>(command) - 1];
        std::cout << "callback=" << static_cast<int>(command) << std::endl;
    }, true), "start production presenter")) return 1;
    BandState state{true, L"Transport fixture", L"Artist", L"", std::vector<std::uint8_t>(40 * 40 * 4)};
    for (std::size_t i = 0; i < state.cover.size(); i += 4)
    {
        state.cover[i] = 20; state.cover[i + 1] = 40; state.cover[i + 2] = 220; state.cover[i + 3] = 255;
    }
    presenter.Publish(state);
    HWND child = nullptr;
    if (!check(Wait([&] { child = FindControl(); return child && IsWindowVisible(child)
        && Title(child) == L"Transport fixture - Artist"; }), "state crosses threads into visible window")) return 1;
    for (int i = 0; i < 200; ++i)
    {
        state.title = L"Revision " + std::to_wstring(i);
        presenter.Publish(state);
    }
    check(Wait([&] { return Title(child) == L"Revision 199 - Artist"; }), "latest state wins during burst updates");
    const auto layout = MakeTaskbarViewLayout(GetDpiForWindow(child));
    state.playback = PlaybackState::Paused;
    presenter.Publish(state);
    check(Wait([&] { return PlayHint(child) == L"\u64ad\u653e"; }), "paused state updates the native play tooltip");
    const auto pausedPixels = PlayPixels(child, layout.play);
    check(!pausedPixels.empty(), "read the real play button pixels");
    state.playback = PlaybackState::Playing;
    presenter.Publish(state);
    check(Wait([&] { return PlayHint(child) == L"\u6682\u505c"; }), "external playback update changes the native tooltip");
    check(Wait([&] { return PlayPixels(child, layout.play) != pausedPixels; }), "playing state changes the actual screen icon without a mouse click");
    Sleep(200);
    RECT bounds{};
    GetWindowRect(child, &bounds);
    const auto coverPoint = POINT{bounds.left + (layout.cover.left + layout.cover.right) / 2,
        bounds.top + (layout.cover.top + layout.cover.bottom) / 2};
    HDC screen = GetDC(nullptr);
    const auto coverColor = GetPixel(screen, coverPoint.x, coverPoint.y);
    ReleaseDC(nullptr, screen);
    check(GetRValue(coverColor) > 200 && GetBValue(coverColor) < 50, "binary cover reaches actual screen with correct channels");
    POINT original{}, last{};
    const bool savedCursor = GetCursorPos(&original) != FALSE;
    bool moved = false;
    if (savedCursor)
    {
        for (const auto& area : {layout.previous, layout.play, layout.next})
        {
            moved = true;
            if (!check(Click(child, area, last), "real mouse sends production button command")) break;
        }
        check(Wait([&] { return commands[0] == 1 && commands[1] == 1 && commands[2] == 1; }),
            "previous play next callbacks each arrive exactly once");
        std::cout << "counts=" << commands[0] << ',' << commands[1] << ',' << commands[2] << std::endl;
    }
    else check(false, "save cursor");
    presenter.Publish({});
    check(Wait([&] { return Title(child).find(L"NCM Mini - ") == 0; }), "disconnect removes previous song");
    if (savedCursor)
    {
        moved = true;
        check(Click(child, layout.play, last), "click disconnected control");
        Sleep(200);
        check(commands[1] == 1, "disconnected control sends no player command");
    }
    presenter.Stop();
    check(!IsWindow(child), "presenter stop destroys child");
    POINT current{};
    if (moved && GetCursorPos(&current) && current.x == last.x && current.y == last.y)
        SetCursorPos(original.x, original.y);
    presenter.Stop();
    check(true, "repeated stop is harmless");
    return failures ? 1 : 0;
}
