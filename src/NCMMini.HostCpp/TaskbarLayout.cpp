#include "TaskbarLayout.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace ncmmini
{
namespace
{
bool Contains(const TaskbarRect& rectangle, TaskbarPoint point)
{
    return rectangle.left <= point.x && point.x < rectangle.right
        && rectangle.top <= point.y && point.y < rectangle.bottom;
}
}

TaskbarHit HitTest(TaskbarPoint point, const TaskbarRect& cover,
    const TaskbarRect& previous, const TaskbarRect& play, const TaskbarRect& next,
    const TaskbarRect& text)
{
    if (Contains(cover, point)) return TaskbarHit::Cover;
    if (Contains(previous, point)) return TaskbarHit::Previous;
    if (Contains(play, point)) return TaskbarHit::Play;
    if (Contains(next, point)) return TaskbarHit::Next;
    if (Contains(text, point)) return TaskbarHit::Text;
    return TaskbarHit::None;
}

std::optional<TaskbarRect> FindTaskbarPlacement(const TaskbarRect& taskbar,
    const std::vector<TaskbarRect>& occupied, int width, int height, int margin)
{
    using Wide = std::int64_t;
    const Wide left = static_cast<Wide>(taskbar.left) + margin;
    const Wide right = static_cast<Wide>(taskbar.right) - margin;
    const Wide availableHeight = static_cast<Wide>(taskbar.bottom) - taskbar.top;
    if (width <= 0 || height <= 0 || margin < 0 || right - left < width
        || availableHeight < static_cast<Wide>(height) + 2LL * margin)
    {
        return std::nullopt;
    }
    std::vector<std::pair<Wide, Wide>> intervals;
    for (const auto& rectangle : occupied)
    {
        if (rectangle.right <= rectangle.left || rectangle.bottom <= rectangle.top)
            return std::nullopt;
        if (rectangle.bottom <= taskbar.top || rectangle.top >= taskbar.bottom
            || rectangle.right <= taskbar.left || rectangle.left >= taskbar.right)
            continue;
        intervals.emplace_back(std::max(left, static_cast<Wide>(rectangle.left) - margin),
            std::min(right, static_cast<Wide>(rectangle.right) + margin));
    }
    std::sort(intervals.begin(), intervals.end());
    Wide cursor = left;
    const auto placement = [&](Wide x) {
        const int y = static_cast<int>(taskbar.top + (availableHeight - height) / 2);
        return TaskbarRect{static_cast<int>(x), y, static_cast<int>(x + width), y + height};
    };
    for (const auto& interval : intervals)
    {
        if (interval.first - cursor >= width) return placement(cursor);
        cursor = std::max(cursor, interval.second);
    }
    if (right - cursor >= width) return placement(cursor);
    return std::nullopt;
}
}
