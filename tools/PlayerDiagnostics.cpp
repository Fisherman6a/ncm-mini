#include "../src/NCMMini.HostCpp/Host.h"
#include "../src/NCMMini.HostCpp/Media.h"

#include <iostream>
#include <objbase.h>

int wmain(int argc, wchar_t** argv)
{
    const bool download = argc == 2 && std::wstring(argv[1]) == L"--download-cover";
    const bool playbackOnly = argc == 2 && std::wstring(argv[1]) == L"--playback-only";
    if (argc > 1 && !download && !playbackOnly) return 2;
    const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) return 1;
    ncmmini::AppOptions options;
    options.launchCloudMusic = false;
    options.closeCloudMusicOnExit = false;
    const auto snapshot = ncmmini::PlayerController(options).ReadSnapshot(true);
    const char* playback = snapshot.playback == ncmmini::PlaybackState::Playing ? "playing"
        : snapshot.playback == ncmmini::PlaybackState::Paused ? "paused" : "unknown";
    if (playbackOnly)
    {
        std::cout << playback << '\n';
        CoUninitialize();
        return 0;
    }
    ncmmini::TrackCatalog catalog;
    const auto track = catalog.Find(snapshot.windowTitle);
    const auto cover = ncmmini::LoadCover(track.coverUrl, download);
    std::cout << "running=" << snapshot.running << " pid=" << snapshot.processId << '\n'
        << "playback=" << playback << '\n'
        << "window_title=" << ncmmini::WideToUtf8(snapshot.windowTitle) << '\n'
        << "title=" << ncmmini::WideToUtf8(track.name.empty() ? snapshot.track.name : track.name) << '\n'
        << "artist=" << ncmmini::WideToUtf8(track.name.empty() ? snapshot.track.artist : track.artist) << '\n'
        << "catalog_match=" << !track.name.empty() << " cover_url=" << !track.coverUrl.empty()
        << " cover_bytes=" << cover.size() << " download_enabled=" << download << '\n';
    CoUninitialize();
    return snapshot.running && !snapshot.windowTitle.empty() ? 0 : 1;
}
