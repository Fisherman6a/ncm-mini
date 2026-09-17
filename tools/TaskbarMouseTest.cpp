#include "../src/NCMMini.HostCpp/Win11TaskbarWindow.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace ncmmini
{
namespace
{
constexpr UINT_PTR InputTimer = 2;

COLORREF ScreenPixel(POINT point)
{
    HDC screen = GetDC(nullptr);
    const auto color = GetPixel(screen, point.x, point.y);
    ReleaseDC(nullptr, screen);
    return color;
}

bool CaptureProbe(HWND window, const wchar_t* filename)
{
    RECT rect{};
    if (!GetWindowRect(window, &rect)) return false;
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    bool success = false;
    if (bitmap && pixels && memory)
    {
        const auto previous = SelectObject(memory, bitmap);
        if (BitBlt(memory, 0, 0, width, height, screen, rect.left, rect.top, SRCCOPY | CAPTUREBLT))
        {
            GdiFlush();
            BITMAPFILEHEADER header{};
            header.bfType = 0x4D42;
            header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
            header.bfSize = header.bfOffBits + width * height * 4;
            wchar_t executable[32768]{};
            GetModuleFileNameW(nullptr, executable, 32768);
            const auto path = std::filesystem::path(executable).parent_path() / filename;
            std::ofstream file(path, std::ios::binary);
            file.write(reinterpret_cast<const char*>(&header), sizeof(header));
            file.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
            file.write(static_cast<const char*>(pixels), width * height * 4);
            success = file.good();
            if (success) std::wcout << L"screenshot=" << path.wstring() << L'\n';
        }
        SelectObject(memory, previous);
    }
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
    if (screen) ReleaseDC(nullptr, screen);
    return success;
}
}

bool Win11TaskbarWindow::TestCheck(bool condition, const wchar_t* name)
{
    std::wcout << (condition ? L"PASS " : L"FAIL ") << name << std::endl;
    if (!condition)
    {
        KillTimer(controller_, InputTimer);
        PostQuitMessage(1);
    }
    return condition;
}

bool Win11TaskbarWindow::MoveTestCursor(POINT point)
{
    if (!TestCheck(SetCursorPos(point.x, point.y) != FALSE, L"move real cursor")) return false;
    cursorMoved_ = true;
    testCursor_ = point;
    return true;
}

bool Win11TaskbarWindow::SendMouseButton(bool down)
{
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    const bool sent = SendInput(1, &input, sizeof(input)) == 1;
    if (sent) testButtonDown_ = down;
    return TestCheck(sent, down ? L"send mouse down" : L"send mouse up");
}

void Win11TaskbarWindow::CheckInput()
{
    if (focusCheckAvailable_
        && !TestCheck(GetForegroundWindow() == foreground_, L"foreground matches startup witness")) return;
    RECT bounds{};
    if (!TestCheck(child_ && IsWindowVisible(child_) && GetWindowRect(child_, &bounds), L"test window visible")) return;
    POINT current{};
    if (cursorMoved_ && (!GetCursorPos(&current) || current.x != testCursor_.x || current.y != testCursor_.y))
    {
        TestCheck(false, L"cursor moved externally; cancel input test");
        return;
    }
    const auto layout = MakeTaskbarViewLayout(dpi_);
    const auto screen = [&](int x, int y) { return POINT{bounds.left + x, bounds.top + y}; };
    const auto padding = [&](const TaskbarRect& rect) {
        return screen(rect.left + MulDiv(3, dpi_, 96), (rect.top + rect.bottom) / 2);
    };
    const POINT outside{bounds.left + (bounds.right - bounds.left) / 2, bounds.top - MulDiv(24, dpi_, 96)};
    const auto sample = screen(MulDiv(42, dpi_, 96), MulDiv(20, dpi_, 96));
    const auto previousPadding = padding(layout.previous);
    const auto hover = [&](TaskbarHit hit) {
        return TestCheck(visual_.hovered && visual_.hot == hit && WindowFromPoint(testCursor_) == child_,
            L"real hover reaches full target including padding");
    };
    const auto pressed = [&](TaskbarHit hit) {
        return TestCheck(visual_.pressed == hit, L"mouse down reaches correct button");
    };
    switch (testStep_++)
    {
    case 0:
        if (!TestCheck(GetCursorPos(&oldCursor_) != FALSE, L"save cursor")) return;
        MoveTestCursor(outside);
        break;
    case 1:
        idlePixel_ = ScreenPixel(sample);
        if (!TestCheck(!visual_.hovered && idlePixel_ != CLR_INVALID, L"idle state before mouse entry")
            || !TestCheck((GetWindowLongPtrW(child_, GWL_STYLE) & WS_CHILD) && GetParent(child_) == taskbar_, L"embedded child parent")
            || !TestCheck(ScreenPixel({bounds.left, bounds.top}) == borderBackground_, L"transparent corner preserves taskbar")
            || !TestCheck(ScreenPixel(screen(MulDiv(20, dpi_, 96), MulDiv(20, dpi_, 96))) != idlePixel_, L"cover has visible screen pixels")
            || !TestCheck(CaptureProbe(child_, L"probe-idle.bmp"), L"capture idle")) return;
        MoveTestCursor(previousPadding);
        break;
    case 2:
        hoverButtonPixel_ = ScreenPixel(previousPadding);
        if (!hover(TaskbarHit::Previous)
            || !TestCheck(ScreenPixel(sample) != CLR_INVALID && ScreenPixel(sample) != idlePixel_, L"hover changes actual screen pixels")
            || !TestCheck(CaptureProbe(child_, L"probe-hover.bmp"), L"capture hover")) return;
        SendMouseButton(true);
        break;
    case 3:
        if (!pressed(TaskbarHit::Previous)
            || !TestCheck(ScreenPixel(previousPadding) != hoverButtonPixel_, L"pressed changes actual screen pixels")
            || !TestCheck(CaptureProbe(child_, L"probe-pressed.bmp"), L"capture pressed")) return;
        SendMouseButton(false);
        break;
    case 4:
        if (!TestCheck(buttonClicks_[0] == 1 && !visual_.playing, L"previous once without toggling play")) return;
        MoveTestCursor(padding(layout.play));
        break;
    case 5:
        if (hover(TaskbarHit::Play)) SendMouseButton(true);
        break;
    case 6:
        if (pressed(TaskbarHit::Play)) SendMouseButton(false);
        break;
    case 7:
        if (!TestCheck(buttonClicks_[1] == 1 && visual_.playing, L"play once toggles to pause")
            || !TestCheck(CaptureProbe(child_, L"probe-playing.bmp"), L"capture pause icon")) return;
        MoveTestCursor(padding(layout.next));
        break;
    case 8:
        if (hover(TaskbarHit::Next)) SendMouseButton(true);
        break;
    case 9:
        if (pressed(TaskbarHit::Next)) SendMouseButton(false);
        break;
    case 10:
        if (!TestCheck(buttonClicks_[2] == 1 && visual_.playing, L"next once without toggling play")) return;
        MoveTestCursor(padding(layout.play));
        break;
    case 11:
        if (hover(TaskbarHit::Play)) SendMouseButton(true);
        break;
    case 12:
        if (pressed(TaskbarHit::Play)) SendMouseButton(false);
        break;
    case 13:
        if (TestCheck(buttonClicks_[1] == 2 && !visual_.playing, L"pause toggles back to play")) SendMouseButton(true);
        break;
    case 14:
        if (pressed(TaskbarHit::Play)) MoveTestCursor(outside);
        break;
    case 15:
        SendMouseButton(false);
        break;
    case 16:
        if (!TestCheck(buttonClicks_[1] == 2 && !visual_.playing && visual_.pressed == TaskbarHit::None, L"drag out cancels click")) return;
        MoveTestCursor(screen((layout.cover.left + layout.cover.right) / 2, (layout.cover.top + layout.cover.bottom) / 2));
        break;
    case 17:
        if (hover(TaskbarHit::Cover)) SendMouseButton(true);
        break;
    case 18:
        SendMouseButton(false);
        break;
    case 19:
        if (!TestCheck(buttonClicks_ == std::array<unsigned int, 3>{1, 2, 1}, L"cover does not trigger playback controls")) return;
        MoveTestCursor(outside);
        break;
    case 20:
        if (!TestCheck(!visual_.hovered && visual_.hot == TaskbarHit::None && mouseLeaves_ > 0,
                L"real mouse leave clears hover")
            || !TestCheck(ScreenPixel(sample) == idlePixel_, L"screen background restored after leave")
            || !TestCheck(CaptureProbe(child_, L"probe-leave.bmp"), L"capture leave")) return;
        if (focusCheckAvailable_)
        {
            if (!TestCheck(GetForegroundWindow() == foreground_, L"foreground focus preserved")) return;
        }
        else std::wcout << L"SKIP foreground focus: no foreground witness\n";
        testPassed_ = true;
        std::wcout << L"self_test previous=" << buttonClicks_[0] << L" play=" << buttonClicks_[1]
            << L" next=" << buttonClicks_[2] << L" mouse_leaves=" << mouseLeaves_
            << L" focus=" << (focusCheckAvailable_ ? L"PASS" : L"SKIP") << L" result=PASS" << std::endl;
        KillTimer(controller_, InputTimer);
        PostQuitMessage(0);
        break;
    }
}
}
