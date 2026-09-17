#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include "TaskbarLayout.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ncmmini
{
constexpr int TaskbarViewWidth = 360;
constexpr int TaskbarViewHeight = 40;
inline constexpr wchar_t TaskbarTextFace[] = L"Microsoft YaHei UI";

struct TaskbarViewLayout
{
    TaskbarRect cover;
    TaskbarRect title;
    TaskbarRect detail;
    TaskbarRect previous;
    TaskbarRect play;
    TaskbarRect next;
    TaskbarHit Hit(int x, int y) const;
};

TaskbarViewLayout MakeTaskbarViewLayout(UINT dpi);

struct TaskbarVisualState
{
    bool light = true;
    bool hovered = false;
    bool playing = false;
    bool playbackKnown = true;
    bool controlsEnabled = true;
    TaskbarHit hot = TaskbarHit::None;
    TaskbarHit pressed = TaskbarHit::None;
    std::wstring title = L"NCM Mini";
    std::wstring detail = L"\u672a\u8fde\u63a5";
};

class TaskbarView
{
public:
    TaskbarView();
    ~TaskbarView();
    TaskbarView(const TaskbarView&) = delete;
    TaskbarView& operator=(const TaskbarView&) = delete;
    bool Ready() const { return token_ != 0 && cover_ != nullptr; }
    bool LoadCover(const std::wstring& path);
    bool SetCoverPixels(const std::vector<std::uint8_t>& bgra);
    std::vector<std::uint32_t> Draw(UINT dpi, const TaskbarVisualState& state) const;

private:
    ULONG_PTR token_ = 0;
    std::unique_ptr<Gdiplus::Bitmap> cover_;
    std::unique_ptr<Gdiplus::Bitmap> DefaultCover() const;
};
}
