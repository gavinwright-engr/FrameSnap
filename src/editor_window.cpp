#include "editor_window.h"

#include "util.h"

namespace {

constexpr wchar_t kEditorClassName[] = L"OneShotEditorWindow";
constexpr int kToolbarHeight = 88;
constexpr int kSidebarWidth = 292;
constexpr int kOuterPadding = 18;
constexpr int kButtonHeight = 46;
constexpr int kButtonGap = 10;
constexpr COLORREF kWindowColor = RGB(238, 242, 247);
constexpr COLORREF kToolbarColor = RGB(248, 250, 253);
constexpr COLORREF kSidebarColor = RGB(246, 249, 252);
constexpr COLORREF kCanvasColor = RGB(228, 233, 240);
constexpr COLORREF kCanvasStripeColor = RGB(220, 226, 235);
constexpr COLORREF kCanvasFrameColor = RGB(205, 214, 226);
constexpr COLORREF kCardColor = RGB(255, 255, 255);
constexpr COLORREF kAccentColor = RGB(29, 78, 216);
constexpr COLORREF kAccentPressedColor = RGB(21, 66, 190);
constexpr COLORREF kBorderColor = RGB(214, 223, 234);
constexpr COLORREF kTitleColor = RGB(15, 23, 42);
constexpr COLORREF kBodyColor = RGB(71, 85, 105);
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

RECT RectWithSize(LONG left, LONG top, LONG width, LONG height) {
    return {left, top, left + width, top + height};
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

void DrawStrokeGeometry(Gdiplus::Graphics& graphics, std::span<const Gdiplus::PointF> points, const Stroke& stroke, float width) {
    if (points.empty()) {
        return;
    }

    const BYTE alpha = stroke.tool == ActiveTool::Highlighter ? 104 : stroke.tool == ActiveTool::EraseBrush ? 220 : 255;
    if (points.size() == 1) {
        Gdiplus::SolidBrush brush(ToGdiColor(stroke.color, alpha));
        const float radius = std::max(1.0f, width * 0.5f);
        graphics.FillEllipse(&brush, points[0].X - radius, points[0].Y - radius, radius * 2.0f, radius * 2.0f);
        return;
    }

    Gdiplus::Pen pen(ToGdiColor(stroke.color, alpha), std::max(1.0f, width));
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);

    Gdiplus::GraphicsPath path;
    if (points.size() == 2) {
        path.AddLine(points[0], points[1]);
    } else {
        path.AddCurve(points.data(), static_cast<INT>(points.size()), 0.35f);
    }
    graphics.DrawPath(&pen, &path);
}

void DrawStrokeOnView(Gdiplus::Graphics& graphics, const Stroke& stroke, const Gdiplus::RectF& imageRect, const SIZE& imageSize) {
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
        DrawStrokeGeometry(graphics, segment, stroke, std::max(1.0f, stroke.width * scale));
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

void DrawStrokeNative(Gdiplus::Graphics& graphics, const Stroke& stroke) {
    std::vector<Gdiplus::PointF> segment;
    segment.reserve(stroke.points.size());
    const auto flush = [&]() {
        if (segment.empty()) {
            return;
        }
        DrawStrokeGeometry(graphics, segment, stroke, std::max(1.0f, stroke.width));
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
        0,
        kEditorClassName,
        L"OneShot Editor",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1280,
        880,
        nullptr,
        nullptr,
        instance_,
        this);
    if (hwnd_ == nullptr) {
        return false;
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
    widthSlider_ = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_DOWNISLEFT, 0, 0, 0, 0, hwnd_,
        reinterpret_cast<HMENU>(ControlWidth), instance_, nullptr);
    SendMessageW(widthSlider_, TBM_SETRANGE, TRUE, MAKELPARAM(1, 48));
    SendMessageW(widthSlider_, TBM_SETTICFREQ, 6, 0);
    SendMessageW(widthSlider_, TBM_SETPAGESIZE, 0, 4);

    colorPicker_ = std::make_unique<ColorPickerControl>(instance_, hwnd_, ControlColor);
    colorPicker_->Create(0, 0, 220, 310);

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
    if (widthSlider_ != nullptr) {
        SendMessageW(widthSlider_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
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
    colorPicker_->SetColor(settings.penColor);
    SetWindowTextW(hwnd_, BuildEditorTitle(*image).c_str());
    UpdateToolbarState();
    LayoutControls();
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void EditorWindow::LayoutControls() {
    if (hwnd_ == nullptr || colorPicker_ == nullptr) {
        return;
    }

    const RECT sidebar = SidebarRect();
    const int contentRight = sidebar.left - kOuterPadding;

    int x = kOuterPadding;
    const int buttonTop = 18;
    MoveWindow(penButton_, x, buttonTop, 100, kButtonHeight, TRUE);
    x += 100 + kButtonGap;
    MoveWindow(highlighterButton_, x, buttonTop, 132, kButtonHeight, TRUE);
    x += 132 + kButtonGap;
    MoveWindow(eraseButton_, x, buttonTop, 122, kButtonHeight, TRUE);
    x += 122 + kButtonGap;
    MoveWindow(eraseBrushButton_, x, buttonTop, 120, kButtonHeight, TRUE);
    x += 120 + kButtonGap;
    MoveWindow(eyedropperButton_, x, buttonTop, 122, kButtonHeight, TRUE);
    x += 122 + kButtonGap;

    const int saveWidth = 122;
    const int actionWidth = 82;
    const int saveX = contentRight - saveWidth;
    const int redoX = saveX - kButtonGap - actionWidth;
    const int undoX = redoX - kButtonGap - actionWidth;
    const int sliderX = x;
    const int sliderWidth = std::max(160, undoX - sliderX - kButtonGap);

    MoveWindow(widthSlider_, sliderX, buttonTop + 7, sliderWidth, 28, TRUE);
    MoveWindow(undoButton_, undoX, buttonTop, actionWidth, kButtonHeight, TRUE);
    MoveWindow(redoButton_, redoX, buttonTop, actionWidth, kButtonHeight, TRUE);
    MoveWindow(saveButton_, saveX, buttonTop, saveWidth, kButtonHeight, TRUE);

    colorPicker_->Move(sidebar.left + 18, kToolbarHeight + 18, sidebar.right - sidebar.left - 36, 320);
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
    const float x = canvas.left + (canvasWidth - width) / 2.0f;
    const float y = canvas.top + (canvasHeight - height) / 2.0f;
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

void EditorWindow::InvalidateCanvas(bool erase) const {
    if (hwnd_ == nullptr) {
        return;
    }
    RECT dirty = ImageViewRect();
    if (util::IsRectEmptySafe(dirty)) {
        dirty = CanvasRect();
    }
    InflateRect(&dirty, 72, 72);
    const RECT canvas = CanvasRect();
    dirty = util::IntersectRectSafe(dirty, canvas);
    InvalidateRect(hwnd_, util::IsRectEmptySafe(dirty) ? &canvas : &dirty, erase);
}

void EditorWindow::InvalidateSidebar() const {
    if (hwnd_ == nullptr) {
        return;
    }
    const RECT sidebar = SidebarRect();
    InvalidateRect(hwnd_, &sidebar, FALSE);
}

void EditorWindow::RefreshButtons() const {
    const std::array<HWND, 8> buttons{
        penButton_,
        highlighterButton_,
        eraseButton_,
        eraseBrushButton_,
        eyedropperButton_,
        undoButton_,
        redoButton_,
        saveButton_,
    };
    for (const HWND button : buttons) {
        if (button != nullptr) {
            InvalidateRect(button, nullptr, TRUE);
        }
    }
}

void EditorWindow::BeginStroke(FloatPoint point) {
    if (settings_ == nullptr) {
        return;
    }
    drawing_ = true;
    inFlightStroke_ = {};
    inFlightStroke_.tool = activeTool_;
    inFlightStroke_.color = activeTool_ == ActiveTool::Highlighter ? settings_->highlighterColor : settings_->penColor;
    inFlightStroke_.width = activeTool_ == ActiveTool::Highlighter ? settings_->highlighterWidth : settings_->penWidth;
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
        if ((dx * dx + dy * dy) < 0.12f) {
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
        strokes_.push_back(inFlightStroke_);
        redoStrokes_.clear();
    }
    inFlightStroke_ = {};
    UpdateToolbarState();
    InvalidateCanvas(false);
}

void EditorWindow::EraseStrokeAt(FloatPoint point) {
    for (auto it = strokes_.rbegin(); it != strokes_.rend(); ++it) {
        const float tolerance = std::max(6.0f, it->width * 1.15f);
        for (size_t i = 1; i < it->points.size(); ++i) {
            if (IsStrokeBreakPoint(it->points[i - 1]) || IsStrokeBreakPoint(it->points[i])) {
                continue;
            }
            if (DistanceToSegment(point, it->points[i - 1], it->points[i]) <= tolerance) {
                strokes_.erase(std::next(it).base());
                redoStrokes_.clear();
                UpdateToolbarState();
                InvalidateCanvas(false);
                return;
            }
        }
    }
}

void EditorWindow::EraseBrushAt(FloatPoint point) {
    bool changed = false;
    constexpr float minimumRadius = 6.0f;
    const float radius = std::max(minimumRadius, settings_ != nullptr ? settings_->penWidth * 1.35f : minimumRadius);

    for (auto strokeIt = strokes_.begin(); strokeIt != strokes_.end();) {
        bool strokeChanged = false;
        std::vector<FloatPoint> rebuilt;
        rebuilt.reserve(strokeIt->points.size());

        auto flushBreak = [&]() {
            if (!rebuilt.empty() && !IsStrokeBreakPoint(rebuilt.back())) {
                rebuilt.push_back(kStrokeBreakPoint);
            }
        };

        for (const auto& pointValue : strokeIt->points) {
            if (IsStrokeBreakPoint(pointValue)) {
                flushBreak();
                continue;
            }
            const float dx = pointValue.x - point.x;
            const float dy = pointValue.y - point.y;
            if ((dx * dx + dy * dy) <= radius * radius) {
                flushBreak();
                strokeChanged = true;
                continue;
            }
            rebuilt.push_back(pointValue);
        }

        while (!rebuilt.empty() && IsStrokeBreakPoint(rebuilt.back())) {
            rebuilt.pop_back();
        }

        if (strokeChanged) {
            strokeIt->points = std::move(rebuilt);
            changed = true;
            if (!StrokeHasContent(*strokeIt)) {
                strokeIt = strokes_.erase(strokeIt);
                continue;
            }
        }
        ++strokeIt;
    }

    if (changed) {
        redoStrokes_.clear();
        UpdateToolbarState();
        InvalidateCanvas(false);
    }
}

void EditorWindow::PickColorAt(FloatPoint point) {
    if (image_ == nullptr || settings_ == nullptr) {
        return;
    }
    const int x = util::Clamp(static_cast<int>(std::round(point.x)), 0, image_->width - 1);
    const int y = util::Clamp(static_cast<int>(std::round(point.y)), 0, image_->height - 1);
    const auto index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image_->width) + static_cast<std::size_t>(x)) * 4U;
    const COLORREF color = RGB(image_->pixels[index + 2], image_->pixels[index + 1], image_->pixels[index + 0]);
    if (inkTool_ == ActiveTool::Highlighter) {
        settings_->highlighterColor = color;
    } else {
        settings_->penColor = color;
    }
    colorPicker_->SetColor(color);
    activeTool_ = inkTool_;
    UpdateToolbarState();
    InvalidateSidebar();
}

void EditorWindow::UpdateToolbarState() {
    if (settings_ == nullptr || widthSlider_ == nullptr) {
        return;
    }

    ActiveTool widthTool = activeTool_;
    if (widthTool != ActiveTool::Pen && widthTool != ActiveTool::Highlighter) {
        widthTool = inkTool_;
    }

    const int value = widthTool == ActiveTool::Highlighter ? static_cast<int>(std::round(settings_->highlighterWidth))
                                                           : static_cast<int>(std::round(settings_->penWidth));
    SendMessageW(widthSlider_, TBM_SETPOS, TRUE, value);
    EnableWindow(widthSlider_, activeTool_ != ActiveTool::EraseLine && activeTool_ != ActiveTool::Eyedropper);
    EnableWindow(redoButton_, !redoStrokes_.empty());
    EnableWindow(undoButton_, !strokes_.empty());
    RefreshButtons();
    InvalidateSidebar();
}

void EditorWindow::UpdateWidthsFromSlider() {
    if (settings_ == nullptr) {
        return;
    }

    const float width = static_cast<float>(SendMessageW(widthSlider_, TBM_GETPOS, 0, 0));
    ActiveTool widthTool = activeTool_;
    if (widthTool != ActiveTool::Pen && widthTool != ActiveTool::Highlighter) {
        widthTool = inkTool_;
    }
    if (widthTool == ActiveTool::Highlighter) {
        settings_->highlighterWidth = width;
        inkTool_ = ActiveTool::Highlighter;
    } else {
        settings_->penWidth = width;
        inkTool_ = ActiveTool::Pen;
    }
    InvalidateSidebar();
}

ImageData EditorWindow::RenderDocument() const {
    ImageData rendered = *image_;
    Gdiplus::Bitmap bitmap(rendered.width, rendered.height, rendered.width * 4, PixelFormat32bppARGB, rendered.pixels.data());
    Gdiplus::Graphics graphics(&bitmap);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    for (const auto& stroke : strokes_) {
        DrawStrokeNative(graphics, stroke);
    }
    if (drawing_ && !inFlightStroke_.points.empty()) {
        DrawStrokeNative(graphics, inFlightStroke_);
    }
    return rendered;
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

    COLORREF fill = kCardColor;
    COLORREF border = kBorderColor;
    COLORREF text = kTitleColor;
    if (!enabled) {
        fill = RGB(242, 245, 248);
        border = RGB(226, 232, 240);
        text = RGB(148, 163, 184);
    } else if (controlId == ControlSave) {
        fill = pressed ? kAccentPressedColor : kAccentColor;
        border = fill;
        text = RGB(255, 255, 255);
    } else if (active) {
        fill = RGB(219, 234, 254);
        border = RGB(96, 165, 250);
        text = RGB(30, 64, 175);
    } else if (pressed) {
        fill = RGB(232, 238, 245);
        border = RGB(191, 201, 214);
    } else if (drawItem.itemState & ODS_FOCUS) {
        fill = RGB(244, 247, 251);
    }

    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(drawItem.hDC, brush);
    HGDIOBJ oldPen = SelectObject(drawItem.hDC, pen);
    RoundRect(drawItem.hDC, drawItem.rcItem.left, drawItem.rcItem.top, drawItem.rcItem.right, drawItem.rcItem.bottom, 18, 18);
    SelectObject(drawItem.hDC, oldBrush);
    SelectObject(drawItem.hDC, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);

    SetBkMode(drawItem.hDC, TRANSPARENT);
    SetTextColor(drawItem.hDC, text);
    SelectObject(drawItem.hDC, uiFont_);
    RECT textRect = drawItem.rcItem;
    const std::wstring buttonText = ReadWindowText(drawItem.hwndItem);
    DrawTextW(drawItem.hDC, buttonText.c_str(), -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
    RECT paintLocal{ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right, ps.rcPaint.bottom};
    HBRUSH windowBrush = CreateSolidBrush(kWindowColor);
    FillRect(backDc, &paintLocal, windowBrush);
    DeleteObject(windowBrush);

    const RECT toolbar = RectWithSize(0, 0, client.right, kToolbarHeight);
    const RECT sidebar = SidebarRect();
    const RECT canvas = CanvasRect();

    HBRUSH toolbarBrush = CreateSolidBrush(kToolbarColor);
    FillRect(backDc, &toolbar, toolbarBrush);
    DeleteObject(toolbarBrush);
    HBRUSH sidebarBrush = CreateSolidBrush(kSidebarColor);
    FillRect(backDc, &sidebar, sidebarBrush);
    DeleteObject(sidebarBrush);

    Gdiplus::Graphics graphics(backDc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    Gdiplus::SolidBrush canvasBrush(ToGdiColor(kCanvasColor));
    graphics.FillRectangle(&canvasBrush,
        static_cast<Gdiplus::REAL>(canvas.left),
        static_cast<Gdiplus::REAL>(canvas.top),
        static_cast<Gdiplus::REAL>(canvas.right - canvas.left),
        static_cast<Gdiplus::REAL>(canvas.bottom - canvas.top));

    for (LONG y = canvas.top; y < canvas.bottom; y += 18) {
        const bool offset = ((y - canvas.top) / 18) % 2 == 0;
        RECT stripe{canvas.left + (offset ? 0 : 9), y, canvas.right, std::min<LONG>(canvas.bottom, y + 9)};
        HBRUSH stripeBrush = CreateSolidBrush(kCanvasStripeColor);
        FillRect(backDc, &stripe, stripeBrush);
        DeleteObject(stripeBrush);
    }

    Gdiplus::Pen canvasBorder(ToGdiColor(kCanvasFrameColor), 1.0f);
    graphics.DrawRectangle(&canvasBorder,
        static_cast<Gdiplus::REAL>(canvas.left),
        static_cast<Gdiplus::REAL>(canvas.top),
        static_cast<Gdiplus::REAL>(canvas.right - canvas.left),
        static_cast<Gdiplus::REAL>(canvas.bottom - canvas.top));

    if (image_ != nullptr) {
        Gdiplus::Bitmap bitmap(image_->width, image_->height, image_->width * 4, PixelFormat32bppARGB, const_cast<BYTE*>(image_->pixels.data()));
        const auto imageRect = ImageToViewRect();
        Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(36, 15, 23, 42));
        graphics.FillRectangle(&shadowBrush, imageRect.X + 8.0f, imageRect.Y + 10.0f, imageRect.Width, imageRect.Height);
        graphics.DrawImage(&bitmap, imageRect);
        Gdiplus::Pen imageBorder(ToGdiColor(RGB(203, 213, 225)), 1.0f);
        graphics.DrawRectangle(&imageBorder, imageRect);

        const SIZE imageSize{image_->width, image_->height};
        for (const auto& stroke : strokes_) {
            DrawStrokeOnView(graphics, stroke, imageRect, imageSize);
        }
        if (drawing_ && !inFlightStroke_.points.empty()) {
            DrawStrokeOnView(graphics, inFlightStroke_, imageRect, imageSize);
        }
    }

    SelectObject(backDc, uiFont_);
    SetBkMode(backDc, TRANSPARENT);
    SetTextColor(backDc, kMutedColor);
    RECT sliderRect{};
    if (widthSlider_ != nullptr) {
        GetWindowRect(widthSlider_, &sliderRect);
        MapWindowPoints(HWND_DESKTOP, hwnd_, reinterpret_cast<LPPOINT>(&sliderRect), 2);
    }
    RECT widthValueRect = RectWithSize(sliderRect.left, 56, 140, 18);
    if (settings_ != nullptr) {
        ActiveTool widthTool = activeTool_;
        if (widthTool != ActiveTool::Pen && widthTool != ActiveTool::Highlighter) {
            widthTool = inkTool_;
        }
        const float width = widthTool == ActiveTool::Highlighter ? settings_->highlighterWidth : settings_->penWidth;
        const std::wstring widthLabel = std::to_wstring(static_cast<int>(std::round(width))) + L" px";
        DrawTextW(backDc, widthLabel.c_str(), -1, &widthValueRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    const RECT infoCard = RectWithSize(sidebar.left + 16, kToolbarHeight + 350, sidebar.right - sidebar.left - 32, 188);
    HBRUSH cardBrush = CreateSolidBrush(kCardColor);
    HPEN cardPen = CreatePen(PS_SOLID, 1, kBorderColor);
    HGDIOBJ oldBrush = SelectObject(backDc, cardBrush);
    HGDIOBJ oldPen = SelectObject(backDc, cardPen);
    RoundRect(backDc, infoCard.left, infoCard.top, infoCard.right, infoCard.bottom, 18, 18);
    SelectObject(backDc, oldBrush);
    SelectObject(backDc, oldPen);
    DeleteObject(cardBrush);
    DeleteObject(cardPen);

    SelectObject(backDc, uiFont_);
    SetTextColor(backDc, kTitleColor);
    RECT panelTitleRect = RectWithSize(infoCard.left + 16, infoCard.top + 16, 160, 18);
    DrawTextW(backDc, L"Selection", -1, &panelTitleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (image_ != nullptr && settings_ != nullptr) {
        SelectObject(backDc, uiFont_);
        SetTextColor(backDc, kTitleColor);
        const std::wstring toolLabel =
            activeTool_ == ActiveTool::Highlighter ? L"Highlighter" :
            activeTool_ == ActiveTool::EraseLine ? L"Erase line" :
            activeTool_ == ActiveTool::EraseBrush ? L"Erase pen" :
            activeTool_ == ActiveTool::Eyedropper ? L"Eyedropper" :
            L"Pen";
        const COLORREF inkColor = inkTool_ == ActiveTool::Highlighter ? settings_->highlighterColor : settings_->penColor;
        RECT resolutionLabel = RectWithSize(infoCard.left + 16, infoCard.top + 46, 120, 18);
        RECT resolutionValue = RectWithSize(infoCard.left + 16, infoCard.top + 66, infoCard.right - infoCard.left - 32, 22);
        RECT toolLabelRect = RectWithSize(infoCard.left + 16, infoCard.top + 102, 120, 18);
        RECT toolValueRect = RectWithSize(infoCard.left + 16, infoCard.top + 122, 120, 22);
        DrawTextW(backDc, L"Resolution", -1, &resolutionLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DrawTextW(backDc, util::FormatRectSize(image_->width, image_->height).c_str(), -1, &resolutionValue, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DrawTextW(backDc, L"Tool", -1, &toolLabelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DrawTextW(backDc, toolLabel.c_str(), -1, &toolValueRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT swatchRect = RectWithSize(infoCard.right - 96, infoCard.top + 54, 60, 60);
        HBRUSH swatchBrush = CreateSolidBrush(inkColor);
        HPEN swatchPen = CreatePen(PS_SOLID, 1, RGB(148, 163, 184));
        HGDIOBJ oldSwatchBrush = SelectObject(backDc, swatchBrush);
        HGDIOBJ oldSwatchPen = SelectObject(backDc, swatchPen);
        RoundRect(backDc, swatchRect.left, swatchRect.top, swatchRect.right, swatchRect.bottom, 18, 18);
        SelectObject(backDc, oldSwatchBrush);
        SelectObject(backDc, oldSwatchPen);
        DeleteObject(swatchBrush);
        DeleteObject(swatchPen);

        RECT hexRect = RectWithSize(infoCard.right - 114, infoCard.top + 126, 104, 18);
        SetTextColor(backDc, kMutedColor);
        DrawTextW(backDc, util::ColorToHex(inkColor).c_str(), -1, &hexRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        if (image_->hdrSource) {
            RECT hdrRect = RectWithSize(infoCard.left + 16, infoCard.top + 150, 120, 18);
            DrawTextW(backDc, L"HDR source", -1, &hdrRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
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
        minmax->ptMinTrackSize.x = 1080;
        minmax->ptMinTrackSize.y = 720;
        return 0;
    }
    case WM_SIZE:
        LayoutControls();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_NCHITTEST: {
        const LRESULT hit = DefWindowProcW(hwnd_, message, wParam, lParam);
        if (hit != HTCLIENT) {
            return hit;
        }
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd_, &point);
        if (point.y >= 0 && point.y < kToolbarHeight) {
            HWND child = ChildWindowFromPointEx(hwnd_, point, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED);
            if (child == nullptr || child == hwnd_) {
                return HTCAPTION;
            }
        }
        return HTCLIENT;
    }
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
            break;
        case ControlHighlighter:
            activeTool_ = ActiveTool::Highlighter;
            inkTool_ = ActiveTool::Highlighter;
            if (settings_ != nullptr) {
                colorPicker_->SetColor(settings_->highlighterColor);
            }
            UpdateToolbarState();
            break;
        case ControlErase:
            activeTool_ = ActiveTool::EraseLine;
            UpdateToolbarState();
            break;
        case ControlEraseBrush:
            activeTool_ = ActiveTool::EraseBrush;
            UpdateToolbarState();
            break;
        case ControlEyedropper:
            activeTool_ = ActiveTool::Eyedropper;
            UpdateToolbarState();
            break;
        case ControlUndo:
            if (!strokes_.empty()) {
                redoStrokes_.push_back(strokes_.back());
                strokes_.pop_back();
                UpdateToolbarState();
                InvalidateCanvas(false);
            }
            break;
        case ControlRedo:
            if (!redoStrokes_.empty()) {
                strokes_.push_back(redoStrokes_.back());
                redoStrokes_.pop_back();
                UpdateToolbarState();
                InvalidateCanvas(false);
            }
            break;
        case ControlSave:
            SaveImage();
            break;
        default:
            break;
        }
        return 0;
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
        break;
    case WM_LBUTTONDOWN: {
        const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const auto imagePoint = ClientToImage(point);
        if ((activeTool_ == ActiveTool::Pen || activeTool_ == ActiveTool::Highlighter) && imagePoint.has_value()) {
            SetCapture(hwnd_);
            BeginStroke(*imagePoint);
            InvalidateCanvas(false);
            return 0;
        }
        if (activeTool_ == ActiveTool::EraseLine && imagePoint.has_value()) {
            SetCapture(hwnd_);
            EraseStrokeAt(*imagePoint);
            return 0;
        }
        if (activeTool_ == ActiveTool::EraseBrush && imagePoint.has_value()) {
            SetCapture(hwnd_);
            drawing_ = true;
            EraseBrushAt(*imagePoint);
            return 0;
        }
        if (activeTool_ == ActiveTool::Eyedropper && imagePoint.has_value()) {
            PickColorAt(*imagePoint);
            return 0;
        }
        break;
    }
    case WM_MOUSEMOVE:
        if (drawing_) {
            const auto imagePoint = ClientToImage({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            if (activeTool_ == ActiveTool::EraseBrush) {
                if (imagePoint.has_value()) {
                    EraseBrushAt(*imagePoint);
                }
                return 0;
            }
            if (imagePoint.has_value()) {
                ExtendStroke(*imagePoint);
            } else {
                BreakStroke();
            }
            InvalidateCanvas(false);
            return 0;
        }
        if ((wParam & MK_LBUTTON) != 0U && activeTool_ == ActiveTool::EraseLine) {
            const auto imagePoint = ClientToImage({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            if (imagePoint.has_value()) {
                EraseStrokeAt(*imagePoint);
            }
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (drawing_) {
            if (activeTool_ == ActiveTool::EraseBrush) {
                drawing_ = false;
                ReleaseCapture();
                return 0;
            }
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
        if (activeTool_ == ActiveTool::EraseLine) {
            ReleaseCapture();
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
