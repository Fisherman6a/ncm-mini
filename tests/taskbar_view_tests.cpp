#include "../src/NCMMini.HostCpp/TaskbarView.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
bool SavePng(Gdiplus::Bitmap& bitmap, const std::filesystem::path& path)
{
    UINT count = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&count, &size);
    if (!size) return false;
    std::vector<BYTE> buffer(size);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(count, size, encoders) != Gdiplus::Ok) return false;
    for (UINT index = 0; index < count; ++index)
        if (std::wstring(encoders[index].MimeType) == L"image/png")
            return bitmap.Save(path.c_str(), &encoders[index].Clsid, nullptr) == Gdiplus::Ok;
    return false;
}

bool SavePreview(ncmmini::TaskbarView& view, const std::filesystem::path& path, bool light)
{
    constexpr int width = ncmmini::TaskbarViewWidth * 2;
    constexpr int height = ncmmini::TaskbarViewHeight * 2;
    Gdiplus::Bitmap sheet(width, height * 3, PixelFormat32bppPARGB);
    Gdiplus::Graphics graphics(&sheet);
    graphics.Clear(light ? Gdiplus::Color(255, 230, 234, 243) : Gdiplus::Color(255, 32, 32, 32));
    ncmmini::TaskbarVisualState state;
    state.light = light;
    for (int row = 0; row < 3; ++row)
    {
        state.hovered = row != 0;
        state.hot = row ? ncmmini::TaskbarHit::Play : ncmmini::TaskbarHit::None;
        state.playing = row == 2;
        auto pixels = view.Draw(192, state);
        if (pixels.empty()) return false;
        Gdiplus::Bitmap bitmap(width, height, width * 4, PixelFormat32bppPARGB,
            reinterpret_cast<BYTE*>(pixels.data()));
        graphics.DrawImage(&bitmap, 0, row * height);
    }
    graphics.Flush(Gdiplus::FlushIntentionSync);
    return SavePng(sheet, path);
}

bool TextGlyphsAvailable()
{
    const std::wstring text = L"\u971c\u96ea\u5343\u5e74 (\u5b98\u65b9\u91cd\u7f6e\u7248)"
        L"\u6d1b\u5929\u4f9dOfficial/\u4e50\u6b63\u7eeb"
        L"\u8718\u86db\u7cf8\u30e2\u30ce\u30dd\u30ea\u30fc\u521d\u97f3\u30df\u30af";
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT font = CreateFontW(-26, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, ncmmini::TaskbarTextFace);
    if (!dc || !font) { if (dc) DeleteDC(dc); if (font) DeleteObject(font); return false; }
    auto previous = SelectObject(dc, font);
    std::vector<WORD> glyphs(text.size());
    const auto result = GetGlyphIndicesW(dc, text.c_str(), static_cast<int>(text.size()), glyphs.data(), GGI_MARK_NONEXISTING_GLYPHS);
    SelectObject(dc, previous); DeleteObject(font); DeleteDC(dc);
    return result != GDI_ERROR && std::none_of(glyphs.begin(), glyphs.end(), [](WORD g) { return g == 0xffff; });
}

bool SaveFontPreview(ncmmini::TaskbarView& view, const std::filesystem::path& path, bool light)
{
    Gdiplus::Bitmap sheet(720, 180, PixelFormat32bppPARGB);
    Gdiplus::Graphics graphics(&sheet);
    graphics.Clear(light ? Gdiplus::Color(255, 230, 234, 243) : Gdiplus::Color(255, 32, 32, 32));
    ncmmini::TaskbarVisualState state;
    state.light = light;
    state.title = L"\u971c\u96ea\u5343\u5e74 (\u5b98\u65b9\u91cd\u7f6e\u7248)";
    state.detail = L"\u6d1b\u5929\u4f9dOfficial/\u4e50\u6b63\u7eeb";
    int top = 0;
    for (UINT dpi : {96u, 144u, 192u})
    {
        const int width = MulDiv(ncmmini::TaskbarViewWidth, dpi, 96);
        const int height = MulDiv(ncmmini::TaskbarViewHeight, dpi, 96);
        auto pixels = view.Draw(dpi, state);
        if (pixels.empty()) return false;
        Gdiplus::Bitmap bitmap(width, height, width * 4, PixelFormat32bppPARGB, reinterpret_cast<BYTE*>(pixels.data()));
        graphics.DrawImage(&bitmap, 0, top);
        top += height;
    }
    graphics.Flush(Gdiplus::FlushIntentionSync);
    return SavePng(sheet, path);
}
}

