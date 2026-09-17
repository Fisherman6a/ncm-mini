#include "Media.h"

#include "Json.h"

#include <objidl.h>
#include <winhttp.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace ncmmini
{
namespace
{
constexpr std::uintmax_t MaximumCacheFileSize = 16 * 1024 * 1024;
constexpr std::uintmax_t MaximumCacheReadSize = 64 * 1024 * 1024;
constexpr std::size_t MaximumCacheEntries = 2048;
constexpr int MaximumCacheDepth = 2;
constexpr std::size_t MaximumCatalogFiles = 96;
constexpr std::size_t MaximumLyricFiles = 32;
constexpr std::size_t CoverWidth = 40;
constexpr std::size_t CoverHeight = 40;

template <typename Interface>
void Release(Interface*& value)
{
    if (value != nullptr)
    {
        value->Release();
        value = nullptr;
    }
}

bool ReadFile(const std::filesystem::path& path, std::string& text, std::uintmax_t& remainingBytes)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > MaximumCacheFileSize || size > remainingBytes)
    {
        return false;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return false;
    }
    remainingBytes -= size;
    // Read only the measured size even if the player appends to the file concurrently.
    text.resize(static_cast<std::size_t>(size));
    if (!stream.read(text.data(), static_cast<std::streamsize>(size)))
    {
        text.clear();
        return false;
    }
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF
        && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
    {
        text.erase(0, 3);
    }
    return !text.empty();
}

std::wstring JsonText(const JsonValue* value)
{
    if (value == nullptr)
    {
        return {};
    }
    if (const auto* text = value->AsString())
    {
        return Utf8ToWide(*text);
    }
    if (const auto* number = value->AsNumber())
    {
        if (!std::isfinite(*number))
        {
            return {};
        }
        std::wostringstream stream;
        if (std::floor(*number) == *number)
        {
            stream << std::fixed << std::setprecision(0) << *number;
        }
        else
        {
            stream << std::setprecision(std::numeric_limits<double>::max_digits10) << *number;
        }
        return stream.str();
    }
    return {};
}

std::wstring FirstText(const JsonValue& object, std::initializer_list<const char*> names)
{
    for (const auto* name : names)
    {
        auto value = JsonText(object.Find(name));
        if (!Trim(value).empty())
        {
            return value;
        }
    }
    return {};
}

std::wstring ReadArtists(const JsonValue& object)
{
    const JsonValue* artists = object.Find("artists");
    if (artists == nullptr)
    {
        artists = object.Find("ar");
    }
    if (artists == nullptr)
    {
        return JsonText(object.Find("artist"));
    }
    if (const auto* direct = artists->AsString())
    {
        return Utf8ToWide(*direct);
    }
    const auto* array = artists->AsArray();
    if (array == nullptr)
    {
        return {};
    }
    std::wstring result;
    for (const auto& item : *array)
    {
        auto name = item.AsObject() == nullptr ? JsonText(&item) : JsonText(item.Find("name"));
        if (!Trim(name).empty())
        {
            if (!result.empty()) result += L"/";
            result += name;
        }
    }
    return result;
}

TrackInfo TryCreateTrack(const JsonValue& value)
{
    TrackInfo track;
    if (value.AsObject() == nullptr)
    {
        return track;
    }
    track.name = JsonText(value.Find("name"));
    if (Trim(track.name).empty())
    {
        return {};
    }
    track.artist = ReadArtists(value);
    const JsonValue* album = value.Find("album");
    if (album == nullptr || album->AsObject() == nullptr)
    {
        album = value.Find("al");
    }
    track.coverUrl = album != nullptr && album->AsObject() != nullptr
        ? FirstText(*album, {"picUrl", "coverImgUrl", "coverUrl", "blurPicUrl"})
        : FirstText(value, {"picUrl", "coverImgUrl", "coverUrl", "blurPicUrl"});
    track.trackId = FirstText(value, {"id", "trackId"});
    track.lyricsId = FirstText(value, {"lrcid", "lyricsId", "lyricId"});
    return track;
}

