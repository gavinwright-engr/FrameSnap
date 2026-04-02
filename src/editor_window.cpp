#include "editor_window.h"

#include "util.h"

#include <limits>

namespace {

constexpr wchar_t kEditorClassName[] = L"OneShotEditorWindow";
constexpr int kTitleStripHeight = 46;
constexpr int kToolbarHeight = 154;
constexpr int kSidebarWidth = 320;
constexpr int kOuterPadding = 20;
constexpr int kButtonHeight = 44;
constexpr int kButtonGap = 8;
constexpr int kWindowButtonWidth = 42;
constexpr int kWindowButtonHeight = 30;
constexpr int kSliderLabelWidth = 88;
constexpr int kSliderValueWidth = 84;
constexpr COLORREF kWindowColor = RGB(242, 246, 251);
constexpr COLORREF kHeaderColor = RGB(250, 252, 255);
constexpr COLORREF kToolbarColor = RGB(248, 250, 253);
constexpr COLORREF kSidebarColor = RGB(244, 247, 252);
constexpr COLORREF kCanvasColor = RGB(236, 241, 248);
constexpr COLORREF kCanvasStripeColor = RGB(228, 234, 243);
constexpr COLORREF kCanvasFrameColor = RGB(205, 214, 226);
constexpr COLORREF kCardColor = RGB(255, 255, 255);
constexpr COLORREF kAccentColor = RGB(29, 78, 216);
constexpr COLORREF kAccentPressedColor = RGB(21, 66, 190);
constexpr COLORREF kBorderColor = RGB(214, 223, 234);
constexpr COLORREF kTitleColor = RGB(15, 23, 42);
constexpr COLORREF kBodyColor = RGB(59, 72, 89);
constexpr COLORREF kMutedColor = RGB(100, 116, 139);
constexpr FloatPoint kStrokeBreakPoint{-1.0f, -1.0f};

enum ControlId {
    ControlPen = 2001,
    ControlHighlighter,
    ControlErase,
    ControlEraseBrush,
    ControlEyedropper,
    ControlUndo,
    ControlRedo,
    ControlSave,
    ControlMinimize,
    ControlMaximize,
    ControlClose,
    ControlWidth,
    ControlColor,
};

std::wstring BuildEditorTitle(const ImageData& image) {
    std::wstring title = L"OneShot Editor - ";
    title += util::FormatRectSize(image.width, image.height);
    if (image.hdrSource) {
        title += L" - HDR source";
    }
    return title;
}

std::wstring FormatBrushWidthLabel(float width) {
    wchar_t buffer[32]{};
    if (std::fabs(width - std::round(width)) < 0.05f) {
        swprintf_s(buffer, L"%.0f px", width);
    } else {
        swprintf_s(buffer, L"%.1f px", width);
    }
    return buffer;
}

RECT RectWithSize(LONG left, LONG top, LONG width, LONG height) {
    return {left, top, left + width, top + height};
}

RECT UnionRectSafe(const RECT& a, const RECT& b) {
    if (util::IsRectEmptySafe(a)) {
        return b;
    }
    if (util::IsRectEmptySafe(b)) {
        return a;
    }
    return {
        std::min(a.left, b.left),
        std::min(a.top, b.top),
        std::max(a.right, b.right),
        std::max(a.bottom, b.bottom),
    };
}

RECT ChildToClientRect(HWND parent, HWND child) {
    RECT rect{};
    if (parent == nullptr || child == nullptr) {
        return rect;
    }
    GetWindowRect(child, &rect);
    MapWindowPoints(HWND_DESKTOP, parent, reinterpret_cast<LPPOINT>(&rect), 2);
    return rect;
}

std::wstring ReadWindowText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1U, L'\0');
    GetWindowTextW(control, buffer.data(), length + 1);
    return buffer.data();
}

Gdiplus::Color ToGdiColor(COLORREF color, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

ActiveTool ResolveWidthTool(ActiveTool activeTool, ActiveTool inkTool) {
    if (activeTool == ActiveTool::Pen || activeTool == ActiveTool::Highlighter) {
        return activeTool;
    }
    return inkTool;
}

bool IsStrokeBreakPoint(const FloatPoint& point) {
    return point.x < 0.0f || point.y < 0.0f;
}

float DistanceToSegment(FloatPoint point, FloatPoint a, FloatPoint b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 0.0001f) {
        const float ex = point.x - a.x;
        const float ey = point.y - a.y;
        return std::sqrt(ex * ex + ey * ey);
    }
    const float t = std::clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / lengthSquared, 0.0f, 1.0f);
    const float px = a.x + t * dx;
    const float py = a.y + t * dy;
    const float ex = point.x - px;
    const float ey = point.y - py;
    return std::sqrt(ex * ex + ey * ey);
}

void PopulateSmoothPath(std::span<const Gdiplus::PointF> points, Gdiplus::GraphicsPath& path) {
    path.Reset();
    if (points.empty()) {
        return;
    }
    if (points.size() == 1) {
        path.AddLine(points[0], points[0]);
        return;
    }
    if (points.size() == 2) {
        path.AddLine(points[0], points[1]);
        return;
    }

    const auto midpoint = [](const Gdiplus::PointF& a, const Gdiplus::PointF& b) {
        return Gdiplus::PointF((a.X + b.X) * 0.5f, (a.Y + b.Y) * 0.5f);
    };
    const auto quadraticToBezier = [](const Gdiplus::PointF& start, const Gdiplus::PointF& control, const Gdiplus::PointF& end,
                                      Gdiplus::PointF& c1, Gdiplus::PointF& c2) {
        c1 = Gdiplus::PointF(start.X + (control.X - start.X) * (2.0f / 3.0f), start.Y + (control.Y - start.Y) * (2.0f / 3.0f));
        c2 = Gdiplus::PointF(end.X + (control.X - end.X) * (2.0f / 3.0f), end.Y + (control.Y - end.Y) * (2.0f / 3.0f));
    };

    path.StartFigure();
    Gdiplus::PointF current = points[0];
    for (size_t i = 1; i + 1 < points.size(); ++i) {
        const Gdiplus::PointF end = midpoint(points[i], points[i + 1]);
        Gdiplus::PointF c1{};
        Gdiplus::PointF c2{};
        quadraticToBezier(current, points[i], end, c1, c2);
        path.AddBezier(current, c1, c2, end);
        current = end;
    }

    Gdiplus::PointF c1{};
    Gdiplus::PointF c2{};
    quadraticToBezier(current, points[points.size() - 2], points.back(), c1, c2);
    path.AddBezier(current, c1, c2, points.back());
}

void DrawStrokeGeometry(Gdiplus::Graphics& graphics, std::span<const Gdiplus::PointF> points, const Stroke& stroke, float width) {
    if (points.empty()) {
        return;
    }

    const bool eraseStroke = stroke.tool == ActiveTool::EraseBrush;
    const BYTE alpha = eraseStroke ? 0 : (stroke.tool == ActiveTool::Highlighter ? 108 : 255);
    const float clampedWidth = std::max(1.0f, width);
    if (points.size() == 1) {
        Gdiplus::SolidBrush brush(ToGdiColor(stroke.color, alpha));
        const float radius = std::max(1.0f, clampedWidth * 0.5f);
        graphics.FillEllipse(&brush, points[0].X - radius, points[0].Y - radius, radius * 2.0f, radius * 2.0f);
        return;
    }

    Gdiplus::Pen pen(ToGdiColor(stroke.color, alpha), clampedWidth);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    Gdiplus::GraphicsPath path(Gdiplus::FillModeAlternate);
    PopulateSmoothPath(points, path);
    graphics.DrawPath(&pen, &path);
}

void DrawStrokeOnView(Gdiplus::Graphics& graphics, const Stroke& stroke, const Gdiplus::RectF& imageRect, const SIZE& imageSize, float width) {
    if (imageSize.cx <= 0 || imageSize.cy <= 0) {
        return;
    }

    const float scale = imageRect.Width / static_cast<float>(std::max<LONG>(1, imageSize.cx));
    std::vector<Gdiplus::PointF> segment;
    segment.reserve(stroke.points.size());
    const auto flush = [&]() {
        if (segment.empty()) {
            return;
        }
        DrawStrokeGeometry(graphics, segment, stroke, width * scale);
        segment.clear();
    };

    for (const auto& point : stroke.points) {
        if (IsStrokeBreakPoint(point)) {
            flush();
            continue;
        }
        const float x = imageRect.X + (point.x / static_cast<float>(imageSize.cx)) * imageRect.Width;
        const float y = imageRect.Y + (point.y / static_cast<float>(imageSize.cy)) * imageRect.Height;
        segment.emplace_back(x, y);
    }
    flush();
}

