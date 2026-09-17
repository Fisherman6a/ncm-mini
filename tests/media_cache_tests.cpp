#include "../src/NCMMini.HostCpp/Media.h"
#include "../src/NCMMini.HostCpp/Json.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using ncmmini::LyricsStore;
using ncmmini::TrackCatalog;

namespace
{
int failures = 0;
int checks = 0;

void Check(bool condition, const char* name)
{
    ++checks;
    std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
    if (!condition) ++failures;
}

void Write(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) throw std::runtime_error("Could not write fixture");
}

fs::path NormalCache(const fs::path& root)
{
    return root / L"NetEase/CloudMusic/webdata/file";
}

fs::path PackagedCache(const fs::path& root)
{
    return root / L"Packages/1F8B0F94.122165AE053F_j2p0p5q0044a6/LocalCache/Local/NetEase/CloudMusic/webdata/file";
}

// Only this test process sees the fixture root; the player's environment is untouched.
class LocalDataScope
{
public:
    explicit LocalDataScope(const fs::path& root) : previous_(ncmmini::LocalAppDataPath())
    {
        if (!SetEnvironmentVariableW(L"LOCALAPPDATA", root.c_str()))
            throw std::runtime_error("Could not set fixture environment");
    }
    ~LocalDataScope()
    {
        SetEnvironmentVariableW(L"LOCALAPPDATA", previous_.empty() ? nullptr : previous_.c_str());
    }
private:
    std::wstring previous_;
};

const std::string Queue = R"({"queue":[
    {"id":101,"name":"Shared Song","artists":[{"name":"Other Artist"}],"album":{"picUrl":"https://example.test/other.jpg"}},
    {"id":202,"name":"Shared Song","artists":[{"name":"Right Artist"}],"lrcid":"lyric202","album":{"picUrl":"https://example.test/right.jpg"}},
    {"id":303,"name":"Duet","ar":[{"name":"First Artist"},{"name":"Second Artist"}],"al":{"picUrl":"https://example.test/duet.jpg"}},
    {"id":404,"name":"Solo","artist":"Annabelle"},
    {"id":405,"name":"Exact","artists":[{"name":"Ann"},{"name":"Guest"}]},
    {"id":406,"name":"Exact","artist":"Ann"}
]})";

const std::string PlayingList = R"({"list":[{"displayOrder":0,"track":{
    "id":505,"name":"Shared Song","artists":[{"name":"Right Artist"}],
    "album":{"picUrl":"https://example.test/packaged.jpg"}
}}]})";

void TestNormalAndMatching(const fs::path& root)
{
    LocalDataScope environment(root);
    Write(NormalCache(root) / L"queue", Queue);
    TrackCatalog catalog;
    auto track = catalog.FindQueued(L"Shared Song - Right Artist");
    Check(track.trackId == L"202" && track.lyricsId == L"lyric202"
        && track.coverUrl == L"https://example.test/right.jpg", "normal queue retains metadata");
    Check(catalog.Find(L"  SHARED   song - right ARTIST  ").trackId == L"202",
        "title and artist normalize case and whitespace");
    Check(catalog.FindQueued(L"Shared Song - Missing Artist").name.empty(),
        "queue rejects same title with different artist");
    Check(catalog.Find(L"Shared Song - Missing Artist", true).name.empty(),
        "catalog fallback rejects same title with different artist");
    Check(catalog.FindQueued(L"Solo - Ann").name.empty(), "artist substrings do not match");
    Check(catalog.FindQueued(L"Exact - Ann").trackId == L"406", "exact artist outranks duet membership");
    Check(catalog.FindQueued(L"Duet - Second Artist").trackId == L"303", "individual duet artist matches");
    Check(catalog.FindQueued(L"Duet - First Artist/Second Artist").trackId == L"303", "full duet artist matches");
    Check(!catalog.FindQueued(L"Shared Song").name.empty(), "title-only lookup remains supported");
    Check(catalog.Find(L"  ").name.empty(), "empty title has no match");
    Check(catalog.Find(L"Unknown - Artist").name.empty(), "unknown title has no match");
}

