#include "TaskbarView.h"

#include <algorithm>

namespace ncmmini
{
namespace
{
using namespace Gdiplus;

void RoundedPath(GraphicsPath& path, REAL x, REAL y, REAL width, REAL height, REAL radius)
{
    const REAL diameter = radius * 2;
    path.AddArc(x, y, diameter, diameter, 180, 90);
    path.AddArc(x + width - diameter, y, diameter, diameter, 270, 90);
    path.AddArc(x + width - diameter, y + height - diameter, diameter, diameter, 0, 90);
    path.AddArc(x, y + height - diameter, diameter, diameter, 90, 90);
    path.CloseFigure();
}

void Text(Graphics& graphics, const std::wstring& value, const Font& font,
    const TaskbarRect& area, Color color, bool centered = false)
{
    RectF rect(static_cast<REAL>(area.left), static_cast<REAL>(area.top),
        static_cast<REAL>(area.right - area.left), static_cast<REAL>(area.bottom - area.top));
    StringFormat format;
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    format.SetLineAlignment(StringAlignmentCenter);
    format.SetAlignment(centered ? StringAlignmentCenter : StringAlignmentNear);
    SolidBrush brush(color);
    const auto saved = graphics.Save();
    graphics.SetClip(rect);
    graphics.DrawString(value.c_str(), static_cast<INT>(value.size()), &font, rect, &format, &brush);
    graphics.Restore(saved);
}
}

TaskbarHit TaskbarViewLayout::Hit(int x, int y) const
{
    return HitTest({x, y}, cover, previous, play, next,
        {title.left, title.top, detail.right, detail.bottom});
}

TaskbarViewLayout MakeTaskbarViewLayout(UINT dpi)
{
    const auto rect = [dpi](int left, int top, int right, int bottom) {
        return TaskbarRect{MulDiv(left, dpi, 96), MulDiv(top, dpi, 96),
            MulDiv(right, dpi, 96), MulDiv(bottom, dpi, 96)};
    };
    return {rect(4, 4, 36, 36), rect(46, 3, 242, 22), rect(46, 22, 242, 37),
        rect(250, 4, 282, 36), rect(286, 4, 318, 36), rect(322, 4, 354, 36)};
}

TaskbarView::TaskbarView()
{
    GdiplusStartupInput input;
    if (GdiplusStartup(&token_, &input, nullptr) == Ok) cover_ = DefaultCover();
    else token_ = 0;
}

TaskbarView::~TaskbarView()
{
    cover_.reset();
    if (token_) GdiplusShutdown(token_);
}

bool TaskbarView::SetCoverPixels(const std::vector<std::uint8_t>& bgra)
{
    if (!token_) return false;
    if (bgra.size() != 40 * 40 * 4)
    {
        cover_ = DefaultCover();
        return false;
    }
    Bitmap source(40, 40, 40 * 4, PixelFormat32bppARGB, const_cast<BYTE*>(bgra.data()));
    auto decoded = std::make_unique<Bitmap>(40, 40, PixelFormat32bppPARGB);
    Graphics graphics(decoded.get());
    graphics.Clear(Color(255, 35, 44, 45));
    if (graphics.DrawImage(&source, 0, 0, 40, 40) != Ok)
    {
        cover_ = DefaultCover();
        return false;
    }
    cover_ = std::move(decoded);
    return true;
}

bool TaskbarView::LoadCover(const std::wstring& path)
{
    if (!token_) return false;
    if (!path.empty())
    {
        Bitmap source(path.c_str());
        if (source.GetLastStatus() == Ok && source.GetWidth() && source.GetHeight())
        {
            auto decoded = std::make_unique<Bitmap>(128, 128, PixelFormat32bppPARGB);
            Graphics graphics(decoded.get());
            graphics.Clear(Color(255, 35, 44, 45));
            graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            const auto side = std::min(source.GetWidth(), source.GetHeight());
            if (graphics.DrawImage(&source, Rect(0, 0, 128, 128),
                static_cast<INT>((source.GetWidth() - side) / 2),
                static_cast<INT>((source.GetHeight() - side) / 2), side, side, UnitPixel) == Ok)
            {
                cover_ = std::move(decoded);
                return true;
            }
        }
    }
    cover_ = DefaultCover();
    return false;
}

std::unique_ptr<Bitmap> TaskbarView::DefaultCover() const
{
    auto bitmap = std::make_unique<Bitmap>(128, 128, PixelFormat32bppPARGB);
    if (bitmap->GetLastStatus() != Ok) return {};
    Graphics graphics(bitmap.get());
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.Clear(Color(255, 34, 72, 70));
    SolidBrush band(Color(255, 67, 112, 101));
    Point points[]{{0, 0}, {46, 0}, {128, 82}, {128, 128}};
    graphics.FillPolygon(&band, points, 4);
    SolidBrush record(Color(255, 25, 32, 34));
    graphics.FillEllipse(&record, 20, 20, 88, 88);
    Pen groove(Color(255, 57, 68, 69), 1.5f);
    graphics.DrawEllipse(&groove, 28, 28, 72, 72);
    graphics.DrawEllipse(&groove, 34, 34, 60, 60);
    SolidBrush label(Color(255, 190, 225, 188));
    graphics.FillEllipse(&label, 49, 49, 30, 30);
    graphics.FillEllipse(&record, 61, 61, 6, 6);
    return bitmap;
}

std::vector<std::uint32_t> TaskbarView::Draw(UINT dpi, const TaskbarVisualState& state) const
{
    if (!Ready() || dpi < 48 || dpi > 768) return {};
    const int width = MulDiv(TaskbarViewWidth, dpi, 96);
    const int height = MulDiv(TaskbarViewHeight, dpi, 96);
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * height);
    Bitmap bitmap(width, height, width * 4, PixelFormat32bppPARGB,
        reinterpret_cast<BYTE*>(pixels.data()));
    Graphics graphics(&bitmap);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    const REAL scale = static_cast<REAL>(dpi) / 96;
    GraphicsPath root;
    RoundedPath(root, 0, 0, static_cast<REAL>(width), static_cast<REAL>(height), 5 * scale);
    // Alpha zero is click-through in a layered HWND. One keeps the entire target usable.
    SolidBrush background(Color(state.hovered ? (state.light ? 85 : 24) : 1, 255, 255, 255));
    graphics.FillPath(&background, &root);
    if (state.hovered)
    {
        Pen border(Color(state.light ? 75 : 20, 255, 255, 255), scale);
        graphics.DrawPath(&border, &root);
    }