void DrawStrokeNative(Gdiplus::Graphics& graphics, const Stroke& stroke, float width) {
    std::vector<Gdiplus::PointF> segment;
    segment.reserve(stroke.points.size());
    const auto flush = [&]() {
        if (segment.empty()) {
            return;
        }
        DrawStrokeGeometry(graphics, segment, stroke, width);
        segment.clear();
    };

    for (const auto& point : stroke.points) {
        if (IsStrokeBreakPoint(point)) {
            flush();
            continue;
        }
        segment.emplace_back(point.x, point.y);
    }
    flush();
}

bool StrokeHasContent(const Stroke& stroke) {
    for (const auto& point : stroke.points) {
        if (!IsStrokeBreakPoint(point)) {
            return true;
        }
    }
    return false;
}

HGLOBAL CreateDibV5(const ImageData& image) {
    const SIZE_T headerSize = sizeof(BITMAPV5HEADER);
    const SIZE_T pixelSize = image.pixels.size();
    const HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, headerSize + pixelSize);
    if (handle == nullptr) {
        return nullptr;
    }

    auto* data = static_cast<BYTE*>(GlobalLock(handle));
    if (data == nullptr) {
        GlobalFree(handle);
        return nullptr;
    }

    auto* header = reinterpret_cast<BITMAPV5HEADER*>(data);
    ZeroMemory(header, sizeof(*header));
    header->bV5Size = sizeof(BITMAPV5HEADER);
    header->bV5Width = image.width;
    header->bV5Height = -image.height;
    header->bV5Planes = 1;
    header->bV5BitCount = 32;
    header->bV5Compression = BI_BITFIELDS;
    header->bV5RedMask = 0x00FF0000;
    header->bV5GreenMask = 0x0000FF00;
    header->bV5BlueMask = 0x000000FF;
    header->bV5AlphaMask = 0xFF000000;
    memcpy(data + headerSize, image.pixels.data(), pixelSize);
    GlobalUnlock(handle);
    return handle;
}

void DrawButtonGlyph(Gdiplus::Graphics& graphics, UINT controlId, const Gdiplus::RectF& rect, COLORREF color) {
    Gdiplus::Pen pen(ToGdiColor(color), 2.15f);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    Gdiplus::SolidBrush brush(ToGdiColor(color));
    Gdiplus::SolidBrush softBrush(ToGdiColor(color, 48));

    switch (controlId) {
    case ControlPen: {
        graphics.DrawLine(&pen, rect.X + 3.0f, rect.Y + rect.Height - 4.0f, rect.X + rect.Width - 5.0f, rect.Y + 5.0f);
        Gdiplus::PointF nib[]{
            {rect.X + rect.Width - 6.0f, rect.Y + 5.0f},
            {rect.X + rect.Width - 1.0f, rect.Y + 8.5f},
            {rect.X + rect.Width - 7.5f, rect.Y + 12.5f},
        };
        graphics.FillPolygon(&brush, nib, 3);
        break;
    }
    case ControlHighlighter: {
        Gdiplus::RectF body(rect.X + 3.5f, rect.Y + 5.5f, rect.Width - 7.0f, rect.Height - 11.0f);
        graphics.FillRectangle(&softBrush, body);
        graphics.DrawRectangle(&pen, body);
        graphics.DrawLine(&pen, body.X + 4.0f, body.Y + body.Height - 3.0f, body.GetRight() - 4.0f, body.Y + 4.0f);
        graphics.FillRectangle(&brush, body.X + 3.0f, body.GetBottom() - 4.0f, body.Width - 6.0f, 3.0f);
        break;
    }
    case ControlErase: {
        graphics.DrawBezier(&pen,
            rect.X + 2.0f, rect.Y + rect.Height - 6.0f,
            rect.X + 7.0f, rect.Y + rect.Height - 12.0f,
            rect.X + rect.Width - 9.0f, rect.Y + 10.0f,
            rect.X + rect.Width - 3.0f, rect.Y + 6.0f);
        graphics.DrawLine(&pen, rect.X + 5.0f, rect.Y + rect.Height - 3.0f, rect.X + rect.Width - 4.0f, rect.Y + 4.0f);
        break;
    }
    case ControlEraseBrush: {
        Gdiplus::GraphicsPath body;
        body.AddArc(rect.X + 3.0f, rect.Y + 3.0f, rect.Width - 8.0f, rect.Height - 8.0f, 45.0f, 270.0f);
        body.CloseFigure();
        graphics.FillPath(&softBrush, &body);
        graphics.DrawPath(&pen, &body);
        graphics.DrawLine(&pen, rect.X + 4.5f, rect.Y + rect.Height - 4.5f, rect.X + rect.Width - 4.0f, rect.Y + 4.5f);
        break;
    }
    case ControlEyedropper: {
        graphics.FillEllipse(&brush, rect.X + rect.Width - 8.5f, rect.Y + 3.0f, 6.5f, 6.5f);
        graphics.DrawLine(&pen, rect.X + 5.0f, rect.Y + rect.Height - 4.0f, rect.X + rect.Width - 6.0f, rect.Y + 6.0f);
        graphics.DrawEllipse(&pen, rect.X + 2.5f, rect.Y + rect.Height - 9.0f, 8.0f, 8.0f);
        break;
    }
    case ControlUndo: {
        graphics.DrawArc(&pen, rect.X + 2.0f, rect.Y + 4.0f, rect.Width - 4.0f, rect.Height - 8.0f, 200.0f, 220.0f);
        graphics.DrawLine(&pen, rect.X + 5.0f, rect.Y + 10.0f, rect.X + 1.5f, rect.Y + 14.5f);
        graphics.DrawLine(&pen, rect.X + 5.0f, rect.Y + 10.0f, rect.X + 10.0f, rect.Y + 10.5f);
        break;
    }
    case ControlRedo: {
        graphics.DrawArc(&pen, rect.X + 2.0f, rect.Y + 4.0f, rect.Width - 4.0f, rect.Height - 8.0f, 120.0f, 220.0f);
        graphics.DrawLine(&pen, rect.X + rect.Width - 5.0f, rect.Y + 10.0f, rect.X + rect.Width - 1.0f, rect.Y + 14.5f);
        graphics.DrawLine(&pen, rect.X + rect.Width - 5.0f, rect.Y + 10.0f, rect.X + rect.Width - 10.0f, rect.Y + 10.5f);
        break;
    }
    case ControlSave: {
        graphics.DrawLine(&pen, rect.X + 3.0f, rect.Y + rect.Height - 6.0f, rect.X + rect.Width - 3.0f, rect.Y + rect.Height - 6.0f);
        graphics.DrawLine(&pen, rect.X + 6.0f, rect.Y + rect.Height - 10.0f, rect.X + rect.Width - 6.0f, rect.Y + rect.Height - 10.0f);
        graphics.DrawLine(&pen, rect.X + rect.Width * 0.5f, rect.Y + 4.5f, rect.X + rect.Width * 0.5f, rect.Y + rect.Height - 12.0f);
        graphics.DrawLine(&pen, rect.X + rect.Width * 0.5f, rect.Y + rect.Height - 12.0f, rect.X + rect.Width * 0.5f - 4.0f, rect.Y + rect.Height - 16.0f);
        graphics.DrawLine(&pen, rect.X + rect.Width * 0.5f, rect.Y + rect.Height - 12.0f, rect.X + rect.Width * 0.5f + 4.0f, rect.Y + rect.Height - 16.0f);
        break;
    }
    case ControlMinimize: {
        graphics.DrawLine(&pen, rect.X + 4.0f, rect.Y + rect.Height - 6.0f, rect.X + rect.Width - 4.0f, rect.Y + rect.Height - 6.0f);
        break;
    }
    case ControlMaximize: {
        graphics.DrawRectangle(&pen, rect.X + 4.0f, rect.Y + 4.0f, rect.Width - 8.0f, rect.Height - 8.0f);
        break;
    }
    case ControlClose: {
        graphics.DrawLine(&pen, rect.X + 5.0f, rect.Y + 5.0f, rect.X + rect.Width - 5.0f, rect.Y + rect.Height - 5.0f);
        graphics.DrawLine(&pen, rect.X + rect.Width - 5.0f, rect.Y + 5.0f, rect.X + 5.0f, rect.Y + rect.Height - 5.0f);
        break;
    }
    default:
        break;
    }
}

}  // namespace