void TestPackaged(const fs::path& root)
{
    LocalDataScope environment(root);
    Write(PackagedCache(root) / L"playingList", PlayingList);
    Write(PackagedCache(root) / L"lyric505", "[00:01.00]Packaged lyric\n");
    TrackCatalog catalog;
    Check(catalog.FindQueued(L"Shared Song - Right Artist").trackId == L"505",
        "packaged playingList is available through queued lookup");
    const auto track = catalog.Find(L"Shared Song - Right Artist");
    Check(track.trackId == L"505" && track.coverUrl == L"https://example.test/packaged.jpg",
        "packaged nested track supplies metadata");
    ncmmini::TrackInfo lyricTrack;
    lyricTrack.trackId = L"505";
    const auto lines = LyricsStore().Find(lyricTrack);
    Check(lines.size() == 1 && lines[0].text == L"Packaged lyric", "lyrics use packaged cache");
}

void TestSelection(const fs::path& root)
{
    LocalDataScope environment(root);
    const auto normal = NormalCache(root) / L"queue";
    const auto packaged = PackagedCache(root) / L"playingList";
    Write(normal, Queue);
    Write(packaged, PlayingList);
    const auto now = fs::file_time_type::clock::now();
    fs::last_write_time(normal, now - std::chrono::hours(2));
    fs::last_write_time(packaged, now - std::chrono::hours(1));
    TrackCatalog catalog;
    Check(catalog.FindQueued(L"Shared Song - Right Artist").trackId == L"505", "newer packaged queue wins");
    fs::last_write_time(normal, now);
    Check(catalog.FindQueued(L"Shared Song - Right Artist").trackId == L"202", "queue priority updates without reconstruction");
    Write(normal, R"([{"id":1,"name":"Shared Song","artist":"Wrong Artist"}])");
    Check(catalog.FindQueued(L"Shared Song - Right Artist").trackId == L"505", "artist mismatch falls through to other cache");
    Write(normal, "{broken json");
    Check(catalog.FindQueued(L"Shared Song - Right Artist").trackId == L"505", "malformed queue falls through to other cache");
}

void TestMissingAndLimits(const fs::path& root)
{
    LocalDataScope environment(root);
    TrackCatalog catalog;
    Check(catalog.Find(L"Missing - Artist", true).name.empty(), "missing directories return no track");
    Check(catalog.FindQueued(L"Missing - Artist").name.empty(), "missing queue returns no track");
    ncmmini::TrackInfo track;
    track.trackId = L"999";
    Check(LyricsStore().Find(track).empty(), "missing lyric returns no lines");
    Check(LyricsStore().Find({}).empty(), "missing lyric identifiers return no lines");
    Write(NormalCache(root) / L"queue", "");
    Check(catalog.FindQueued(L"Missing - Artist").name.empty(), "empty file is ignored");
    Write(NormalCache(root) / L"queue", Queue);
    fs::resize_file(NormalCache(root) / L"queue", 16 * 1024 * 1024 + 1);
    Check(catalog.FindQueued(L"Shared Song - Right Artist").name.empty(), "oversized cache file is ignored");
    Write(NormalCache(root) / L"queue", Queue);
    Check(catalog.FindQueued(L"Shared Song - Right Artist").trackId == L"202", "cache appearing after construction is read");
}

void TestExistingLyrics(const fs::path& root)
{
    LocalDataScope environment(root);
    Write(NormalCache(root) / L"nested/lyric202.lrc", "\xEF\xBB\xBF[00:01.00]First\r\n[00:02.50]Second\r\n");
    ncmmini::TrackInfo track;
    track.lyricsId = L"lyric202";
    auto lines = LyricsStore().Find(track);
    Check(lines.size() == 2, "normal nested BOM lyrics remain supported");
    Check(LyricsStore::Current(lines, std::chrono::milliseconds(500)).empty(), "lyrics before first timestamp are empty");
    Check(LyricsStore::Current(lines, std::chrono::milliseconds(2500)) == L"Second", "lyric timeline remains supported");
}