int main()
{
    int failures = 0;
    int assertions = 0;
    const auto check = [&](bool condition, const char* name) {
        std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
        if (!condition) ++failures;
        ++assertions;
    };
    ncmmini::TaskbarView view;
    check(view.Ready(), "native renderer initializes without extra dependencies");
    check(TextGlyphsAvailable(), "the actual text face contains the reported Chinese, Latin and Japanese glyphs");
    for (UINT dpi : {96u, 144u, 192u})
    {
        const auto layout = ncmmini::MakeTaskbarViewLayout(dpi);
        check(layout.cover.right < layout.title.left && layout.title.right < layout.previous.left
            && layout.previous.right < layout.play.left && layout.play.right < layout.next.left,
            "cover text and controls never overlap at supported DPI");
        check(layout.title.bottom <= layout.detail.top, "title and detail have separate rows");
        check(layout.Hit(layout.previous.left + 2, layout.previous.top + 2) == ncmmini::TaskbarHit::Previous,
            "button padding participates in hit testing");
        for (bool light : {false, true})
        {
            ncmmini::TaskbarVisualState state;
            state.light = light;
            const auto idle = view.Draw(dpi, state);
            const int width = MulDiv(ncmmini::TaskbarViewWidth, dpi, 96);
            const int height = MulDiv(ncmmini::TaskbarViewHeight, dpi, 96);
            check(idle.size() == static_cast<std::size_t>(width * height), "render surface matches layout");
            if (idle.size() != static_cast<std::size_t>(width * height)) continue;
            const auto sample = static_cast<std::size_t>(height / 2 * width + MulDiv(42, dpi, 96));
            check(idle[0] == 0 && (idle[sample] >> 24) == 1, "idle background is transparent with a minimal hit surface");
            state.hovered = true;
            state.hot = ncmmini::TaskbarHit::Play;
            const auto hover = view.Draw(dpi, state);
            check((hover[sample] >> 24) > 1 && (hover[sample] >> 24) < 128,
                "hover remains translucent instead of becoming an opaque panel");
            check(std::all_of(hover.begin(), hover.end(), [](std::uint32_t pixel) {
                const auto alpha = pixel >> 24;
                return (pixel & 255) <= alpha && ((pixel >> 8) & 255) <= alpha
                    && ((pixel >> 16) & 255) <= alpha;
            }), "text and highlight preserve premultiplied alpha");
            state.pressed = ncmmini::TaskbarHit::Play;
            check(view.Draw(dpi, state) != hover, "pressed button has distinct feedback");
            state = {};
            state.light = light;
            state.playing = true;
            check(view.Draw(dpi, state) != idle, "play pause changes rendered icon");
            state.playing = false;
            check(view.Draw(dpi, state) == idle, "leaving hover restores the idle rendering");
            state.title = std::wstring(200, L'W');
            state.detail = std::wstring(200, L'M');
            const auto longText = view.Draw(dpi, state);
            bool buttonsUnchanged = true;
            for (int y = 0; y < height; ++y)
                for (int x = layout.previous.left; x < width; ++x)
                    if (idle[y * width + x] != longText[y * width + x]) buttonsUnchanged = false;
            check(buttonsUnchanged, "long text is clipped before the playback buttons");
        }
    }
    ncmmini::TaskbarVisualState state;
    const auto fallback = view.Draw(96, state);
    check(!view.LoadCover(L"Z:\\nonexistent-ncm-cover.png"), "missing cover is reported");
    check(view.Draw(96, state) == fallback, "missing cover restores the built-in default artwork");
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    const auto output = std::filesystem::path(executable).parent_path();
    check(SavePreview(view, output / L"view-light.png", true), "render light preview sheet");
    check(SavePreview(view, output / L"view-dark.png", false), "render dark preview sheet");
    check(SaveFontPreview(view, output / L"font-regression-light.png", true), "render reported title and artist at 100/150/200 percent light");
    check(SaveFontPreview(view, output / L"font-regression-dark.png", false), "render reported title and artist at 100/150/200 percent dark");

    const auto fixturePath = output / L"test-cover.png";
    {
        Gdiplus::Bitmap fixture(64, 32, PixelFormat32bppPARGB);
        Gdiplus::Graphics graphics(&fixture);
        graphics.Clear(Gdiplus::Color(255, 202, 77, 96));
        Gdiplus::SolidBrush brush(Gdiplus::Color(255, 235, 197, 91));
        graphics.FillRectangle(&brush, 32, 0, 32, 32);
        graphics.Flush(Gdiplus::FlushIntentionSync);
        check(SavePng(fixture, fixturePath), "create local image decoder fixture");
    }
    check(view.LoadCover(fixturePath.wstring()), "real PNG cover decodes");
    const auto externalCover = view.Draw(96, state);
    check(externalCover != fallback, "decoded cover replaces default pixels");
    check(std::filesystem::remove(fixturePath), "loaded image does not keep its file locked");
    {
        std::ofstream corrupt(fixturePath, std::ios::binary);
        corrupt << "not an image";
    }
    check(!view.LoadCover(fixturePath.wstring()), "corrupt cover is rejected");
    check(view.Draw(96, state) == fallback, "corrupt cover restores default after a valid image");
    std::filesystem::remove(fixturePath);
    check(!view.LoadCover(L""), "empty cover selects built-in artwork");
    check(view.Draw(96, state) == fallback, "empty cover keeps the default artwork");
    std::cout << "view tests: " << assertions << " assertions, " << failures << " failures\n";
    return failures ? 1 : 0;
}