EditorWindow::EditorWindow(HINSTANCE instance, HWND owner)
    : instance_(instance), owner_(owner) {}

EditorWindow::~EditorWindow() {
    if (uiFont_ != nullptr) {
        DeleteObject(uiFont_);
    }
    if (titleFont_ != nullptr) {
        DeleteObject(titleFont_);
    }
    if (hintFont_ != nullptr) {
        DeleteObject(hintFont_);
    }
    if (smallIcon_ != nullptr) {
        DestroyIcon(smallIcon_);
    }
    if (largeIcon_ != nullptr) {
        DestroyIcon(largeIcon_);
    }
}

HFONT EditorWindow::CreateUiFont(int height, int weight) const {
    HFONT font = CreateFontW(
        -height,
        0,
        0,
        0,
        weight,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        VARIABLE_PITCH,
        L"Segoe UI Variable Text");
    if (font == nullptr) {
        font = CreateFontW(
            -height,
            0,
            0,
            0,
            weight,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            VARIABLE_PITCH,
            L"Segoe UI");
    }
    return font;
}

void EditorWindow::ApplyWindowChrome() const {
    DWM_WINDOW_CORNER_PREFERENCE cornerPreference = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPreference, sizeof(cornerPreference));
    const COLORREF captionColor = kHeaderColor;
    const COLORREF textColor = kTitleColor;
    const COLORREF borderColor = kBorderColor;
    DwmSetWindowAttribute(hwnd_, DWMWA_CAPTION_COLOR, &captionColor, sizeof(captionColor));
    DwmSetWindowAttribute(hwnd_, DWMWA_TEXT_COLOR, &textColor, sizeof(textColor));
    DwmSetWindowAttribute(hwnd_, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));
}

bool EditorWindow::EnsureWindow() {
    if (hwnd_ != nullptr) {
        return true;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = EditorWindow::WndProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kEditorClassName;
    RegisterClassW(&wc);

    uiFont_ = CreateUiFont(17, FW_NORMAL);
    titleFont_ = CreateUiFont(24, FW_SEMIBOLD);
    hintFont_ = CreateUiFont(15, FW_NORMAL);

    hwnd_ = CreateWindowExW(
        WS_EX_APPWINDOW,
        kEditorClassName,
        L"OneShot Editor",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1360,
        920,
        nullptr,
        nullptr,
        instance_,
        this);
    if (hwnd_ == nullptr) {
        return false;
    }

    smallIcon_ = util::CreateOneShotAppIcon(GetSystemMetrics(SM_CXSMICON));
    largeIcon_ = util::CreateOneShotAppIcon(GetSystemMetrics(SM_CXICON));
    if (smallIcon_ != nullptr) {
        SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon_));
    }
    if (largeIcon_ != nullptr) {
        SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(largeIcon_));
    }

    ApplyWindowChrome();

    penButton_ = CreateWindowW(L"BUTTON", L"Pen", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, hwnd_,
        reinterpret_cast<HMENU>(ControlPen), instance_, nullptr);
    highlighterButton_ = CreateWindowW(L"BUTTON", L"Highlighter", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, hwnd_,
        reinterpret_cast<HMENU>(ControlHighlighter), instance_, nullptr);
    eraseButton_ = CreateWindowW(L"BUTTON", L"Erase Line", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, hwnd_,
        reinterpret_cast<HMENU>(ControlErase), instance_, nullptr);
    eraseBrushButton_ = CreateWindowW(L"BUTTON", L"Erase Pen", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, hwnd_,
        reinterpret_cast<HMENU>(ControlEraseBrush), instance_, nullptr);
    eyedropperButton_ = CreateWindowW(L"BUTTON", L"Eyedropper", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, hwnd_,
        reinterpret_cast<HMENU>(ControlEyedropper), instance_, nullptr);
    undoButton_ = CreateWindowW(L"BUTTON", L"Undo", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, hwnd_,
        reinterpret_cast<HMENU>(ControlUndo), instance_, nullptr);
    redoButton_ = CreateWindowW(L"BUTTON", L"Redo", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, hwnd_,
        reinterpret_cast<HMENU>(ControlRedo), instance_, nullptr);
    saveButton_ = CreateWindowW(L"BUTTON", L"Save Copy", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, hwnd_,
        reinterpret_cast<HMENU>(ControlSave), instance_, nullptr);
    minimizeButton_ = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, hwnd_,
        reinterpret_cast<HMENU>(ControlMinimize), instance_, nullptr);
    maximizeButton_ = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, hwnd_,
        reinterpret_cast<HMENU>(ControlMaximize), instance_, nullptr);
    closeButton_ = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, hwnd_,
        reinterpret_cast<HMENU>(ControlClose), instance_, nullptr);
    widthSlider_ = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_NOTICKS | TBS_TOOLTIPS | TBS_DOWNISLEFT, 0, 0, 0, 0, hwnd_,
        reinterpret_cast<HMENU>(ControlWidth), instance_, nullptr);
    SetWindowTheme(widthSlider_, L"Explorer", nullptr);
    SendMessageW(widthSlider_, TBM_SETRANGE, TRUE, MAKELPARAM(10, 480));
    SendMessageW(widthSlider_, TBM_SETPAGESIZE, 0, 10);
    SendMessageW(widthSlider_, TBM_SETLINESIZE, 0, 1);
    SendMessageW(widthSlider_, TBM_SETTHUMBLENGTH, 26, 0);

    colorPicker_ = std::make_unique<ColorPickerControl>(instance_, hwnd_, ControlColor);
    colorPicker_->Create(0, 0, 220, 336);

    ApplyFonts();
    LayoutControls();
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    return true;
}

void EditorWindow::ApplyFonts() const {
    const std::array<HWND, 8> controls{
        penButton_,
        highlighterButton_,
        eraseButton_,
        eraseBrushButton_,
        eyedropperButton_,
        undoButton_,
        redoButton_,
        saveButton_,
    };
    for (const HWND control : controls) {
        if (control != nullptr) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        }
    }
}

