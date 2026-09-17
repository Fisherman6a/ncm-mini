#include "../src/NCMMini.HostCpp/TaskbarBinding.h"

#include <iostream>

int main()
{
    using namespace ncmmini;
    int failures = 0;
    const auto check = [&](bool value, const char* name) {
        std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
        if (!value) ++failures;
    };
    const auto parse = [](std::initializer_list<const wchar_t*> args) {
        std::vector<wchar_t*> mutableArgs;
        for (const auto* arg : args) mutableArgs.push_back(const_cast<wchar_t*>(arg));
        return ParseOptions(static_cast<int>(mutableArgs.size()), mutableArgs.data());
    };
    check(parse({L"host", L"--taskbar", L"win11"}).taskbarMode == TaskbarMode::Win11, "explicit win11 mode");
    check(parse({L"host", L"--taskbar", L"deskband"}).taskbarMode == TaskbarMode::DeskBand, "explicit legacy mode");
    check(!parse({L"host", L"--taskbar", L"wrong"}).error.empty(), "invalid taskbar mode rejected");
    check(!parse({L"host", L"--taskbar"}).error.empty(), "missing taskbar mode rejected");
    check(parse({L"host", L"--duration", L"15"}).durationSeconds == 15, "bounded live run option");
    check(!parse({L"host", L"--duration", L"abc"}).error.empty(), "invalid duration rejected");
    check(!parse({L"host", L"--no-show-band"}).showBand, "legacy hidden flag preserved");
    check(SelectTaskbarMode(TaskbarMode::Auto, 26200, true) == TaskbarMode::Win11, "modern taskbar uses win11");
    check(SelectTaskbarMode(TaskbarMode::Auto, 19045, false) == TaskbarMode::DeskBand, "win10 keeps deskband");
    check(SelectTaskbarMode(TaskbarMode::Auto, 26200, false) == TaskbarMode::Win11, "explorer startup does not switch to legacy");
    check(SelectTaskbarMode(TaskbarMode::DeskBand, 26200, true) == TaskbarMode::DeskBand, "explicit legacy never silently changes");
    check(SelectTaskbarMode(TaskbarMode::Win11, 19045, false) == TaskbarMode::Win11, "explicit modern never silently changes");

    BandState state{true, L"Song", L"Artist", L"", {}};
    auto visual = PlayerVisual(state);
    check(visual.title == L"Song" && visual.detail == L"Artist", "real title and artist reach view");
    check(!visual.playbackKnown && visual.controlsEnabled, "unknown playback is not fabricated");
    state.playback = PlaybackState::Playing;
    visual = PlayerVisual(state);
    check(visual.playbackKnown && visual.playing, "playing state selects the pause icon");
    state.playback = PlaybackState::Paused;
    visual = PlayerVisual(state);
    check(visual.playbackKnown && !visual.playing, "paused state selects the play icon");
    state.playback = PlaybackState::Unknown;
    state.lyric = L"Current lyric";
    check(PlayerVisual(state).detail == state.lyric, "current lyric reaches detail row");
    state.running = false;
    visual = PlayerVisual(state);
    check(!visual.controlsEnabled && visual.title != L"Song" && visual.detail != state.lyric, "disconnect clears stale song and disables controls");
    state = {true, L"", L"", L"", {}};
    check(!PlayerVisual(state).title.empty(), "starting player has a readable title");

    TaskbarView view;
    const auto initial = view.Draw(96, {});
    std::vector<std::uint8_t> cover(40 * 40 * 4);
    for (std::size_t i = 0; i < cover.size(); i += 4)
    {
        cover[i] = 20; cover[i + 1] = 40; cover[i + 2] = 220; cover[i + 3] = 255;
    }
    check(view.SetCoverPixels(cover), "40x40 BGRA cover accepted");
    auto drawn = view.Draw(96, {});
    check(drawn != initial && ((drawn[20 * 360 + 20] >> 16) & 255) > 200, "BGRA red channel remains red");
    cover.assign(4, 255);
    check(!view.SetCoverPixels(cover) && view.Draw(96, {}) == initial, "invalid pixel size falls back to default");
    check(!view.SetCoverPixels({}) && view.Draw(96, {}) == initial, "absent cover restores default");
    check(failures == 0, "all binding assertions");
    return failures ? 1 : 0;
}