    const auto layout = MakeTaskbarViewLayout(dpi);
    const auto coverState = graphics.Save();
    GraphicsPath coverClip;
    RoundedPath(coverClip, static_cast<REAL>(layout.cover.left), static_cast<REAL>(layout.cover.top),
        static_cast<REAL>(layout.cover.right - layout.cover.left),
        static_cast<REAL>(layout.cover.bottom - layout.cover.top), 3 * scale);
    graphics.SetClip(&coverClip);
    graphics.DrawImage(cover_.get(), Rect(layout.cover.left, layout.cover.top,
        layout.cover.right - layout.cover.left, layout.cover.bottom - layout.cover.top));
    graphics.Restore(coverState);

    // Segoe UI has no CJK glyphs, so GDI+ font linking can mix unrelated fallback faces
    // within one title. YaHei UI covers the Chinese and Latin rows with one regular face.
    FontFamily textFamily(TaskbarTextFace);
    Font titleFont(&textFamily, 13 * scale, FontStyleRegular, UnitPixel);
    Font detailFont(&textFamily, 11 * scale, FontStyleRegular, UnitPixel);
    const Color ink = state.light ? Color(255, 26, 29, 33) : Color(255, 245, 246, 248);
    const Color muted = state.light ? Color(255, 87, 94, 103) : Color(255, 187, 192, 200);
    Text(graphics, state.title, titleFont, layout.title, ink);
    Text(graphics, state.detail, detailFont, layout.detail, muted);

    FontFamily fluent(L"Segoe Fluent Icons");
    FontFamily fallback(L"Segoe MDL2 Assets");
    Font iconFont(fluent.GetLastStatus() == Ok ? &fluent : &fallback,
        15 * scale, FontStyleRegular, UnitPixel);
    const auto button = [&](TaskbarHit hit, const TaskbarRect& rect, const wchar_t* glyph) {
        if (state.controlsEnabled && state.hot == hit)
        {
            const bool pressed = state.pressed == hit;
            const BYTE alpha = pressed ? (state.light ? 12 : 13) : (state.light ? 120 : 28);
            const BYTE value = state.light && pressed ? 0 : 255;
            SolidBrush brush(Color(alpha, value, value, value));
            GraphicsPath shape;
            RoundedPath(shape, static_cast<REAL>(rect.left), static_cast<REAL>(rect.top),
                static_cast<REAL>(rect.right - rect.left), static_cast<REAL>(rect.bottom - rect.top), 4 * scale);
            graphics.FillPath(&brush, &shape);
        }
        Text(graphics, glyph, iconFont, rect, !state.controlsEnabled || state.pressed == hit ? muted : ink, true);
    };
    button(TaskbarHit::Previous, layout.previous, L"\ue892");
    button(TaskbarHit::Play, layout.play, state.playbackKnown && state.playing ? L"\ue769" : L"\ue768");
    button(TaskbarHit::Next, layout.next, L"\ue893");
    graphics.Flush(FlushIntentionSync);
    return pixels;
}
}
