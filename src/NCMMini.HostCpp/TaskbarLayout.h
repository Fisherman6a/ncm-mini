#pragma once

#include <optional>
#include <vector>

namespace ncmmini
{
struct TaskbarRect
{
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

struct TaskbarPoint
{
    int x = 0;
    int y = 0;
};

enum class TaskbarHit
{
    None,
    Cover,
    Previous,
    Play,
    Next,
    Text
};

TaskbarHit HitTest(TaskbarPoint point, const TaskbarRect& cover,
    const TaskbarRect& previous, const TaskbarRect& play, const TaskbarRect& next,
    const TaskbarRect& text);

// Input and output use physical screen coordinates.
std::optional<TaskbarRect> FindTaskbarPlacement(const TaskbarRect& taskbar,
    const std::vector<TaskbarRect>& occupied, int width, int height, int margin);
}
