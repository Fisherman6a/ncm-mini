#pragma once

#include "Host.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace ncmmini
{
struct LyricLine
{
    std::chrono::milliseconds position{};
    std::wstring text;
};

// Known cache locations, including absent directories that may appear after player startup.
std::vector<std::filesystem::path> MediaCacheDirectories(
    const std::filesystem::path& localAppData = LocalAppDataPath());

class TrackCatalog
{
public:
    explicit TrackCatalog(std::vector<std::filesystem::path> dataDirectories = MediaCacheDirectories());

    TrackInfo Find(const std::wstring& title, bool forceReload = false);
    TrackInfo FindQueued(const std::wstring& title) const;

private:
    void EnsureLoaded(bool forceReload);

    std::vector<std::filesystem::path> dataDirectories_;
    std::chrono::steady_clock::time_point lastLoad_{};
    std::vector<TrackInfo> tracks_;
};

class LyricsStore
{
public:
    explicit LyricsStore(std::vector<std::filesystem::path> dataDirectories = MediaCacheDirectories());

    std::vector<LyricLine> Find(const TrackInfo& track) const;
    static std::vector<LyricLine> Parse(const std::string& text);
    static std::wstring Current(const std::vector<LyricLine>& lines, std::chrono::milliseconds elapsed);

private:
    std::vector<std::filesystem::path> dataDirectories_;
};

// Native cache is tried first. Disabling downloads still permits an offline cache hit.
std::vector<std::uint8_t> LoadCover(const std::wstring& url, bool allowDownload = true);
}
