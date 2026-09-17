#include "../src/NCMMini.HostCpp/Media.h"
#include "../src/NCMMini.HostCpp/Json.h"

#include <objbase.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;

namespace
{
int checks = 0;
int failures = 0;

void Check(bool result, const char* message)
{
    ++checks;
    if (!result) ++failures;
    std::cout << (result ? "PASS " : "FAIL ") << message << '\n';
}

void Put32(Bytes& bytes, std::size_t offset, std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes.at(offset++) = static_cast<std::uint8_t>(value >> shift);
}

void Write(const fs::path& path, const Bytes& bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("Cannot write cover fixture");
}

Bytes Bitmap()
{
    Bytes bmp(58);
    bmp[0] = 'B'; bmp[1] = 'M';
    Put32(bmp, 2, 58); Put32(bmp, 10, 54); Put32(bmp, 14, 40);
    Put32(bmp, 18, 1); Put32(bmp, 22, 1);
    bmp[26] = 1; bmp[28] = 24;
    Put32(bmp, 34, 4);
    bmp[54] = 17; bmp[55] = 34; bmp[56] = 51;
    return bmp;
}

Bytes BlockFile(unsigned number, unsigned blockSize, unsigned blocks)
{
    Bytes bytes(8192 + blockSize * blocks);
    Put32(bytes, 0, 0xc104cac3); Put32(bytes, 4, 0x20000);
    Put32(bytes, 8, number); Put32(bytes, 12, blockSize);
    Put32(bytes, 16, blocks); Put32(bytes, 20, blocks);
    for (unsigned i = 0; i < blocks; ++i) bytes[80 + i / 8] |= 1 << (i % 8);
    return bytes;
}

fs::path CachePath(const fs::path& local, bool packaged = false)
{
    const auto root = packaged
        ? local / L"Packages/1F8B0F94.122165AE053F_j2p0p5q0044a6/LocalCache/Local" : local;
    return root / L"NetEase/CloudMusic/webapp91x64/Cache";
}

// Chromium 91 disk_format.h: 368-byte index header, 96-byte entry prefix,
// response body in stream 1, and a 36-byte rankings record with dirty at 28.
void Fixture(const fs::path& cache, const std::string& key, const Bytes& body = Bitmap())
{
    if (key.size() > 927) throw std::runtime_error("Fixture key too long");
    const auto blocks = static_cast<unsigned>((96 + key.size() + 1 + 255) / 256);
    const auto entryAddress = 0xa0010000u | ((blocks - 1) << 24);
    Bytes index(368 + 65536 * 4);
    Put32(index, 0, 0xc103cac3); Put32(index, 4, 0x20001);
    Put32(index, 8, 1); Put32(index, 368, entryAddress);
    auto entries = BlockFile(1, 256, blocks);
    Put32(entries, 8192 + 8, 0x90000000);
    Put32(entries, 8192 + 32, static_cast<std::uint32_t>(key.size()));
    Put32(entries, 8192 + 44, static_cast<std::uint32_t>(body.size()));
    Put32(entries, 8192 + 60, 0x80000001);
    std::copy(key.begin(), key.end(), entries.begin() + 8192 + 96);
    auto rankings = BlockFile(0, 36, 1);
    Put32(rankings, 8192 + 24, entryAddress);
    Write(cache / L"index", index);
    Write(cache / L"data_0", rankings);
    Write(cache / L"data_1", entries);
    Write(cache / L"f_000001", body);
}

class LocalDataScope
{
public:
    explicit LocalDataScope(const fs::path& root) : previous_(ncmmini::LocalAppDataPath())
    {
        if (!SetEnvironmentVariableW(L"LOCALAPPDATA", root.c_str()))
            throw std::runtime_error("Cannot isolate test cache directory");
    }
    ~LocalDataScope() { SetEnvironmentVariableW(L"LOCALAPPDATA", previous_.empty() ? nullptr : previous_.c_str()); }
private:
    std::wstring previous_;
};

bool IsFixtureCover(const Bytes& pixels)
{
    if (pixels.size() != 40 * 40 * 4) return false;
    for (std::size_t i = 0; i < pixels.size(); i += 4)
        if (pixels[i] != 17 || pixels[i + 1] != 34 || pixels[i + 2] != 51 || pixels[i + 3] != 255)
            return false;
    return true;
}

void Patch32(const fs::path& path, std::size_t offset, std::uint32_t value)
{
    Bytes bytes(4);
    Put32(bytes, 0, value);
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(static_cast<std::streamoff>(offset));
    file.write(reinterpret_cast<const char*>(bytes.data()), 4);
    if (!file) throw std::runtime_error("Cannot patch cover fixture");
}

const std::string Album = "http://p3.music.126.net/identity-token/123456.jpg";
const std::wstring AlbumWide(Album.begin(), Album.end());
const std::string Thumbnail = "?imageView=&thumbnail=216y216&type=jpg&rotate=0&tostatic=0";

void TestMatches(const fs::path& root)
{
    LocalDataScope environment(root);
    auto cache = CachePath(root);
    Fixture(cache, Album + Thumbnail);
    Check(IsFixtureCover(ncmmini::LoadCover(AlbumWide, false)), "offline lookup accepts native thumbnail of the same album");
    Check(IsFixtureCover(ncmmini::LoadCover(L"https://p3.music.126.net/identity-token/123456.jpg", false)),
        "HTTP and HTTPS identify the same NetEase image");
    Check(IsFixtureCover(ncmmini::LoadCover(L"//p3.music.126.net/identity-token/123456.jpg", false)),
        "protocol-relative album URL remains supported");
    Check(IsFixtureCover(ncmmini::LoadCover(AlbumWide + L"?param=40y40", false)),
        "known resizing parameters preserve image identity");
    Check(ncmmini::LoadCover(AlbumWide + L"?crop=1", false).empty(), "unknown transformations are not stripped");
    Check(ncmmini::LoadCover(L"http://p3.music.126.net/other-token/123456.jpg", false).empty(),
        "same numeric image ID with a different path is not a match");
    Check(ncmmini::LoadCover(L"http://p3.music.126.net.evil.test/identity-token/123456.jpg", false).empty(),
        "host suffix spoof is not a match");
    Check(ncmmini::LoadCover(L"http://p4.music.126.net/identity-token/123456.jpg", false).empty(),
        "different CDN host is not assumed equivalent");
    Check(ncmmini::LoadCover(AlbumWide + L"?param=0y40", false).empty(), "invalid resize is not treated as equivalent");
    Check(ncmmini::LoadCover(L"", false).empty(), "empty URL is a miss");
    Fixture(cache, Album, Bytes{1, 2, 3});
    Fixture(CachePath(root, true), Album + Thumbnail);
    Check(IsFixtureCover(ncmmini::LoadCover(AlbumWide, false)), "bad desktop image does not hide a valid Store image");
    fs::remove_all(cache);
    Check(IsFixtureCover(ncmmini::LoadCover(AlbumWide, false)), "Store cache is found without a desktop cache");
}

void TestBlockBodies(const fs::path& root)
{
    LocalDataScope environment(root);
    const auto cache = CachePath(root);
    for (unsigned type = 2; type <= 4; ++type)
    {
        Fixture(cache, Album);
        const auto blockSize = type == 2 ? 256u : type == 3 ? 1024u : 4096u;
        auto bodyFile = BlockFile(4, blockSize, 3);
        const auto bmp = Bitmap();
        std::copy(bmp.begin(), bmp.end(), bodyFile.begin() + 8192 + blockSize * 2);
        Write(cache / L"data_4", bodyFile);
        Patch32(cache / L"data_1", 8192 + 60, 0x80040002u | (type << 28));
        Check(IsFixtureCover(ncmmini::LoadCover(AlbumWide, false)), "body address selects the correct block file and offset");
    }
    Fixture(cache, Album + std::string(180, 'x'));
    const auto longUrl = AlbumWide + std::wstring(180, L'x');
    Check(IsFixtureCover(ncmmini::LoadCover(longUrl, false)), "inline keys spanning multiple blocks are parsed");
    Fixture(cache, Album);
    const std::string longKey = Album + "?" + std::string(950, 'q');
    auto keyBytes = Bytes(longKey.begin(), longKey.end());
    keyBytes.push_back(0);
    Write(cache / L"f_000002", keyBytes);
    Patch32(cache / L"data_1", 8192 + 32, static_cast<std::uint32_t>(longKey.size()));
    Patch32(cache / L"data_1", 8192 + 36, 0x80000002);
    Check(IsFixtureCover(ncmmini::LoadCover(std::wstring(longKey.begin(), longKey.end()), false)),
        "external key uses its recorded address and exact length");
}

void TestRejects(const fs::path& root)
{
    LocalDataScope environment(root);
    const auto cache = CachePath(root);
    struct Mutation { const wchar_t* file; std::size_t offset; std::uint32_t value; const char* name; };
    const Mutation cases[] = {
        {L"index", 0, 0, "wrong index magic"},
        {L"index", 4, 0x30000, "unsupported index version"},
        {L"index", 28, 0x100000, "oversized index table"},
        {L"index", 28, 65537, "invalid index table size"},
        {L"index", 368, 0, "unindexed image is never guessed"},
        {L"index", 368, 0xb0010000, "wrong entry address type"},
        {L"data_1", 0, 0, "wrong block magic"},
        {L"data_1", 4, 0x40000, "unsupported block version"},
        {L"data_1", 8, 2, "wrong block file number"},
        {L"data_1", 12, 1024, "wrong block size"},
        {L"data_1", 20, 0, "entry past allocated file capacity"},
        {L"data_1", 56, 1, "block header currently being updated"},
        {L"data_1", 80, 0, "entry points to a freed block"},
        {L"data_1", 8192 + 20, 1, "evicted entry"},
        {L"data_1", 8192 + 20, 2, "doomed entry"},
        {L"data_1", 8192 + 32, 0xffffffff, "negative key length"},
        {L"data_1", 8192 + 32, 4097, "oversized key"},
        {L"data_1", 8192 + 32, 200, "key outside address capacity"},
        {L"data_1", 8192 + 44, 0xffffffff, "negative image length"},
        {L"data_1", 8192 + 44, 8 * 1024 * 1024 + 1, "image exceeds 8 MiB"},
        {L"data_1", 8192 + 44, 60, "truncated external image"},
        {L"data_1", 8192 + 60, 0xc300ffff, "image beyond block-file range"},
        {L"data_1", 8192 + 60, 0x8c000001, "external image address does not use truncated file number"},
        {L"data_1", 8192 + 72, 1, "sparse parent entry"},
        {L"data_0", 8192 + 28, 1, "dirty entry"},
        {L"data_0", 8192 + 24, 0xa0010001, "rankings record belongs to another entry"},
        {L"f_000001", 18, 100000, "huge decoded image dimensions"},
    };
    for (const auto& mutation : cases)
    {
        Fixture(cache, Album);
        Patch32(cache / mutation.file, mutation.offset, mutation.value);
        Check(ncmmini::LoadCover(AlbumWide, false).empty(), mutation.name);
    }
    for (const auto* missing : {L"index", L"data_0", L"data_1", L"f_000001"})
    {
        Fixture(cache, Album);
        fs::remove(cache / missing);
        Check(ncmmini::LoadCover(AlbumWide, false).empty(), "missing cache component is a miss");
    }
    Fixture(cache, Album, Bytes{0, 1, 2, 3});
    Check(ncmmini::LoadCover(AlbumWide, false).empty(), "corrupt image is not returned as pixels");
    Fixture(cache, "http://p3.music.126.net/different/999.jpg");
    Patch32(cache / L"data_1", 8192 + 4, 0xa0010000);
    Check(ncmmini::LoadCover(AlbumWide, false).empty(), "cyclic collision list terminates without a false hit");
    Fixture(cache, Album + "?imageView=&thumbnail=216y216&type=jpg&rotate=90&tostatic=0");
    Check(ncmmini::LoadCover(AlbumWide, false).empty(), "rotated derivative is not silently treated as the original cover");
}

void TestEntryLimit(const fs::path& root)
{
    LocalDataScope environment(root);
    const auto cache = CachePath(root);
    Fixture(cache, Album);
    auto entries = BlockFile(1, 256, 2049);
    for (unsigned i = 0; i < 2048; ++i)
        Put32(entries, 8192 + i * 256 + 4, 0xa0010000 + i + 1);
    const auto last = 8192 + 2048 * 256;
    Put32(entries, last + 8, 0x90000000);
    Put32(entries, last + 32, static_cast<std::uint32_t>(Album.size()));
    Put32(entries, last + 44, 58); Put32(entries, last + 60, 0x80000001);
    std::copy(Album.begin(), Album.end(), entries.begin() + last + 96);
    Write(cache / L"data_1", entries);
    Patch32(cache / L"data_0", 8192 + 24, 0xa0010800);
    Check(ncmmini::LoadCover(AlbumWide, false).empty(), "scan stops after 2048 indexed entries");
    Patch32(cache / L"index", 368, 0xa0010800);
    Check(IsFixtureCover(ncmmini::LoadCover(AlbumWide, false)), "same valid record is usable inside entry budget");
}

int VerifyLocalCache()
{
    const auto paths = ncmmini::MediaCacheDirectories();
    for (const auto& directory : paths)
    {
        const auto playlist = directory / L"playingList";
        std::error_code error;
        const auto size = fs::file_size(playlist, error);
        if (error || size == 0 || size > 16 * 1024 * 1024) continue;
        std::ifstream input(playlist, std::ios::binary);
        std::string text(static_cast<std::size_t>(size), '\0');
        if (!input.read(text.data(), static_cast<std::streamsize>(size))) continue;
        ncmmini::JsonValue json;
        if (!ncmmini::ParseJson(text, json)) continue;
        const auto* listValue = json.Find("list");
        const auto* list = listValue ? listValue->AsArray() : nullptr;
        if (!list) continue;
        std::size_t tried = 0;
        for (const auto& item : *list)
        {
            if (++tried > 128) break;
            const auto* track = item.Find("track");
            const auto* album = track ? track->Find("album") : nullptr;
            const auto* urlValue = album ? album->Find("picUrl") : nullptr;
            const auto* url = urlValue ? urlValue->AsString() : nullptr;
            if (!url) continue;
            const auto pixels = ncmmini::LoadCover(ncmmini::Utf8ToWide(*url), false);
            if (pixels.empty()) continue;
            Check(pixels.size() == 6400, "live offline native cover decodes to 40x40 BGRA");
            const bool varied = !std::equal(pixels.begin() + 4, pixels.end(), pixels.begin());
            Check(varied, "live image contains distinct pixel colors");
            std::cout << "OFFLINE_HIT allowDownload=false url=" << *url << " bytes=" << pixels.size()
                << " playlistCandidate=" << tried << '\n';
            return failures ? 1 : 0;
        }
    }
    std::cerr << "No matching live offline cover found among bounded playlist candidates\n";
    return 1;
}
}

int main(int argc, char** argv)
{
    const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) return 2;
    try
    {
        if (argc == 2 && std::string(argv[1]) == "--verify-local-cache")
        {
            const auto result = VerifyLocalCache();
            CoUninitialize();
            return result;
        }
        const auto root = fs::path(ncmmini::ExecutableDirectory()) / L"fixtures"
            / (L"cover-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        {
            LocalDataScope environment(root);
            Fixture(CachePath(root), "http://127.0.0.1:1/cover.bmp");
            Check(IsFixtureCover(ncmmini::LoadCover(L"http://127.0.0.1:1/cover.bmp")),
                "native indexed cover is returned when album download cannot connect");
        }
        TestMatches(root / L"matches");
        TestBlockBodies(root / L"blocks");
        TestRejects(root / L"rejects");
        TestEntryLimit(root / L"entry-limit");
        std::cout << checks << " checks, " << failures << " failures\n";
        CoUninitialize();
        return failures ? 1 : 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Cover fixture error: " << error.what() << '\n';
        CoUninitialize();
        return 2;
    }
}
