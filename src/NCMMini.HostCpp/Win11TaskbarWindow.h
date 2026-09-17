#pragma once

#include "TaskbarScanner.h"
#include "TaskbarView.h"
#include "Host.h"

#include <atomic>
#include <array>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <functional>

namespace ncmmini
{
constexpr wchar_t Win11ControllerClass[] = L"NCMMini.Win11.Controller";
constexpr UINT Win11StatusMessage = WM_APP + 74;
constexpr UINT Win11ShowMessage = WM_APP + 75;

class Win11TaskbarWindow
{
public:
    ~Win11TaskbarWindow();
    using CommandHandler = std::function<void(BandCommand)>;
    bool Start(const std::wstring& coverPath = {}, CommandHandler handler = {}, bool visible = true);
    int Run(unsigned int durationSeconds, bool selfTest, HWND foregroundWitness = nullptr);
    void Publish(const BandState& state);
    void RequestClose();
    void Stop();

private:
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void ScanLoop();
    void ApplySnapshot();
    bool Attach(const TaskbarSnapshot& snapshot);
    bool Render(int width, int height);
    void CheckInput();
    bool TestCheck(bool condition, const wchar_t* name);
    bool MoveTestCursor(POINT point);
    bool SendMouseButton(bool down);
    TaskbarHit HitAt(int x, int y) const;
    void UpdateHover(int x, int y);
    void Repaint();
    void UpdateTooltips();
    void ApplyPlayerState();
    CommandHandler commandHandler_;
    bool visibleRequested_ = true;
    std::mutex playerMutex_;
    BandState pendingState_;
    bool playerMessagePending_ = false;
    std::vector<std::uint8_t> displayedCover_;

    HWND controller_ = nullptr;
    HWND child_ = nullptr;
    HWND taskbar_ = nullptr;
    HWND tooltip_ = nullptr;
    DWORD taskbarProcessId_ = 0;
    UINT dpi_ = 96;
    TaskbarView view_;
    TaskbarVisualState visual_;
    bool selfTest_ = false;
    bool testStarted_ = false;
    bool testPassed_ = false;
    bool appeared_ = false;
    COLORREF borderBackground_ = CLR_INVALID;
    bool mouseTracking_ = false;
    unsigned int mouseLeaves_ = 0;
    std::array<unsigned int, 3> buttonClicks_{};
    bool cursorMoved_ = false;
    POINT oldCursor_{};
    POINT testCursor_{};
    HWND foreground_ = nullptr;
    bool focusCheckAvailable_ = false;
    bool testButtonDown_ = false;
    unsigned int testStep_ = 0;
    COLORREF idlePixel_ = CLR_INVALID;
    COLORREF hoverButtonPixel_ = CLR_INVALID;
    std::atomic_bool stopping_{false};
    std::atomic_bool scannerDone_{true};
    std::thread scanner_;
    std::mutex scanMutex_;
    std::condition_variable scanWake_;
    TaskbarSnapshot latest_;
    std::wstring lastStatus_;
};
}