std::wstring Normalize(const std::wstring& value)
{
    std::wstring result;
    bool pendingSpace = false;
    for (const auto character : Trim(value))
    {
        if (iswspace(character))
        {
            pendingSpace = !result.empty();
            continue;
        }
        if (pendingSpace)
        {
            result.push_back(L' ');
            pendingSpace = false;
        }
        result.push_back(static_cast<wchar_t>(towlower(character)));
    }
    return result;
}

using TrackMap = std::map<std::tuple<std::wstring, std::wstring, std::wstring>, TrackInfo>;

void VisitTracks(const JsonValue& value, TrackMap& tracks)
{
    if (const auto* object = value.AsObject())
    {
        auto track = TryCreateTrack(value);
        if (!track.name.empty())
        {
            auto key = std::make_tuple(track.trackId, Normalize(track.name), Normalize(track.artist));
            tracks.try_emplace(std::move(key), std::move(track));
        }
        for (const auto& property : *object)
        {
            // These branches describe metadata; name-only song objects elsewhere remain valid.
            if (property.first == "artist" || property.first == "artists" || property.first == "ar"
                || property.first == "album" || property.first == "al")
            {
                continue;
            }
            VisitTracks(property.second, tracks);
        }
    }
    else if (const auto* array = value.AsArray())
    {
        for (const auto& item : *array)
        {
            VisitTracks(item, tracks);
        }
    }
}

TrackInfo SelectTrack(const std::vector<TrackInfo>& tracks, const std::wstring& parsedName, const std::wstring& parsedArtist)
{
    const auto name = Normalize(parsedName);
    const auto artist = Normalize(parsedArtist);
    const TrackInfo* best = nullptr;
    int bestScore = -1;
    for (const auto& track : tracks)
    {
        if (Normalize(track.name) != name)
        {
            continue;
        }
        int score = 0;
        if (!artist.empty())
        {
            const auto trackArtist = Normalize(track.artist);
            if (trackArtist == artist)
            {
                score = 2;
            }
            else
            {
                std::wistringstream artists(trackArtist);
                std::wstring member;
                while (std::getline(artists, member, L'/'))
                {
                    if (Trim(member) == artist)
                    {
                        score = 1;
                        break;
                    }
                }
                if (score == 0) continue;
            }
        }
        if (score > bestScore)
        {
            best = &track;
            bestScore = score;
        }
    }
    return best == nullptr ? TrackInfo{} : *best;
}

TrackInfo FindTrackInFile(const std::filesystem::path& path, const std::wstring& parsedName,
    const std::wstring& parsedArtist, std::uintmax_t& remainingBytes)
{
    std::string text;
    JsonValue root;
    if (!ReadFile(path, text, remainingBytes) || !ParseJson(text, root))
    {
        return {};
    }
    TrackMap found;
    VisitTracks(root, found);
    std::vector<TrackInfo> tracks;
    tracks.reserve(found.size());
    for (auto& [key, track] : found)
    {
        tracks.push_back(std::move(track));
    }
    return SelectTrack(tracks, parsedName, parsedArtist);
}

bool ContainsCaseInsensitive(const std::wstring& text, const std::wstring& fragment)
{
    return Normalize(text).find(Normalize(fragment)) != std::wstring::npos;
}

struct CacheFile
{
    std::filesystem::file_time_type time;
    std::filesystem::path path;
};

void AddCacheFile(const std::filesystem::path& path, std::vector<CacheFile>& files)
{
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES
        || (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
    {
        return;
    }
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > MaximumCacheFileSize) return;
    const auto time = std::filesystem::last_write_time(path, error);
    if (!error) files.push_back({time, path});
}

void NewestFirst(std::vector<CacheFile>& files)
{
    std::stable_sort(files.begin(), files.end(), [](const auto& left, const auto& right) {
        return left.time > right.time;
    });
}

std::vector<CacheFile> QueueFiles(const std::vector<std::filesystem::path>& directories)
{
    std::vector<CacheFile> files;
    for (const auto& directory : directories)
    {
        if (directory.empty()) continue;
        AddCacheFile(directory / L"queue", files);
        AddCacheFile(directory / L"playingList", files);
    }
    NewestFirst(files);
    return files;
}

