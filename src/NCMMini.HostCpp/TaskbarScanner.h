#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "TaskbarLayout.h"

#include <string>

namespace ncmmini
{
struct TaskbarSnapshot
{
    HWND window = nullptr;
    DWORD processId = 0;
    UINT dpi = 96;
    TaskbarRect bounds;
    std::vector<TaskbarRect> occupied;
    bool valid = false;
    std::wstring error;
};

// Call on a COM-initialized worker thread, never on the window thread.
TaskbarSnapshot ScanTaskbar(bool verbose = false);
}
