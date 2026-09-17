#include "../src/NCMMini.HostCpp/TaskbarLayout.h"

#include <iostream>
#include <string>

using ncmmini::TaskbarRect;
using ncmmini::TaskbarHit;

int main()
{
    const TaskbarRect cover{8, 4, 48, 44};
    const TaskbarRect previous{60, 4, 100, 44};
    const TaskbarRect play{104, 4, 144, 44};
    const TaskbarRect next{148, 4, 188, 44};
    const TaskbarRect text{196, 4, 300, 44};
    struct HitCase { const char* name; int x; int y; TaskbarHit expected; };
    const std::vector<HitCase> hitCases = {
        {"cover hit", 20, 20, TaskbarHit::Cover},
        {"previous hit", 80, 20, TaskbarHit::Previous},
        {"play hit", 120, 20, TaskbarHit::Play},
        {"next hit", 160, 20, TaskbarHit::Next},
        {"text hit", 220, 20, TaskbarHit::Text},
        {"outside hit", 2, 20, TaskbarHit::None},
    };
    int failures = 0;
    for (const auto& test : hitCases)
    {
        const auto actual = ncmmini::HitTest({test.x, test.y}, cover, previous, play, next, text);
        const bool equal = actual == test.expected;
        std::cout << (equal ? "PASS " : "FAIL ") << test.name << '\n';
        if (!equal) ++failures;
    }
    struct Case
    {
        const char* name;
        TaskbarRect taskbar;
        std::vector<TaskbarRect> occupied;
        int width;
        int height;
        int margin;
        std::optional<TaskbarRect> expected;
    };
    const std::vector<Case> cases = {
        {"centered: uses space after widgets", {0, 950, 1000, 1000},
            {{0, 950, 120, 1000}, {400, 950, 600, 1000}, {850, 950, 1000, 1000}},
            160, 32, 8, TaskbarRect{128, 959, 288, 991}},
        {"left aligned: uses gap before tray", {0, 950, 1000, 1000},
            {{0, 950, 600, 1000}, {850, 950, 1000, 1000}},
            160, 32, 8, TaskbarRect{608, 959, 768, 991}},
        {"overlapping unsorted obstacles are merged", {0, 0, 1000, 50},
            {{350, 0, 600, 50}, {0, 0, 400, 50}, {850, 0, 1000, 50}},
            160, 32, 8, TaskbarRect{608, 9, 768, 41}},
        {"full taskbar hides instead of overlapping", {0, 0, 1000, 50},
            {{0, 0, 800, 50}, {900, 0, 1000, 50}}, 160, 32, 8, std::nullopt},
        {"negative monitor origin", {-1920, 1030, 0, 1080},
            {{-1920, 1030, -1800, 1080}, {-1300, 1030, -900, 1080}, {-200, 1030, 0, 1080}},
            160, 32, 8, TaskbarRect{-1792, 1039, -1632, 1071}},
        {"outside rectangles do not consume taskbar", {0, 100, 1000, 150},
            {{0, 0, 1000, 50}, {400, 100, 600, 150}},
            160, 32, 8, TaskbarRect{8, 109, 168, 141}},
        {"exact fit includes margins", {0, 0, 176, 48}, {},
            160, 32, 8, TaskbarRect{8, 8, 168, 40}},
        {"one pixel short hides", {0, 0, 175, 48}, {}, 160, 32, 8, std::nullopt},
        {"height overflow hides", {0, 0, 1000, 40}, {}, 160, 32, 8, std::nullopt},
        {"invalid taskbar hides", {100, 0, 0, 50}, {}, 160, 32, 8, std::nullopt},
        {"zero width hides", {0, 0, 1000, 50}, {}, 0, 32, 8, std::nullopt},
        {"invalid occupied rectangle fails closed", {0, 0, 1000, 50},
            {{500, 0, 400, 50}}, 160, 32, 8, std::nullopt},
        {"150 percent dimensions", {0, 0, 1500, 75},
            {{0, 0, 180, 75}, {600, 0, 900, 75}, {1275, 0, 1500, 75}},
            240, 48, 12, TaskbarRect{192, 13, 432, 61}}
    };
    for (const auto& test : cases)
    {
        const auto actual = ncmmini::FindTaskbarPlacement(test.taskbar, test.occupied,
            test.width, test.height, test.margin);
        const bool equal = actual.has_value() == test.expected.has_value()
            && (!actual || (actual->left == test.expected->left && actual->top == test.expected->top
                && actual->right == test.expected->right && actual->bottom == test.expected->bottom));
        std::cout << (equal ? "PASS " : "FAIL ") << test.name << '\n';
        if (!equal) ++failures;
    }
    return failures == 0 ? 0 : 1;
}