std::vector<CacheFile> CacheFiles(const std::vector<std::filesystem::path>& directories,
    const std::vector<std::wstring>& identifiers = {})
{
    std::vector<CacheFile> files;
    for (const auto& directory : directories)
    {
        if (directory.empty()) continue;
        std::error_code error;
        std::filesystem::recursive_directory_iterator iterator(directory,
            std::filesystem::directory_options::skip_permission_denied, error);
        const std::filesystem::recursive_directory_iterator end;
        // Count every entry, including non-matching files and directories.
        std::size_t inspected = 0;
        for (; !error && iterator != end && inspected < MaximumCacheEntries; iterator.increment(error), ++inspected)
        {
            const auto attributes = GetFileAttributesW(iterator->path().c_str());
            if (iterator.depth() >= MaximumCacheDepth || attributes == INVALID_FILE_ATTRIBUTES
                || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                iterator.disable_recursion_pending();
            }
            if (attributes == INVALID_FILE_ATTRIBUTES
                || (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
            {
                continue;
            }
            const auto filename = iterator->path().filename().wstring();
            if (identifiers.empty() || std::any_of(identifiers.begin(), identifiers.end(), [&](const auto& id) {
                return ContainsCaseInsensitive(filename, id);
            }))
            {
                AddCacheFile(iterator->path(), files);
            }
        }
    }
    NewestFirst(files);
    const auto limit = identifiers.empty() ? MaximumCatalogFiles : MaximumLyricFiles;
    if (files.size() > limit) files.resize(limit);
    return files;
}

std::wstring NormalizeCoverUrl(std::wstring url)
{
    url = Trim(std::move(url));
    if (url.rfind(L"//", 0) == 0)
    {
        return L"https:" + url;
    }
    if (url.size() >= 7 && _wcsnicmp(url.c_str(), L"http://", 7) == 0)
    {
        return L"https://" + url.substr(7);
    }
    return url;
}

std::vector<std::uint8_t> Download(const std::wstring& sourceUrl)
{
    const auto url = NormalizeCoverUrl(sourceUrl);
    if (url.empty())
    {
        return {};
    }
    URL_COMPONENTSW components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &components))
    {
        return {};
    }
    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength > 0)
    {
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    if (path.empty()) path = L"/";

    HINTERNET session = WinHttpOpen(L"NCM-Mini/0.2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr)
    {
        return {};
    }
    WinHttpSetTimeouts(session, 4000, 4000, 8000, 8000);
    HINTERNET connection = WinHttpConnect(session, host.c_str(), components.nPort, 0);
    const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = connection == nullptr ? nullptr : WinHttpOpenRequest(connection, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    std::vector<std::uint8_t> result;
    if (request != nullptr && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr))
    {
        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) && status >= 200 && status < 300)
        {
            for (;;)
            {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
                if (result.size() + available > 8 * 1024 * 1024)
                {
                    result.clear();
                    break;
                }
                const auto offset = result.size();
                result.resize(offset + available);
                DWORD read = 0;
                if (!WinHttpReadData(request, result.data() + offset, available, &read))
                {
                    result.clear();
                    break;
                }
                result.resize(offset + read);
            }
        }
    }
    if (request != nullptr) WinHttpCloseHandle(request);
    if (connection != nullptr) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return result;
}