void EditorWindow::Show(const std::shared_ptr<ImageData>& image, AppSettings& settings) {
    if (!EnsureWindow() || image == nullptr) {
        return;
    }

    image_ = image;
    settings_ = &settings;
    strokes_.clear();
    redoStrokes_.clear();
    drawing_ = false;
    activeTool_ = ActiveTool::Pen;
    inkTool_ = ActiveTool::Pen;
    inFlightStroke_ = {};
    ClearMarkupLayer();
    colorPicker_->SetColor(settings.penColor);
    SetWindowTextW(hwnd_, BuildEditorTitle(*image).c_str());
    UpdateToolbarState();
    LayoutControls();
    ShowWindow(hwnd_, IsIconic(hwnd_) ? SW_RESTORE : SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void EditorWindow::LayoutControls() {
    if (hwnd_ == nullptr || colorPicker_ == nullptr) {
        return;
    }

    RECT client{};
    GetClientRect(hwnd_, &client);
    const RECT sidebar = SidebarRect();
    const int contentRight = sidebar.left - kOuterPadding;
    const int toolTop = kTitleStripHeight + 14;
    const int rowTwoTop = kTitleStripHeight + 70;
    const int windowButtonsRight = client.right - 14;
    const int closeX = windowButtonsRight - kWindowButtonWidth;
    const int maximizeX = closeX - kWindowButtonWidth;
    const int minimizeX = maximizeX - kWindowButtonWidth;

    MoveWindow(minimizeButton_, minimizeX, 8, kWindowButtonWidth, kWindowButtonHeight, TRUE);
    MoveWindow(maximizeButton_, maximizeX, 8, kWindowButtonWidth, kWindowButtonHeight, TRUE);
    MoveWindow(closeButton_, closeX, 8, kWindowButtonWidth, kWindowButtonHeight, TRUE);

    int x = kOuterPadding;
    MoveWindow(penButton_, x, toolTop, 110, kButtonHeight, TRUE);
    x += 110 + kButtonGap;
    MoveWindow(highlighterButton_, x, toolTop, 136, kButtonHeight, TRUE);
    x += 136 + kButtonGap;
    MoveWindow(eraseButton_, x, toolTop, 130, kButtonHeight, TRUE);
    x += 130 + kButtonGap;
    MoveWindow(eraseBrushButton_, x, toolTop, 124, kButtonHeight, TRUE);
    x += 124 + kButtonGap;
    MoveWindow(eyedropperButton_, x, toolTop, 134, kButtonHeight, TRUE);

    const int saveWidth = 142;
    const int actionWidth = 92;
    const int saveX = contentRight - saveWidth;
    const int redoX = saveX - kButtonGap - actionWidth;
    const int undoX = redoX - kButtonGap - actionWidth;
    MoveWindow(undoButton_, undoX, toolTop, actionWidth, kButtonHeight, TRUE);
    MoveWindow(redoButton_, redoX, toolTop, actionWidth, kButtonHeight, TRUE);
    MoveWindow(saveButton_, saveX, toolTop, saveWidth, kButtonHeight, TRUE);

    const int sliderX = kOuterPadding + kSliderLabelWidth + 16;
    const int sliderWidth = std::max(220, contentRight - sliderX - kSliderValueWidth - 12);
    MoveWindow(widthSlider_, sliderX, rowTwoTop + 14, sliderWidth, 32, TRUE);

    colorPicker_->Move(sidebar.left + 20, kToolbarHeight + 20, sidebar.right - sidebar.left - 40, 350);
    RefreshButtons();
}

RECT EditorWindow::CanvasRect() const {
    RECT client{};
    GetClientRect(hwnd_, &client);
    return {
        kOuterPadding,
        kToolbarHeight + kOuterPadding,
        client.right - kSidebarWidth - kOuterPadding,
        client.bottom - kOuterPadding,
    };
}

RECT EditorWindow::SidebarRect() const {
    RECT client{};
    GetClientRect(hwnd_, &client);
    return {
        client.right - kSidebarWidth,
        kToolbarHeight,
        client.right,
        client.bottom,
    };
}

Gdiplus::RectF EditorWindow::ImageToViewRect() const {
    const RECT canvas = CanvasRect();
    if (image_ == nullptr || image_->width == 0 || image_->height == 0) {
        return {0, 0, 0, 0};
    }
    const float canvasWidth = static_cast<float>(canvas.right - canvas.left);
    const float canvasHeight = static_cast<float>(canvas.bottom - canvas.top);
    const float scale = std::min(canvasWidth / static_cast<float>(image_->width), canvasHeight / static_cast<float>(image_->height));
    const float width = image_->width * scale;
    const float height = image_->height * scale;
    const float x = canvas.left + (canvasWidth - width) * 0.5f;
    const float y = canvas.top + (canvasHeight - height) * 0.5f;
    return {x, y, width, height};
}

RECT EditorWindow::ImageViewRect() const {
    const auto rect = ImageToViewRect();
    return {
        static_cast<LONG>(std::floor(rect.X)),
        static_cast<LONG>(std::floor(rect.Y)),
        static_cast<LONG>(std::ceil(rect.X + rect.Width)),
        static_cast<LONG>(std::ceil(rect.Y + rect.Height)),
    };
}

std::optional<FloatPoint> EditorWindow::ClientToImage(POINT point) const {
    if (image_ == nullptr) {
        return std::nullopt;
    }
    const auto viewRect = ImageToViewRect();
    if (point.x < viewRect.X || point.y < viewRect.Y || point.x > viewRect.X + viewRect.Width || point.y > viewRect.Y + viewRect.Height) {
        return std::nullopt;
    }
    const float x = (point.x - viewRect.X) * static_cast<float>(image_->width) / viewRect.Width;
    const float y = (point.y - viewRect.Y) * static_cast<float>(image_->height) / viewRect.Height;
    return FloatPoint{x, y};
}

void EditorWindow::InvalidateCanvas(bool) const {
    if (hwnd_ == nullptr) {
        return;
    }
    RECT dirty = ImageViewRect();
    if (util::IsRectEmptySafe(dirty)) {
        dirty = CanvasRect();
    }
    InflateRect(&dirty, 12, 12);
    InvalidateCanvasRect(dirty);
}

void EditorWindow::InvalidateCanvasRect(const RECT& rect) const {
    if (hwnd_ == nullptr) {
        return;
    }
    RECT canvas = CanvasRect();
    RECT dirty = util::IntersectRectSafe(rect, canvas);
    if (util::IsRectEmptySafe(dirty)) {
        return;
    }
    InvalidateRect(hwnd_, &dirty, FALSE);
}

void EditorWindow::InvalidateSidebar() const {
    if (hwnd_ == nullptr) {
        return;
    }
    const RECT sidebar = SidebarRect();
    InvalidateRect(hwnd_, &sidebar, FALSE);
}

void EditorWindow::RefreshButtons() const {
    const std::array<HWND, 11> buttons{
        penButton_,
        highlighterButton_,
        eraseButton_,
        eraseBrushButton_,
        eyedropperButton_,
        undoButton_,
        redoButton_,
        saveButton_,
        minimizeButton_,
        maximizeButton_,
        closeButton_,
    };
    for (const HWND button : buttons) {
        if (button != nullptr) {
            InvalidateRect(button, nullptr, TRUE);
        }
    }
}

void EditorWindow::ClearMarkupLayer() {
    markupImage_ = {};
    if (image_ == nullptr) {
        return;
    }
    markupImage_.width = image_->width;
    markupImage_.height = image_->height;
    markupImage_.hdrSource = image_->hdrSource;
    markupImage_.sourceRect = image_->sourceRect;
    markupImage_.pixels.assign(static_cast<size_t>(image_->width) * static_cast<size_t>(image_->height) * 4U, 0);
}

void EditorWindow::RebuildMarkupLayer() {
    ClearMarkupLayer();
    for (const auto& stroke : strokes_) {
        RasterizeStroke(markupImage_, stroke);
    }
}

float EditorWindow::StrokeScaleFactor() const {
    if (image_ == nullptr || image_->width <= 0 || image_->height <= 0) {
        return 1.0f;
    }
    constexpr float referencePixels = 1920.0f * 1080.0f;
    const float imagePixels = static_cast<float>(image_->width) * static_cast<float>(image_->height);
    const float scale = std::sqrt(referencePixels / std::max(1.0f, imagePixels));
    return std::clamp(scale, 0.85f, 2.25f);
}

float EditorWindow::EffectiveStrokeWidth(const Stroke& stroke) const {
    const float scaled = stroke.width * StrokeScaleFactor();
    if (stroke.tool == ActiveTool::Highlighter) {
        return std::clamp(scaled, 2.0f, 96.0f);
    }
    return std::clamp(scaled, 1.0f, 96.0f);
}

RECT EditorWindow::StrokeDirtyRect(const Stroke& stroke, size_t startIndex) const {
    if (image_ == nullptr || stroke.points.empty()) {
        return {};
    }

    const auto imageRect = ImageToViewRect();
    const SIZE imageSize{image_->width, image_->height};
    const float scale = imageRect.Width / static_cast<float>(std::max<LONG>(1L, imageSize.cx));
    const float padding = std::max(8.0f, EffectiveStrokeWidth(stroke) * scale * 0.75f + 8.0f);

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    bool hasPoint = false;

    const size_t safeStart = startIndex > 0 ? startIndex - 1 : 0;
    for (size_t i = safeStart; i < stroke.points.size(); ++i) {
        const auto& point = stroke.points[i];
        if (IsStrokeBreakPoint(point)) {
            continue;
        }
        const float x = imageRect.X + (point.x / static_cast<float>(imageSize.cx)) * imageRect.Width;
        const float y = imageRect.Y + (point.y / static_cast<float>(imageSize.cy)) * imageRect.Height;
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
        hasPoint = true;
    }

    if (!hasPoint) {
        return {};
    }

    RECT dirty{
        static_cast<LONG>(std::floor(minX - padding)),
        static_cast<LONG>(std::floor(minY - padding)),
        static_cast<LONG>(std::ceil(maxX + padding)),
        static_cast<LONG>(std::ceil(maxY + padding)),
    };
    RECT canvas = CanvasRect();
    return util::IntersectRectSafe(dirty, canvas);
}

void EditorWindow::RasterizeStroke(ImageData& target, const Stroke& stroke) const {
    if (target.width <= 0 || target.height <= 0 || target.pixels.empty() || !StrokeHasContent(stroke)) {
        return;
    }

    Gdiplus::Bitmap bitmap(target.width, target.height, target.width * 4, PixelFormat32bppARGB, target.pixels.data());
    Gdiplus::Graphics graphics(&bitmap);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    if (stroke.tool == ActiveTool::EraseBrush) {
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
    }
    DrawStrokeNative(graphics, stroke, EffectiveStrokeWidth(stroke));
}

void EditorWindow::BeginStroke(FloatPoint point) {
    if (settings_ == nullptr) {
        return;
    }
    drawing_ = true;
    inFlightStroke_ = {};
    inFlightStroke_.tool = activeTool_;
    const ActiveTool widthTool = ResolveWidthTool(activeTool_, inkTool_);
    inFlightStroke_.color = widthTool == ActiveTool::Highlighter ? settings_->highlighterColor : settings_->penColor;
    inFlightStroke_.width = widthTool == ActiveTool::Highlighter ? settings_->highlighterWidth : settings_->penWidth;
    inFlightStroke_.points.push_back(point);
}

void EditorWindow::BreakStroke() {
    if (!drawing_) {
        return;
    }
    if (inFlightStroke_.points.empty() || IsStrokeBreakPoint(inFlightStroke_.points.back())) {
        return;
    }
    inFlightStroke_.points.push_back(kStrokeBreakPoint);
}

void EditorWindow::ExtendStroke(FloatPoint point) {
    if (!drawing_) {
        return;
    }

    if (!inFlightStroke_.points.empty()) {
        const auto& last = inFlightStroke_.points.back();
        if (IsStrokeBreakPoint(last)) {
            inFlightStroke_.points.push_back(point);
            return;
        }
        const float dx = point.x - last.x;
        const float dy = point.y - last.y;
        const float minDistance = std::clamp(EffectiveStrokeWidth(inFlightStroke_) * 0.15f, 0.45f, 2.1f);
        if ((dx * dx + dy * dy) < (minDistance * minDistance)) {
            return;
        }
    }
    inFlightStroke_.points.push_back(point);
}

void EditorWindow::EndStroke() {
    if (!drawing_) {
        return;
    }

    drawing_ = false;
    while (!inFlightStroke_.points.empty() && IsStrokeBreakPoint(inFlightStroke_.points.back())) {
        inFlightStroke_.points.pop_back();
    }

    if (StrokeHasContent(inFlightStroke_)) {
        const RECT dirty = StrokeDirtyRect(inFlightStroke_);
        strokes_.push_back(inFlightStroke_);
        redoStrokes_.clear();
        RasterizeStroke(markupImage_, strokes_.back());
        UpdateToolbarState();
        InvalidateCanvasRect(dirty);
    }
    inFlightStroke_ = {};
}

void EditorWindow::EraseStrokeAt(FloatPoint point) {
    for (auto it = strokes_.rbegin(); it != strokes_.rend(); ++it) {
        if (it->tool == ActiveTool::EraseBrush) {
            continue;
        }
        const float tolerance = std::max(8.0f, EffectiveStrokeWidth(*it) * 0.75f);
        for (size_t i = 1; i < it->points.size(); ++i) {
            if (IsStrokeBreakPoint(it->points[i - 1]) || IsStrokeBreakPoint(it->points[i])) {
                continue;
            }
            if (DistanceToSegment(point, it->points[i - 1], it->points[i]) <= tolerance) {
                const RECT dirty = StrokeDirtyRect(*it);
                strokes_.erase(std::next(it).base());
                redoStrokes_.clear();
                RebuildMarkupLayer();
                UpdateToolbarState();
                InvalidateCanvasRect(util::IsRectEmptySafe(dirty) ? ImageViewRect() : dirty);
                return;
            }
        }
    }
}

void EditorWindow::PickColorAt(FloatPoint point) {
    if (image_ == nullptr || settings_ == nullptr) {
        return;
    }
    const int x = util::Clamp(static_cast<int>(std::round(point.x)), 0, image_->width - 1);
    const int y = util::Clamp(static_cast<int>(std::round(point.y)), 0, image_->height - 1);
    const auto index = (static_cast<size_t>(y) * static_cast<size_t>(image_->width) + static_cast<size_t>(x)) * 4U;
    const COLORREF color = RGB(image_->pixels[index + 2], image_->pixels[index + 1], image_->pixels[index + 0]);
    if (inkTool_ == ActiveTool::Highlighter) {
        settings_->highlighterColor = color;
    } else {
        settings_->penColor = color;
    }
    colorPicker_->SetColor(color);
    InvalidateSidebar();
}

void EditorWindow::UpdateToolbarState() {
    if (settings_ == nullptr || widthSlider_ == nullptr) {
        return;
    }

    const ActiveTool widthTool = ResolveWidthTool(activeTool_, inkTool_);
    const int value = widthTool == ActiveTool::Highlighter ? static_cast<int>(std::round(settings_->highlighterWidth * 10.0f))
                                                           : static_cast<int>(std::round(settings_->penWidth * 10.0f));
    SendMessageW(widthSlider_, TBM_SETPOS, TRUE, value);
    EnableWindow(widthSlider_, activeTool_ != ActiveTool::EraseLine && activeTool_ != ActiveTool::Eyedropper);
    EnableWindow(undoButton_, !strokes_.empty());
    EnableWindow(redoButton_, !redoStrokes_.empty());
    RefreshButtons();
    InvalidateSidebar();
}

void EditorWindow::UpdateWidthsFromSlider() {
    if (settings_ == nullptr) {
        return;
    }

    const float width = static_cast<float>(SendMessageW(widthSlider_, TBM_GETPOS, 0, 0)) / 10.0f;
    const ActiveTool widthTool = ResolveWidthTool(activeTool_, inkTool_);
    if (widthTool == ActiveTool::Highlighter) {
        settings_->highlighterWidth = width;
    } else {
        settings_->penWidth = width;
    }
    RECT toolbarRect = RectWithSize(0, kTitleStripHeight, SidebarRect().left, kToolbarHeight - kTitleStripHeight);
    InvalidateRect(hwnd_, &toolbarRect, FALSE);
    InvalidateSidebar();
}

ImageData EditorWindow::RenderDocument() const {
    ImageData rendered = *image_;
    Gdiplus::Bitmap targetBitmap(rendered.width, rendered.height, rendered.width * 4, PixelFormat32bppARGB, rendered.pixels.data());
    Gdiplus::Graphics graphics(&targetBitmap);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    if (!markupImage_.pixels.empty()) {
        Gdiplus::Bitmap markupBitmap(markupImage_.width, markupImage_.height, markupImage_.width * 4, PixelFormat32bppARGB,
            const_cast<BYTE*>(markupImage_.pixels.data()));
        graphics.DrawImage(&markupBitmap, 0, 0, rendered.width, rendered.height);
    }
    if (drawing_ && StrokeHasContent(inFlightStroke_)) {
        if (inFlightStroke_.tool == ActiveTool::EraseBrush) {
            graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
        }
        DrawStrokeNative(graphics, inFlightStroke_, EffectiveStrokeWidth(inFlightStroke_));
    }
    return rendered;
}

bool EditorWindow::CopyImageToClipboard(const ImageData& image) const {
    HGLOBAL dib = CreateDibV5(image);
    if (dib == nullptr) {
        return false;
    }
    if (!OpenClipboard(hwnd_)) {
        GlobalFree(dib);
        return false;
    }

    EmptyClipboard();
    const bool copied = SetClipboardData(CF_DIBV5, dib) != nullptr;
    CloseClipboard();
    if (!copied) {
        GlobalFree(dib);
    }
    return copied;
}

void EditorWindow::CopyToClipboardAndClose() {
    if (image_ == nullptr) {
        return;
    }
    const ImageData rendered = RenderDocument();
    if (CopyImageToClipboard(rendered)) {
        MessageBeep(MB_OK);
    }
    ShowWindow(hwnd_, SW_HIDE);
}

void EditorWindow::SaveImage() {
    if (image_ == nullptr) {
        return;
    }
    ImageData rendered = RenderDocument();
    std::wstring path = image_->savedPath;
    if (path.empty()) {
        path = (util::DefaultSaveFolder() / util::TimestampedFileName(rendered)).wstring();
    }
    if (imageIo_.SavePng(rendered, path)) {
        image_->savedPath = path;
        MessageBeep(MB_OK);
    }
}

bool EditorWindow::IsToolButton(UINT controlId) const {
    return controlId == ControlPen || controlId == ControlHighlighter || controlId == ControlErase ||
           controlId == ControlEraseBrush || controlId == ControlEyedropper;
}

void EditorWindow::PaintButton(const DRAWITEMSTRUCT& drawItem) const {
    const UINT controlId = drawItem.CtlID;
    const bool enabled = (drawItem.itemState & ODS_DISABLED) == 0;
    const bool pressed = (drawItem.itemState & ODS_SELECTED) != 0;
    const bool windowButton = controlId == ControlMinimize || controlId == ControlMaximize || controlId == ControlClose;
    bool active = false;
    if (controlId == ControlPen) {
        active = activeTool_ == ActiveTool::Pen;
    } else if (controlId == ControlHighlighter) {
        active = activeTool_ == ActiveTool::Highlighter;
    } else if (controlId == ControlErase) {
        active = activeTool_ == ActiveTool::EraseLine;
    } else if (controlId == ControlEraseBrush) {
        active = activeTool_ == ActiveTool::EraseBrush;
    } else if (controlId == ControlEyedropper) {
        active = activeTool_ == ActiveTool::Eyedropper;
    }

    COLORREF fill = windowButton ? kHeaderColor : kCardColor;
    COLORREF border = windowButton ? kHeaderColor : kBorderColor;
    COLORREF text = kTitleColor;
    int cornerRadius = windowButton ? 12 : 18;

    if (!enabled) {
        fill = windowButton ? kHeaderColor : RGB(242, 245, 248);
        border = windowButton ? kHeaderColor : RGB(226, 232, 240);
        text = RGB(148, 163, 184);
    } else if (windowButton) {
        if (controlId == ControlClose && pressed) {
            fill = RGB(220, 38, 38);
            border = fill;
            text = RGB(255, 255, 255);
        } else if (pressed) {
            fill = RGB(230, 235, 244);
            border = fill;
        } else {
            fill = kHeaderColor;
            border = kHeaderColor;
        }
    } else if (controlId == ControlSave) {
        fill = pressed ? kAccentPressedColor : kAccentColor;
        border = fill;
        text = RGB(255, 255, 255);
    } else if (active) {
        fill = RGB(225, 236, 255);
        border = RGB(110, 159, 245);
        text = RGB(25, 73, 184);
    } else if (pressed) {
        fill = RGB(232, 238, 245);
        border = RGB(191, 201, 214);
    }

    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(drawItem.hDC, brush);
    HGDIOBJ oldPen = SelectObject(drawItem.hDC, pen);
    RoundRect(drawItem.hDC, drawItem.rcItem.left, drawItem.rcItem.top, drawItem.rcItem.right, drawItem.rcItem.bottom, cornerRadius, cornerRadius);
    SelectObject(drawItem.hDC, oldBrush);
    SelectObject(drawItem.hDC, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);

    Gdiplus::Graphics graphics(drawItem.hDC);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::RectF iconRect(
        static_cast<Gdiplus::REAL>(drawItem.rcItem.left + 13),
        static_cast<Gdiplus::REAL>(drawItem.rcItem.top + (drawItem.rcItem.bottom - drawItem.rcItem.top - 18) / 2),
        18.0f,
        18.0f);
    if (windowButton) {
        iconRect = Gdiplus::RectF(
            static_cast<Gdiplus::REAL>(drawItem.rcItem.left + (drawItem.rcItem.right - drawItem.rcItem.left - 14) / 2),
            static_cast<Gdiplus::REAL>(drawItem.rcItem.top + (drawItem.rcItem.bottom - drawItem.rcItem.top - 14) / 2),
            14.0f,
            14.0f);
    }
    if (!(controlId == ControlMaximize && IsZoomed(hwnd_))) {
        DrawButtonGlyph(graphics, controlId, iconRect, text);
    }

    if (controlId == ControlMaximize && IsZoomed(hwnd_)) {
        Gdiplus::Pen restorePen(ToGdiColor(text), 2.0f);
        restorePen.SetLineJoin(Gdiplus::LineJoinRound);
        const Gdiplus::RectF restoreRect(iconRect.X + 2.0f, iconRect.Y + 2.0f, iconRect.Width - 4.0f, iconRect.Height - 4.0f);
        graphics.DrawRectangle(&restorePen, restoreRect.X - 2.0f, restoreRect.Y + 2.0f, restoreRect.Width, restoreRect.Height);
        graphics.DrawRectangle(&restorePen, restoreRect.X + 1.0f, restoreRect.Y - 1.0f, restoreRect.Width, restoreRect.Height);
    }

    if (windowButton) {
        return;
    }

    SetBkMode(drawItem.hDC, TRANSPARENT);
    SetTextColor(drawItem.hDC, text);
    SelectObject(drawItem.hDC, uiFont_);
    RECT textRect = drawItem.rcItem;
    textRect.left += 38;
    DrawTextW(drawItem.hDC, ReadWindowText(drawItem.hwndItem).c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void EditorWindow::Paint() {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd_, &ps);
    const int paintWidth = std::max(1L, ps.rcPaint.right - ps.rcPaint.left);
    const int paintHeight = std::max(1L, ps.rcPaint.bottom - ps.rcPaint.top);

    HDC backDc = CreateCompatibleDC(hdc);
    HBITMAP backBitmap = CreateCompatibleBitmap(hdc, paintWidth, paintHeight);
    HGDIOBJ oldBitmap = SelectObject(backDc, backBitmap);
    SetViewportOrgEx(backDc, -ps.rcPaint.left, -ps.rcPaint.top, nullptr);

    RECT client{};
    GetClientRect(hwnd_, &client);
    const RECT paintRect{ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right, ps.rcPaint.bottom};
    const RECT paintLocal{ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right, ps.rcPaint.bottom};
    HBRUSH windowBrush = CreateSolidBrush(kWindowColor);
    FillRect(backDc, &paintLocal, windowBrush);
    DeleteObject(windowBrush);

    const RECT header = RectWithSize(0, 0, client.right, kTitleStripHeight);
    const RECT toolbar = RectWithSize(0, 0, client.right, kToolbarHeight);
    RECT toolbarBody = toolbar;
    toolbarBody.top = kTitleStripHeight;
    const RECT sidebar = SidebarRect();
    const RECT canvas = CanvasRect();

    const auto fillRect = [&](const RECT& rect, COLORREF color) {
        RECT clipped = util::IntersectRectSafe(rect, paintRect);
        if (util::IsRectEmptySafe(clipped)) {
            return;
        }
        HBRUSH brush = CreateSolidBrush(color);
        FillRect(backDc, &clipped, brush);
        DeleteObject(brush);
    };

    const auto drawRoundedCard = [&](const RECT& rect, COLORREF fill, COLORREF border, int radius) {
        RECT clipped = util::IntersectRectSafe(rect, paintRect);
        if (util::IsRectEmptySafe(clipped)) {
            return;
        }
        HBRUSH brush = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, border);
        HGDIOBJ oldBrush = SelectObject(backDc, brush);
        HGDIOBJ oldPen = SelectObject(backDc, pen);
        RoundRect(backDc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
        SelectObject(backDc, oldBrush);
        SelectObject(backDc, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);
    };

    fillRect(header, kHeaderColor);
    fillRect(toolbarBody, kToolbarColor);
    fillRect(sidebar, kSidebarColor);

    fillRect(RectWithSize(0, kTitleStripHeight - 1, client.right, 1), kBorderColor);
    fillRect(RectWithSize(0, kToolbarHeight - 1, client.right, 1), kBorderColor);
    fillRect(RectWithSize(sidebar.left, kToolbarHeight, 1, client.bottom - kToolbarHeight), kBorderColor);

    Gdiplus::Graphics graphics(backDc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    RECT canvasPaint = util::IntersectRectSafe(canvas, paintRect);
    if (!util::IsRectEmptySafe(canvasPaint)) {
        Gdiplus::SolidBrush canvasBrush(ToGdiColor(kCanvasColor));
        graphics.FillRectangle(&canvasBrush,
            static_cast<Gdiplus::REAL>(canvas.left),
            static_cast<Gdiplus::REAL>(canvas.top),
            static_cast<Gdiplus::REAL>(canvas.right - canvas.left),
            static_cast<Gdiplus::REAL>(canvas.bottom - canvas.top));

        Gdiplus::SolidBrush dotBrush(ToGdiColor(kCanvasStripeColor));
        const LONG dotStep = 28;
        const LONG dotStartY = canvas.top + ((std::max(canvas.top, paintRect.top) - canvas.top) / dotStep) * dotStep;
        const LONG dotStartX = canvas.left + ((std::max(canvas.left, paintRect.left) - canvas.left) / dotStep) * dotStep;
        for (LONG y = dotStartY; y < canvas.bottom; y += dotStep) {
            for (LONG x = dotStartX; x < canvas.right; x += dotStep) {
                graphics.FillEllipse(&dotBrush,
                    static_cast<Gdiplus::REAL>(x + 5),
                    static_cast<Gdiplus::REAL>(y + 5),
                    4.0f,
                    4.0f);
            }
        }

        Gdiplus::Pen canvasBorder(ToGdiColor(kCanvasFrameColor), 1.0f);
        graphics.DrawRectangle(&canvasBorder,
            static_cast<Gdiplus::REAL>(canvas.left),
            static_cast<Gdiplus::REAL>(canvas.top),
            static_cast<Gdiplus::REAL>(canvas.right - canvas.left),
            static_cast<Gdiplus::REAL>(canvas.bottom - canvas.top));
    }

    SelectObject(backDc, titleFont_);
    SetBkMode(backDc, TRANSPARENT);
    SetTextColor(backDc, kTitleColor);
    RECT headerTitleRect = RectWithSize(kOuterPadding, 7, 260, 24);
    DrawTextW(backDc, L"OneShot Editor", -1, &headerTitleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(backDc, hintFont_);
    SetTextColor(backDc, kBodyColor);
    RECT headerSubtitleRect = RectWithSize(kOuterPadding, 25, 460, 16);
    const wchar_t* subtitle = image_ != nullptr && image_->hdrSource ? L"Frozen HDR-aware capture with vector ink" : L"Frozen capture with vector ink";
    DrawTextW(backDc, subtitle, -1, &headerSubtitleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (image_ != nullptr) {
        SelectObject(backDc, uiFont_);
        SetTextColor(backDc, kMutedColor);
        RECT captureMetaRect = RectWithSize(kOuterPadding + 272, 13, 300, 18);
        const std::wstring captureMeta = util::FormatRectSize(image_->width, image_->height);
        DrawTextW(backDc, captureMeta.c_str(), -1, &captureMetaRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    if (image_ != nullptr) {
        const auto imageRect = ImageToViewRect();
        RECT imageBounds = ImageViewRect();
        RECT expandedImageBounds = imageBounds;
        InflateRect(&expandedImageBounds, 16, 18);
        if (!util::IsRectEmptySafe(util::IntersectRectSafe(expandedImageBounds, paintRect))) {
            Gdiplus::Bitmap bitmap(image_->width, image_->height, image_->width * 4, PixelFormat32bppARGB, const_cast<BYTE*>(image_->pixels.data()));
            Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(26, 15, 23, 42));
            graphics.FillRectangle(&shadowBrush, imageRect.X + 10.0f, imageRect.Y + 12.0f, imageRect.Width, imageRect.Height);
            graphics.DrawImage(&bitmap, imageRect);
            Gdiplus::Pen imageBorder(ToGdiColor(RGB(203, 213, 225)), 1.0f);
            graphics.DrawRectangle(&imageBorder, imageRect);

            if (!markupImage_.pixels.empty()) {
                Gdiplus::Bitmap markupBitmap(markupImage_.width, markupImage_.height, markupImage_.width * 4, PixelFormat32bppARGB,
                    const_cast<BYTE*>(markupImage_.pixels.data()));
                graphics.DrawImage(&markupBitmap, imageRect);
            }

            if (drawing_ && StrokeHasContent(inFlightStroke_)) {
                const SIZE imageSize{image_->width, image_->height};
                DrawStrokeOnView(graphics, inFlightStroke_, imageRect, imageSize, EffectiveStrokeWidth(inFlightStroke_));
            }
        }
    }

    SelectObject(backDc, uiFont_);
    SetBkMode(backDc, TRANSPARENT);
    SetTextColor(backDc, kMutedColor);
    RECT sliderRect = ChildToClientRect(hwnd_, widthSlider_);
    RECT sliderLabelRect = RectWithSize(kOuterPadding, kTitleStripHeight + 82, kSliderLabelWidth, 20);
    DrawTextW(backDc, L"Brush size", -1, &sliderLabelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (settings_ != nullptr) {
        const ActiveTool widthTool = ResolveWidthTool(activeTool_, inkTool_);
        const float width = widthTool == ActiveTool::Highlighter ? settings_->highlighterWidth : settings_->penWidth;
        const std::wstring widthLabel = FormatBrushWidthLabel(width);
        RECT badgeRect = RectWithSize(sliderRect.right + 12, kTitleStripHeight + 78, kSliderValueWidth, 30);
        HBRUSH badgeBrush = CreateSolidBrush(RGB(229, 236, 247));
        HPEN badgePen = CreatePen(PS_SOLID, 1, RGB(196, 208, 223));
        HGDIOBJ oldBrush = SelectObject(backDc, badgeBrush);
        HGDIOBJ oldPen = SelectObject(backDc, badgePen);
        RoundRect(backDc, badgeRect.left, badgeRect.top, badgeRect.right, badgeRect.bottom, 14, 14);
        SelectObject(backDc, oldBrush);
        SelectObject(backDc, oldPen);
        DeleteObject(badgeBrush);
        DeleteObject(badgePen);
        SetTextColor(backDc, kTitleColor);
        DrawTextW(backDc, widthLabel.c_str(), -1, &badgeRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    const RECT infoCard = RectWithSize(sidebar.left + 20, kToolbarHeight + 388, sidebar.right - sidebar.left - 40, 236);
    drawRoundedCard(infoCard, kCardColor, kBorderColor, 18);

    SetTextColor(backDc, kTitleColor);
    RECT panelTitleRect = RectWithSize(infoCard.left + 18, infoCard.top + 18, 180, 20);
    DrawTextW(backDc, L"Capture", -1, &panelTitleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (image_ != nullptr && settings_ != nullptr) {
        const std::wstring toolLabel =
            activeTool_ == ActiveTool::Highlighter ? L"Highlighter" :
            activeTool_ == ActiveTool::EraseLine ? L"Erase line" :
            activeTool_ == ActiveTool::EraseBrush ? L"Erase pen" :
            activeTool_ == ActiveTool::Eyedropper ? L"Eyedropper" :
            L"Pen";
        const COLORREF inkColor = inkTool_ == ActiveTool::Highlighter ? settings_->highlighterColor : settings_->penColor;
        const ActiveTool widthTool = ResolveWidthTool(activeTool_, inkTool_);
        const std::wstring widthValue = FormatBrushWidthLabel(widthTool == ActiveTool::Highlighter ? settings_->highlighterWidth : settings_->penWidth);

        SetTextColor(backDc, kMutedColor);
        RECT resolutionLabel = RectWithSize(infoCard.left + 18, infoCard.top + 54, 120, 18);
        RECT resolutionValue = RectWithSize(infoCard.left + 18, infoCard.top + 74, 168, 24);
        RECT toolLabelRect = RectWithSize(infoCard.left + 18, infoCard.top + 112, 120, 18);
        RECT toolValueRect = RectWithSize(infoCard.left + 18, infoCard.top + 132, 168, 24);
        RECT widthLabelRect = RectWithSize(infoCard.left + 18, infoCard.top + 170, 120, 18);
        RECT widthValueRect = RectWithSize(infoCard.left + 18, infoCard.top + 190, 168, 22);
        DrawTextW(backDc, L"Resolution", -1, &resolutionLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DrawTextW(backDc, L"Tool", -1, &toolLabelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DrawTextW(backDc, L"Brush", -1, &widthLabelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SetTextColor(backDc, kTitleColor);
        DrawTextW(backDc, util::FormatRectSize(image_->width, image_->height).c_str(), -1, &resolutionValue, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        DrawTextW(backDc, toolLabel.c_str(), -1, &toolValueRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        DrawTextW(backDc, widthValue.c_str(), -1, &widthValueRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT swatchRect = RectWithSize(infoCard.right - 112, infoCard.top + 54, 78, 78);
        HBRUSH swatchBrush = CreateSolidBrush(inkColor);
        HPEN swatchPen = CreatePen(PS_SOLID, 1, RGB(148, 163, 184));
        HGDIOBJ oldSwatchBrush = SelectObject(backDc, swatchBrush);
        HGDIOBJ oldSwatchPen = SelectObject(backDc, swatchPen);
        RoundRect(backDc, swatchRect.left, swatchRect.top, swatchRect.right, swatchRect.bottom, 18, 18);
        SelectObject(backDc, oldSwatchBrush);
        SelectObject(backDc, oldSwatchPen);
        DeleteObject(swatchBrush);
        DeleteObject(swatchPen);

        RECT hexRect = RectWithSize(infoCard.right - 126, infoCard.top + 142, 116, 18);
        SetTextColor(backDc, kMutedColor);
        DrawTextW(backDc, util::ColorToHex(inkColor).c_str(), -1, &hexRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (image_->hdrSource) {
            const RECT hdrBadge = RectWithSize(infoCard.right - 116, infoCard.top + 182, 92, 26);
            drawRoundedCard(hdrBadge, RGB(239, 246, 255), RGB(191, 219, 254), 14);
            SetTextColor(backDc, RGB(30, 64, 175));
            DrawTextW(backDc, L"HDR", -1, const_cast<RECT*>(&hdrBadge), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    SetViewportOrgEx(backDc, 0, 0, nullptr);
    BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top, paintWidth, paintHeight, backDc, 0, 0, SRCCOPY);
    SelectObject(backDc, oldBitmap);
    DeleteObject(backBitmap);
    DeleteDC(backDc);
    EndPaint(hwnd_, &ps);
}

LRESULT CALLBACK EditorWindow::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<EditorWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<EditorWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }
    return self != nullptr ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT EditorWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_GETMINMAXINFO: {
        auto* minmax = reinterpret_cast<MINMAXINFO*>(lParam);
        minmax->ptMinTrackSize.x = 1240;
        minmax->ptMinTrackSize.y = 760;
        return 0;
    }
    case WM_SIZE:
        LayoutControls();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DRAWITEM:
        PaintButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ControlPen:
            activeTool_ = ActiveTool::Pen;
            inkTool_ = ActiveTool::Pen;
            if (settings_ != nullptr) {
                colorPicker_->SetColor(settings_->penColor);
            }
            UpdateToolbarState();
            return 0;
        case ControlHighlighter:
            activeTool_ = ActiveTool::Highlighter;
            inkTool_ = ActiveTool::Highlighter;
            if (settings_ != nullptr) {
                colorPicker_->SetColor(settings_->highlighterColor);
            }
            UpdateToolbarState();
            return 0;
        case ControlErase:
            activeTool_ = ActiveTool::EraseLine;
            UpdateToolbarState();
            return 0;
        case ControlEraseBrush:
            activeTool_ = ActiveTool::EraseBrush;
            UpdateToolbarState();
            return 0;
        case ControlEyedropper:
            activeTool_ = ActiveTool::Eyedropper;
            UpdateToolbarState();
            return 0;
        case ControlUndo:
            if (!strokes_.empty()) {
                RECT dirty = StrokeDirtyRect(strokes_.back());
                redoStrokes_.push_back(strokes_.back());
                strokes_.pop_back();
                RebuildMarkupLayer();
                UpdateToolbarState();
                InvalidateCanvasRect(util::IsRectEmptySafe(dirty) ? ImageViewRect() : dirty);
            }
            return 0;
        case ControlRedo:
            if (!redoStrokes_.empty()) {
                strokes_.push_back(redoStrokes_.back());
                redoStrokes_.pop_back();
                RasterizeStroke(markupImage_, strokes_.back());
                UpdateToolbarState();
                InvalidateCanvasRect(StrokeDirtyRect(strokes_.back()));
            }
            return 0;
        case ControlSave:
            SaveImage();
            return 0;
        case ControlMinimize:
            ShowWindow(hwnd_, SW_MINIMIZE);
            return 0;
        case ControlMaximize:
            ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
            return 0;
        case ControlClose:
            ShowWindow(hwnd_, SW_HIDE);
            return 0;
        default:
            break;
        }
        break;
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) == widthSlider_) {
            UpdateWidthsFromSlider();
        }
        return 0;
    case WM_APP_COLOR_CHANGED:
        if (settings_ != nullptr) {
            if (inkTool_ == ActiveTool::Highlighter) {
                settings_->highlighterColor = static_cast<COLORREF>(lParam);
            } else {
                settings_->penColor = static_cast<COLORREF>(lParam);
            }
        }
        InvalidateSidebar();
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            ShowWindow(hwnd_, SW_HIDE);
            return 0;
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            if (wParam == 'C') {
                CopyToClipboardAndClose();
                return 0;
            }
            if (wParam == 'Z') {
                SendMessageW(hwnd_, WM_COMMAND, ControlUndo, 0);
                return 0;
            }
            if (wParam == 'Y') {
                SendMessageW(hwnd_, WM_COMMAND, ControlRedo, 0);
                return 0;
            }
        }
        break;
    case WM_NCHITTEST: {
        const LRESULT hit = DefWindowProcW(hwnd_, message, wParam, lParam);
        if (hit != HTCLIENT) {
            return hit;
        }
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd_, &point);
        if (point.y < kTitleStripHeight) {
            const RECT minimizeRect = ChildToClientRect(hwnd_, minimizeButton_);
            const RECT maximizeRect = ChildToClientRect(hwnd_, maximizeButton_);
            const RECT closeRect = ChildToClientRect(hwnd_, closeButton_);
            if (!PtInRect(&minimizeRect, point) && !PtInRect(&maximizeRect, point) && !PtInRect(&closeRect, point)) {
                return HTCAPTION;
            }
        }
        return HTCLIENT;
    }
    case WM_LBUTTONDOWN: {
        const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const auto imagePoint = ClientToImage(point);
        if ((activeTool_ == ActiveTool::Pen || activeTool_ == ActiveTool::Highlighter || activeTool_ == ActiveTool::EraseBrush) && imagePoint.has_value()) {
            SetCapture(hwnd_);
            BeginStroke(*imagePoint);
            return 0;
        }
        if (activeTool_ == ActiveTool::EraseLine && imagePoint.has_value()) {
            SetCapture(hwnd_);
            EraseStrokeAt(*imagePoint);
            return 0;
        }
        if (activeTool_ == ActiveTool::Eyedropper && imagePoint.has_value()) {
            SetCapture(hwnd_);
            PickColorAt(*imagePoint);
            return 0;
        }
        break;
    }
    case WM_MOUSEMOVE:
        if (drawing_) {
            const auto imagePoint = ClientToImage({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            const size_t startIndex = inFlightStroke_.points.size() > 2 ? inFlightStroke_.points.size() - 2 : 0;
            RECT dirty = StrokeDirtyRect(inFlightStroke_, startIndex);
            if (imagePoint.has_value()) {
                ExtendStroke(*imagePoint);
            } else {
                BreakStroke();
            }
            dirty = UnionRectSafe(dirty, StrokeDirtyRect(inFlightStroke_, startIndex));
            InvalidateCanvasRect(dirty);
            return 0;
        }
        if (activeTool_ == ActiveTool::EraseLine && GetCapture() == hwnd_ && (wParam & MK_LBUTTON) != 0U) {
            const auto imagePoint = ClientToImage({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            if (imagePoint.has_value()) {
                EraseStrokeAt(*imagePoint);
            }
            return 0;
        }
        if (activeTool_ == ActiveTool::Eyedropper && GetCapture() == hwnd_ && (wParam & MK_LBUTTON) != 0U) {
            const auto imagePoint = ClientToImage({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            if (imagePoint.has_value()) {
                PickColorAt(*imagePoint);
            }
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (drawing_) {
            const auto imagePoint = ClientToImage({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            if (imagePoint.has_value()) {
                ExtendStroke(*imagePoint);
            } else {
                BreakStroke();
            }
            ReleaseCapture();
            EndStroke();
            return 0;
        }
        if ((activeTool_ == ActiveTool::EraseLine || activeTool_ == ActiveTool::Eyedropper) && GetCapture() == hwnd_) {
            ReleaseCapture();
            if (activeTool_ == ActiveTool::Eyedropper) {
                activeTool_ = inkTool_;
                UpdateToolbarState();
            }
            return 0;
        }
        break;
    case WM_CLOSE:
        ShowWindow(hwnd_, SW_HIDE);
        return 0;
    case WM_DESTROY:
        hwnd_ = nullptr;
        return 0;
    case WM_PAINT:
        Paint();
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}
