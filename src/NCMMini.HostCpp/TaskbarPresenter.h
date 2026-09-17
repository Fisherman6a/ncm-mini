#pragma once

#include "Win11TaskbarWindow.h"

namespace ncmmini
{
DWORD WindowsBuild();
bool HasModernTaskbar();

class TaskbarPresenter
{
public:
    ~TaskbarPresenter();
    bool Start(Win11TaskbarWindow::CommandHandler handler, bool visible);
    void Publish(const BandState& state);
    void Stop();

private:
    std::thread thread_;
    std::mutex mutex_;
    Win11TaskbarWindow* window_ = nullptr;
    std::atomic_bool stopping_{false};
};
}