std::vector<std::uint8_t> DecodeCover(std::vector<std::uint8_t>& encoded)
{
    if (encoded.empty() || encoded.size() > static_cast<std::size_t>(std::numeric_limits<DWORD>::max()))
    {
        return {};
    }
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICFormatConverter* converter = nullptr;
    std::vector<std::uint8_t> pixels;
    HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (SUCCEEDED(result)) result = factory->CreateStream(&stream);
    if (SUCCEEDED(result)) result = stream->InitializeFromMemory(encoded.data(), static_cast<DWORD>(encoded.size()));
    if (SUCCEEDED(result)) result = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(result)) result = decoder->GetFrame(0, &frame);
    UINT width = 0, height = 0;
    if (SUCCEEDED(result)) result = frame->GetSize(&width, &height);
    if (SUCCEEDED(result) && (width == 0 || height == 0 || width > 8192 || height > 8192
        || static_cast<std::uint64_t>(width) * height > 16 * 1024 * 1024)) result = E_INVALIDARG;
    if (SUCCEEDED(result)) result = factory->CreateBitmapScaler(&scaler);
    if (SUCCEEDED(result)) result = scaler->Initialize(frame, CoverWidth, CoverHeight, WICBitmapInterpolationModeFant);
    if (SUCCEEDED(result)) result = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(result)) result = converter->Initialize(scaler, GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(result))
    {
        pixels.resize(CoverWidth * CoverHeight * 4);
        result = converter->CopyPixels(nullptr, CoverWidth * 4, static_cast<UINT>(pixels.size()), pixels.data());
        if (FAILED(result)) pixels.clear();
    }
    Release(converter);
    Release(scaler);
    Release(frame);
    Release(decoder);
    Release(stream);
    Release(factory);
    return pixels;
}

bool IsCoverResize(const std::wstring& value)
{
    const auto separator = value.find(L'y');
    if (separator == std::wstring::npos) return false;
    for (const auto& part : {value.substr(0, separator), value.substr(separator + 1)})
    {
        if (part.empty() || part.size() > 4) return false;
        unsigned size = 0;
        for (const auto digit : part)
        {
            if (digit < L'0' || digit > L'9') return false;
            size = size * 10 + digit - L'0';
        }
        if (size == 0 || size > 8192) return false;
    }
    return true;
}

bool IsCoverResizeQuery(const std::wstring& query)
{
    if (query.empty()) return true;
    if (query.front() != L'?') return false;
    std::wistringstream fields(query.substr(1));
    std::set<std::wstring> names;
    std::wstring field;
    while (std::getline(fields, field, L'&'))
    {
        const auto equal = field.find(L'=');
        if (equal == std::wstring::npos) return false;
        const auto name = field.substr(0, equal);
        const auto value = field.substr(equal + 1);
        if (!names.insert(name).second) return false;
        if ((name == L"param" || name == L"thumbnail") && IsCoverResize(value)) continue;
        if (name == L"imageView" && value.empty()) continue;
        if (name == L"type" && value == L"jpg") continue;
        if ((name == L"rotate" || name == L"tostatic") && value == L"0") continue;
        return false;
    }
    return !names.empty() && query.back() != L'&';
}

std::wstring CoverIdentity(const std::wstring& source)
{
    if (source.empty() || source.size() > 4096 || source.find(L'\0') != std::wstring::npos) return {};
    const auto url = NormalizeCoverUrl(source);
    URL_COMPONENTSW parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
        parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS
        || parts.dwHostNameLength == 0 || parts.dwUserNameLength != 0 || parts.dwPasswordLength != 0) return {};
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::transform(host.begin(), host.end(), host.begin(), [](wchar_t value) { return towlower(value); });
    const std::wstring suffix = L".music.126.net";
    const auto prefixLength = host.size() > suffix.size() ? host.size() - suffix.size() : 0;
    const bool netease = prefixLength > 1 && host[0] == L'p' && host.substr(prefixLength) == suffix
        && std::all_of(host.begin() + 1, host.begin() + prefixLength, [](wchar_t c) { return c >= L'0' && c <= L'9'; });
    const std::wstring path = parts.dwUrlPathLength ? std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) : L"/";
    auto query = parts.dwExtraInfoLength ? std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength) : L"";
    // Keep unknown transformations and hosts exact; only known NetEase resize forms share an identity.
    if (netease && IsCoverResizeQuery(query)) query.clear();
    return host + L":" + std::to_wstring(parts.nPort) + path + query;
}

using CoverBytes = std::vector<std::uint8_t>;

