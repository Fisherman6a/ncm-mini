#include "TaskbarBinding.h"

namespace ncmmini
{
TaskbarVisualState PlayerVisual(const BandState& state)
{
    TaskbarVisualState visual;
    visual.playbackKnown = state.running && state.playback != PlaybackState::Unknown;
    visual.playing = state.running && state.playback == PlaybackState::Playing;
    visual.controlsEnabled = state.running;
    if (!state.running)
    {
        visual.title = L"NCM Mini";
        visual.detail = L"\u7f51\u6613\u4e91\u97f3\u4e50\u672a\u8fde\u63a5";
    }
    else
    {
        visual.title = state.title.empty() ? L"\u7f51\u6613\u4e91\u97f3\u4e50" : state.title;
        visual.detail = !state.lyric.empty() ? state.lyric : state.artist;
        if (visual.detail.empty()) visual.detail = L"\u7b49\u5f85\u6b4c\u66f2\u4fe1\u606f";
    }
    return visual;
}

TaskbarMode SelectTaskbarMode(TaskbarMode requested, DWORD build, bool modernTaskbar)
{
    if (requested != TaskbarMode::Auto) return requested;
    return modernTaskbar || build >= 22000 ? TaskbarMode::Win11 : TaskbarMode::DeskBand;
}
}