void TestInjectedDirectories(const fs::path& root)
{
    LocalDataScope environment(root / L"default");
    Write(NormalCache(root / L"default") / L"queue", Queue);
    const auto injected = root / L"custom";
    Write(injected / L"playingList", PlayingList);
    Write(injected / L"lyric505", "[00:01.00]Injected lyric\n");
    TrackCatalog catalog(std::vector<fs::path>{root / L"absent", injected});
    const auto track = catalog.Find(L"Shared Song - Right Artist");
    Check(track.trackId == L"505", "injected directories override default discovery");
    Check(catalog.Find(L"Duet - Second Artist", true).name.empty(), "injected catalog never reads default cache");
    const auto lines = LyricsStore(std::vector<fs::path>{injected}).Find(track);
    Check(lines.size() == 1 && lines[0].text == L"Injected lyric", "lyrics accept directory injection");
    Check(TrackCatalog(std::vector<fs::path>{}).Find(L"Shared Song - Right Artist").name.empty(),
        "empty injection disables cache lookup");
    Check(ncmmini::MediaCacheDirectories(fs::path()).empty(), "missing LocalAppData does not search working directory");
    Write(root / L"not-a-directory", "file");
    Check(TrackCatalog(std::vector<fs::path>{root / L"not-a-directory", injected})
        .FindQueued(L"Shared Song - Right Artist").trackId == L"505", "invalid directory does not block later cache");
}