std::uint32_t CoverU32(const CoverBytes& bytes, std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

// Read-only subset of Chromium 91 net/disk_cache/blockfile/{disk_format, disk_format_base, addr}.h.
// Index v2.0/v2.1 uses a 368-byte header; entries have a 96-byte prefix and body stream 1.
// https://chromium.googlesource.com/chromium/src/+/91.0.4472.164/net/disk_cache/blockfile/disk_format.h
class CoverBlockCache
{
public:
    explicit CoverBlockCache(std::filesystem::path directory) : directory_(std::move(directory)) {}

    CoverBytes Find(const std::wstring& identity)
    {
        const auto header = Read(L"index", 0, 368);
        if (header.empty() || CoverU32(header, 0) != 0xc103cac3
            || (CoverU32(header, 4) != 0x20000 && CoverU32(header, 4) != 0x20001)) return {};
        const auto tableSize = CoverU32(header, 28) == 0 ? 65536u : CoverU32(header, 28);
        if (tableSize < 65536 || tableSize > 262144 || (tableSize & (tableSize - 1)) != 0) return {};
        const auto table = Read(L"index", 368, tableSize * 4);
        if (table.empty()) return {};
        std::set<std::uint32_t> visited;
        for (std::size_t slot = 0; slot < table.size(); slot += 4)
        {
            auto address = CoverU32(table, slot);
            while (address != 0 && visited.size() < MaximumCacheEntries && visited.insert(address).second)
            {
                if (((address >> 28) & 7) != 2) break;
                const auto entry = ReadAddress(address, 96);
                if (entry.empty()) break;
                const auto current = address;
                address = CoverU32(entry, 4);
                const auto keyLength = CoverU32(entry, 32);
                const auto bodySize = CoverU32(entry, 44);
                if (CoverU32(entry, 20) != 0 || CoverU32(entry, 72) != 0 || keyLength == 0
                    || keyLength > 4096 || bodySize == 0 || bodySize > 8 * 1024 * 1024) continue;
                const auto keyAddress = CoverU32(entry, 36);
                const auto key = ReadAddress(keyAddress ? keyAddress : current, keyLength + 1 + (keyAddress ? 0 : 96));
                if (key.empty() || key.back() != 0) continue;
                const std::string keyText(reinterpret_cast<const char*>(key.data()) + (keyAddress ? 0 : 96), keyLength);
                if (CoverIdentity(Utf8ToWide(keyText)) != identity || !IsClean(current, CoverU32(entry, 8))) continue;
                auto body = ReadAddress(CoverU32(entry, 60), bodySize);
                // The player may replace or evict an entry while we read its body.
                if (body.empty() || ReadAddress(current, 96) != entry || !IsClean(current, CoverU32(entry, 8))) continue;
                auto pixels = DecodeCover(body);
                if (!pixels.empty()) return pixels;
            }
            if (visited.size() >= MaximumCacheEntries || remainingBytes_ == 0) break;
        }
        return {};
    }

private:
    CoverBytes Read(const std::filesystem::path& filename, std::uint64_t offset, std::uint32_t size)
    {
        if (size == 0 || size > remainingBytes_) return {};
        const auto handle = CreateFileW((directory_ / filename).c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return {};
        BY_HANDLE_FILE_INFORMATION info{};
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(offset);
        CoverBytes bytes;
        if (GetFileInformationByHandle(handle, &info)
            && (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0
            && offset + size <= (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32 | info.nFileSizeLow)
            && SetFilePointerEx(handle, position, nullptr, FILE_BEGIN))
        {
            remainingBytes_ -= size;
            bytes.resize(size);
            DWORD read = 0;
            if (!::ReadFile(handle, bytes.data(), size, &read, nullptr) || read != size) bytes.clear();
        }
        CloseHandle(handle);
        return bytes;
    }

    CoverBytes ReadAddress(std::uint32_t address, std::uint32_t size)
    {
        if ((address & 0x80000000) == 0) return {};
        const auto type = (address >> 28) & 7;
        if (type == 0)
        {
            std::wostringstream filename;
            filename << L"f_" << std::hex << std::setfill(L'0') << std::setw(6) << (address & 0x0fffffff);
            return Read(filename.str(), 0, size);
        }
        if (type > 4 || (address & 0x0c000000) != 0) return {};
        const std::uint32_t sizes[] = {0, 36, 256, 1024, 4096};
        const auto blockSize = sizes[type];
        const auto blocks = ((address >> 24) & 3) + 1;
        const auto file = (address >> 16) & 255;
        const auto first = address & 65535;
        if (size > blocks * blockSize) return {};
        const auto filename = L"data_" + std::to_wstring(file);
        const auto header = Read(filename, 0, 60);
        if (header.empty() || CoverU32(header, 0) != 0xc104cac3
            || (CoverU32(header, 4) != 0x20000 && CoverU32(header, 4) != 0x30000)
            || (CoverU32(header, 8) & 65535) != file || CoverU32(header, 12) != blockSize
            || CoverU32(header, 20) > 64896 || first + blocks > CoverU32(header, 20)
            || CoverU32(header, 56) != 0) return {};
        const auto allocated = Read(filename, 80 + first / 8, (first % 8 + blocks + 7) / 8);
        if (allocated.empty()) return {};
        for (unsigned block = 0; block < blocks; ++block)
            if ((allocated[(first % 8 + block) / 8] & (1 << ((first + block) % 8))) == 0) return {};
        return Read(filename, 8192 + static_cast<std::uint64_t>(first) * blockSize, size);
    }

    bool IsClean(std::uint32_t entry, std::uint32_t rankings)
    {
        if (((rankings >> 28) & 7) != 1) return false;
        const auto node = ReadAddress(rankings, 36);
        return !node.empty() && CoverU32(node, 24) == entry && CoverU32(node, 28) == 0;
    }

    std::filesystem::path directory_;
    std::uintmax_t remainingBytes_ = 32 * 1024 * 1024;
};

CoverBytes LoadCachedCover(const std::wstring& url)
{
    const auto identity = CoverIdentity(url);
    if (identity.empty()) return {};
    for (const auto& mediaDirectory : MediaCacheDirectories())
    {
        const auto directory = mediaDirectory.parent_path().parent_path() / L"webapp91x64" / L"Cache";
        bool safeDirectory = true;
        for (auto parent = directory; !parent.empty() && parent != parent.root_path(); parent = parent.parent_path())
        {
            const auto attributes = GetFileAttributesW(parent.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                safeDirectory = false;
                break;
            }
        }
        if (!safeDirectory) continue;
        auto pixels = CoverBlockCache(directory).Find(identity);
        if (!pixels.empty()) return pixels;
    }
    return {};
}
}

std::vector<std::filesystem::path> MediaCacheDirectories(const std::filesystem::path& localAppData)
{
    if (localAppData.empty()) return {};
    const auto suffix = std::filesystem::path(L"NetEase") / L"CloudMusic" / L"webdata" / L"file";
    // Package family is version-independent; no WindowsApps or broad Packages scan is needed.
    return {
        localAppData / suffix,
        localAppData / L"Packages" / L"1F8B0F94.122165AE053F_j2p0p5q0044a6"
            / L"LocalCache" / L"Local" / suffix
    };
}

TrackCatalog::TrackCatalog(std::vector<std::filesystem::path> dataDirectories)
    : dataDirectories_(std::move(dataDirectories))
{
}

LyricsStore::LyricsStore(std::vector<std::filesystem::path> dataDirectories)
    : dataDirectories_(std::move(dataDirectories))
{
}

TrackInfo TrackCatalog::Find(const std::wstring& title, bool forceReload)
{
    const auto [parsedName, parsedArtist] = ParsePlayerTitle(title);
    if (Trim(parsedName).empty())
    {
        return {};
    }
    auto queued = FindQueued(title);
    if (!queued.name.empty())
    {
        return queued;
    }
    EnsureLoaded(forceReload);
    return SelectTrack(tracks_, parsedName, parsedArtist);
}

TrackInfo TrackCatalog::FindQueued(const std::wstring& title) const
{
    const auto [parsedName, parsedArtist] = ParsePlayerTitle(title);
    if (Trim(parsedName).empty())
    {
        return {};
    }
    auto remainingBytes = MaximumCacheReadSize;
    for (const auto& file : QueueFiles(dataDirectories_))
    {
        auto track = FindTrackInFile(file.path, parsedName, parsedArtist, remainingBytes);
        if (!track.name.empty()) return track;
    }
    return {};
}

void TrackCatalog::EnsureLoaded(bool forceReload)
{
    const auto now = std::chrono::steady_clock::now();
    if (!forceReload && lastLoad_.time_since_epoch().count() != 0 && now - lastLoad_ < std::chrono::seconds(5))
    {
        return;
    }
    lastLoad_ = now;
    TrackMap tracks;
    auto remainingBytes = MaximumCacheReadSize;
    for (const auto& candidate : CacheFiles(dataDirectories_))
    {
        std::string text;
        JsonValue root;
        if (ReadFile(candidate.path, text, remainingBytes) && ParseJson(text, root))
        {
            VisitTracks(root, tracks);
        }
    }
    tracks_.clear();
    tracks_.reserve(tracks.size());
    for (auto& [key, track] : tracks)
    {
        tracks_.push_back(std::move(track));
    }
}

std::vector<LyricLine> LyricsStore::Find(const TrackInfo& track) const
{
    std::vector<std::wstring> identifiers;
    if (!Trim(track.lyricsId).empty()) identifiers.push_back(track.lyricsId);
    if (!Trim(track.trackId).empty()) identifiers.push_back(track.trackId);
    if (identifiers.empty())
    {
        return {};
    }
    auto remainingBytes = MaximumCacheReadSize;
    for (const auto& file : CacheFiles(dataDirectories_, identifiers))
    {
        std::string text;
        if (ReadFile(file.path, text, remainingBytes))
        {
            auto lines = Parse(text);
            if (!lines.empty()) return lines;
        }
    }
    return {};
}

std::vector<LyricLine> LyricsStore::Parse(const std::string& text)
{
    std::vector<LyricLine> lines;
    std::istringstream stream(text);
    std::string raw;
    while (std::getline(stream, raw))
    {
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        auto bracket = raw.find('[');
        while (bracket != std::string::npos)
        {
            const auto colon = raw.find(':', bracket + 1);
            const auto close = raw.find(']', bracket + 1);
            if (colon == std::string::npos || close == std::string::npos || colon > close
                || colon - bracket < 2 || colon - bracket > 4 || close - colon < 3)
            {
                break;
            }
            try
            {
                const auto minutes = std::stoi(raw.substr(bracket + 1, colon - bracket - 1));
                const auto secondsValue = std::stod(raw.substr(colon + 1, close - colon - 1));
                if (minutes >= 0 && secondsValue >= 0 && secondsValue < 60)
                {
                    const auto milliseconds = static_cast<long long>(minutes * 60000 + secondsValue * 1000.0);
                    auto lyric = Utf8ToWide(raw.substr(close + 1));
                    lines.push_back({std::chrono::milliseconds(milliseconds), Trim(std::move(lyric))});
                }
            }
            catch (...)
            {
            }
            bracket = raw.find('[', bracket + 1);
        }
    }
    std::sort(lines.begin(), lines.end(), [](const auto& left, const auto& right) { return left.position < right.position; });
    return lines;
}

std::wstring LyricsStore::Current(const std::vector<LyricLine>& lines, std::chrono::milliseconds elapsed)
{
    std::wstring current;
    for (const auto& line : lines)
    {
        if (line.position > elapsed) break;
        current = line.text;
    }
    return current;
}

std::vector<std::uint8_t> LoadCover(const std::wstring& url, bool allowDownload)
{
    auto cached = LoadCachedCover(url);
    if (!cached.empty() || !allowDownload) return cached;
    auto encoded = Download(url);
    if (encoded.empty())
    {
        return {};
    }
    auto result = DecodeCover(encoded);
    if (result.empty())
    {
        Log(L"failed to decode cover image");
    }
    return result;
}
}