void TestCatalogReload(const fs::path& root)
{
    const auto first = root / L"first";
    const auto second = root / L"second";
    Write(first / L"cached", R"([{"id":7,"name":"Cached","artist":"Artist","coverUrl":"https://example.test/old.jpg"}])");
    Write(second / L"cached", R"([{"id":7,"name":"Cached","artist":"Artist","coverUrl":"https://example.test/new.jpg"}])");
    fs::last_write_time(first / L"cached", fs::file_time_type::clock::now() - std::chrono::hours(1));
    TrackCatalog catalog(std::vector<fs::path>{first, second});
    Check(catalog.Find(L"Cached - Artist").coverUrl == L"https://example.test/new.jpg",
        "fallback catalog preserves newest metadata for duplicate ID");
    Write(second / L"cached", R"([{"id":7,"name":"Cached","artist":"Artist","coverUrl":"https://example.test/refreshed.jpg"}])");
    Check(catalog.Find(L"Cached - Artist").coverUrl == L"https://example.test/new.jpg",
        "fallback catalog keeps existing reload throttle");
    Check(catalog.Find(L"Cached - Artist", true).coverUrl == L"https://example.test/refreshed.jpg",
        "force reload refreshes injected cache");
    fs::remove(first / L"cached");
    fs::remove(second / L"cached");
    Check(catalog.Find(L"Cached - Artist", true).name.empty(), "force reload removes deleted cache entries");
}

void TestArtistIdCollision(const fs::path& root)
{
    const auto queue = root / L"queue";
    Write(queue, R"({"queue":[{"id":101,"name":"First","artists":[{"id":202,"name":"Singer A"}]},{"id":202,"name":"Second","artists":[{"id":303,"name":"Singer B"}],"album":{"picUrl":"https://example.test/second.jpg"},"lrcid":"lyric202"}]})");
    TrackCatalog catalog(std::vector<fs::path>{root});
    const auto queued = catalog.FindQueued(L"Second - Singer B");
    Check(queued.trackId == L"202" && queued.coverUrl == L"https://example.test/second.jpg"
        && queued.lyricsId == L"lyric202", "review fixture: queued song survives earlier artist ID collision");
    const auto found = catalog.Find(L"Second - Singer B", true);
    Check(found.trackId == L"202" && found.coverUrl == L"https://example.test/second.jpg"
        && found.lyricsId == L"lyric202", "review fixture: full lookup retains song metadata");
    Check(catalog.FindQueued(L"First - Singer A").trackId == L"101", "earlier real song remains available");
    Check(catalog.FindQueued(L"Singer A").name.empty(), "artist objects are not title-only songs");
    fs::rename(queue, root / L"cached");
    const auto fallback = catalog.Find(L"Second - Singer B", true);
    Check(fallback.trackId == L"202" && fallback.coverUrl == L"https://example.test/second.jpg"
        && fallback.lyricsId == L"lyric202", "catalog scan survives artist ID collision without queue fast path");
}

void TestMetadataAndTitleOnlyTracks(const fs::path& root)
{
    Write(root / L"queue", R"({"queue":[
        {"id":101,"name":"First","ar":[{"id":202,"name":"Abbreviated Singer"}],
            "al":{"id":303,"name":"Abbreviated Album"}},
        {"id":202,"name":"Instrumental"},
        {"id":303,"name":"Empty Artists","artists":[]},
        {"name":"Only A Name"},
        {"id":404,"name":"Last","artist":{"id":606,"name":"Object Singer"},
            "album":{"id":202,"name":"Standard Album","artists":[{"id":101,"name":"Album Singer"}]}}
    ]})");
    TrackCatalog catalog(std::vector<fs::path>{root});
    Check(catalog.FindQueued(L"Instrumental").trackId == L"202", "song without artist survives metadata ID collisions");
    Check(catalog.FindQueued(L"Empty Artists").trackId == L"303", "song with empty artists remains available");
    Check(catalog.FindQueued(L"Only A Name").name == L"Only A Name", "song with only a name remains supported");
    Check(catalog.FindQueued(L"Abbreviated Singer").name.empty(), "ar metadata is not a song");
    Check(catalog.FindQueued(L"Abbreviated Album").name.empty(), "al metadata is not a song");
    Check(catalog.FindQueued(L"Object Singer").name.empty(), "singular artist metadata is not a song");
    Check(catalog.FindQueued(L"Standard Album").name.empty(), "album metadata is not a song");
    Check(catalog.FindQueued(L"Album Singer").name.empty(), "nested album metadata is not a song");
    Check(catalog.FindQueued(L"First - Abbreviated Singer").trackId == L"101", "later metadata cannot overwrite an earlier real song");
}

void TestTrackKeyAndNewestMetadata(const fs::path& root)
{
    Write(root / L"queue", R"({"queue":[
        {"id":77,"name":"Shared","artist":"First Artist"},
        {"id":77,"name":"Different","artist":"Second Artist"},
        {"id":77,"name":"Shared","artist":"Second Artist"},
        {"id":78,"name":"Shared","artist":"First Artist","lyricsId":"alternate78"}
    ]})");
    TrackCatalog catalog(std::vector<fs::path>{root});
    Check(catalog.FindQueued(L"Different - Second Artist").trackId == L"77", "track key includes title as well as ID");
    Check(catalog.FindQueued(L"Shared - Second Artist").trackId == L"77", "track key includes artist as well as ID");
    Check(catalog.FindQueued(L"Shared - First Artist").trackId == L"77", "first matching song is preserved");

    fs::remove(root / L"queue");
    Write(root / L"newer", R"([{"id":9,"name":"cached","artist":"artist","coverUrl":"https://example.test/new.jpg","lyricsId":"new9"}])");
    Write(root / L"older", R"([{"id":9,"name":"Cached","artist":"Artist","coverUrl":"https://example.test/old.jpg","lyricsId":"old9"}])");
    fs::last_write_time(root / L"older", fs::file_time_type::clock::now() - std::chrono::hours(1));
    const auto newest = catalog.Find(L"Cached - Artist", true);
    Check(newest.trackId == L"9" && newest.coverUrl == L"https://example.test/new.jpg" && newest.lyricsId == L"new9",
        "equivalent title and artist keep newest metadata across caches");
}

void TestScanBounds(const fs::path& root)
{
    const auto depth = root / L"depth";
    Write(depth / L"one/two/cache", Queue);
    Write(depth / L"one/two/three/cache", PlayingList);
    Write(depth / L"one/two/three/lyric505", "[00:01.00]Too deep\n");
    fs::last_write_time(depth / L"one/two/cache", fs::file_time_type::clock::now() - std::chrono::hours(1));
    TrackCatalog catalog(std::vector<fs::path>{depth});
    Check(catalog.Find(L"Shared Song - Right Artist").trackId == L"202", "scan reads through two subdirectories");
    Check(catalog.Find(L"Shared Song - Right Artist").coverUrl != L"https://example.test/packaged.jpg",
        "scan ignores deeper track files");
    ncmmini::TrackInfo track;
    track.trackId = L"505";
    Check(LyricsStore(std::vector<fs::path>{depth}).Find(track).empty(), "lyric scan respects directory depth bound");

    const auto count = root / L"file-count";
    const auto now = fs::file_time_type::clock::now();
    for (int index = 0; index < 97; ++index)
    {
        const auto path = count / (L"cache" + std::to_wstring(index));
        Write(path, index == 0 ? Queue : "{}");
        fs::last_write_time(path, now - std::chrono::seconds(100 - index));
    }
    TrackCatalog capped(std::vector<fs::path>{count});
    Check(capped.Find(L"Shared Song - Right Artist").name.empty(), "catalog reads only newest 96 files");
    fs::last_write_time(count / L"cache0", now);
    Check(capped.Find(L"Shared Song - Right Artist", true).trackId == L"202", "newly recent file enters catalog scan budget");

    const auto lyrics = root / L"lyric-count";
    for (int index = 0; index < 33; ++index)
    {
        const auto path = lyrics / (L"lyric505-" + std::to_wstring(index));
        Write(path, index == 0 ? "[00:01.00]Old lyric\n" : "invalid");
        fs::last_write_time(path, now - std::chrono::seconds(100 - index));
    }
    LyricsStore store(std::vector<fs::path>{lyrics});
    Check(store.Find(track).empty(), "lyrics read at most 32 matching files");
    fs::last_write_time(lyrics / L"lyric505-0", now);
    Check(store.Find(track).size() == 1, "newly recent lyric enters scan budget");

    const auto entries = root / L"entries";
    for (int index = 0; index < 2050; ++index)
        Write(entries / (L"entry" + std::to_wstring(index)), "invalid");
    fs::path outsideBudget;
    std::size_t index = 0;
    for (const auto& entry : fs::directory_iterator(entries))
        if (index++ == 2048) { outsideBudget = entry.path(); break; }
    if (outsideBudget.empty()) throw std::runtime_error("Insufficient scan bound fixtures");
    Write(outsideBudget, Queue);
    Check(TrackCatalog(std::vector<fs::path>{entries}).Find(L"Shared Song - Right Artist").name.empty(),
        "catalog bounds total directory entries, including invalid files");
    const auto later = root / L"later-cache";
    Write(later / L"cached", PlayingList);
    // Explicit times avoid ties on filesystems with coarse timestamp resolution.
    fs::last_write_time(later / L"cached", now + std::chrono::hours(1));
    Check(TrackCatalog(std::vector<fs::path>{later}).Find(L"Shared Song - Right Artist").trackId == L"505",
        "catalog fallback reads nested playingList shape");
    const auto selected = TrackCatalog(std::vector<fs::path>{entries, later}).Find(L"Shared Song - Right Artist");
    Check(selected.trackId == L"505",
        "full first directory budget does not starve second directory");

    const auto bytes = root / L"bytes";
    std::string large(16 * 1024 * 1024, ' ');
    large[0] = '{';
    for (int file = 0; file < 4; ++file)
    {
        const auto path = bytes / (L"large" + std::to_wstring(file));
        Write(path, large);
        fs::last_write_time(path, now);
    }
    Write(bytes / L"small", Queue);
    fs::last_write_time(bytes / L"small", now - std::chrono::hours(1));
    TrackCatalog byteLimited(std::vector<fs::path>{bytes});
    Check(byteLimited.Find(L"Shared Song - Right Artist").name.empty(), "fallback read budget is bounded to 64 MiB");
    fs::remove(bytes / L"large0");
    Check(byteLimited.Find(L"Shared Song - Right Artist", true).trackId == L"202", "remaining read budget permits later small file");
}

int VerifyLocalCache()
{
    const auto path = PackagedCache(ncmmini::LocalAppDataPath()) / L"playingList";
    std::error_code error;
    const auto size = fs::file_size(path, error);
    if (error || size == 0 || size > 16 * 1024 * 1024)
        throw std::runtime_error("Local packaged playingList is absent or outside size limit");
    std::ifstream input(path, std::ios::binary);
    std::string text(static_cast<std::size_t>(size), '\0');
    if (!input.read(text.data(), static_cast<std::streamsize>(size)))
        throw std::runtime_error("Could not read local playingList");
    ncmmini::JsonValue json;
    if (!ncmmini::ParseJson(text, json)) throw std::runtime_error("Local playingList is not valid JSON");
    const auto* listValue = json.Find("list");
    const auto* list = listValue == nullptr ? nullptr : listValue->AsArray();
    if (list == nullptr || list->empty()) throw std::runtime_error("Local playingList has no tracks");
    const auto* track = list->front().Find("track");
    const auto* nameValue = track == nullptr ? nullptr : track->Find("name");
    const auto* name = nameValue == nullptr ? nullptr : nameValue->AsString();
    const auto* artistsValue = track == nullptr ? nullptr : track->Find("artists");
    const auto* artists = artistsValue == nullptr ? nullptr : artistsValue->AsArray();
    if (name == nullptr || artists == nullptr || artists->empty())
        throw std::runtime_error("Local playingList track lacks expected name or artists fields");
    const auto* artistValue = artists->front().Find("name");
    const auto* artist = artistValue == nullptr ? nullptr : artistValue->AsString();
    if (artist == nullptr) throw std::runtime_error("Local playingList artist has no name");
    const auto title = ncmmini::Utf8ToWide(*name) + L" - " + ncmmini::Utf8ToWide(*artist);
    TrackCatalog catalog;
    const auto found = catalog.FindQueued(title);
    Check(found.name == ncmmini::Utf8ToWide(*name) && !found.artist.empty() && !found.trackId.empty(),
        "local packaged playingList resolves title, artist and track ID");
    Check(!found.coverUrl.empty(), "local packaged track exposes cover URL without downloading");
    Check(catalog.Find(title).trackId == found.trackId, "local full lookup agrees with queued lookup");
    std::cout << "Local read-only cache check complete; no queue contents printed\n";
    return failures == 0 ? 0 : 1;
}
}

int main(int argumentCount, char** arguments)
{
    try
    {
        if (argumentCount == 2 && std::string(arguments[1]) == "--verify-local-cache")
            return VerifyLocalCache();
        const auto root = fs::path(ncmmini::ExecutableDirectory()) / L"fixtures"
            / (L"run-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        TestNormalAndMatching(root / L"normal");
        TestPackaged(root / L"packaged");
        TestSelection(root / L"selection");
        TestMissingAndLimits(root / L"missing");
        TestExistingLyrics(root / L"lyrics");
        TestInjectedDirectories(root / L"injected");
        TestCatalogReload(root / L"reload");
        TestArtistIdCollision(root / L"artist-id-collision");
        TestMetadataAndTitleOnlyTracks(root / L"metadata");
        TestTrackKeyAndNewestMetadata(root / L"track-keys");
        TestScanBounds(root / L"bounds");
        std::cout << checks << " checks, " << failures << " failures\n";
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Fixture error: " << error.what() << '\n';
        return 2;
    }
}
