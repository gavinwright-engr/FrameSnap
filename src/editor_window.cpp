#include "editor_window.h"

#include "util.h"

#include <limits>
#include <numeric>

namespace {

constexpr wchar_t kEditorClassName[] = L"FrameSnapEditorWindow";
constexpr int kTitleStripHeight = 0;
constexpr int kToolbarHeight = 84;
constexpr int kSidebarWidth = 288;
constexpr int kOuterPadding = 18;
constexpr int kButtonHeight = 42;
constexpr int kButtonGap = 8;
constexpr int kSliderLabelWidth = 88;
constexpr int kSliderValueWidth = 84;
constexpr COLORREF kWindowColor = RGB(10, 14, 20);
constexpr COLORREF kHeaderColor = RGB(14, 19, 26);
constexpr COLORREF kToolbarColor = RGB(14, 19, 26);
constexpr COLORREF kSidebarColor = RGB(12, 17, 24);
constexpr COLORREF kCanvasColor = RGB(17, 23, 31);
constexpr COLORREF kCanvasStripeColor = RGB(24, 31, 42);
constexpr COLORREF kCanvasFrameColor = RGB(40, 50, 64);
constexpr COLORREF kCardColor = RGB(20, 27, 36);
constexpr COLORREF kAccentColor = RGB(59, 130, 246);
constexpr COLORREF kAccentPressedColor = RGB(37, 99, 235);
constexpr COLORREF kBorderColor = RGB(43, 55, 72);
constexpr COLORREF kTitleColor = RGB(232, 238, 247);
constexpr COLORREF kBodyColor = RGB(194, 203, 217);
constexpr COLORREF kMutedColor = RGB(135, 148, 166);
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

constexpr wchar_t kBrushSizeClassName[] = L"FrameSnapBrushSizeControl";
constexpr UINT WM_EDITOR_BRUSH_SET_VALUE = WM_APP + 60;
constexpr UINT WM_EDITOR_BRUSH_GET_VALUE = WM_APP + 61;
constexpr UINT WM_EDITOR_BRUSH_CHANGED = WM_APP + 62;
constexpr UINT WM_EDITOR_BRUSH_DRAG_END = WM_APP + 63;

struct BrushSizeControlState {
    HWND parent{};
    int minValue{20};
    int maxValue{960};
    int value{60};
    bool dragging{};
    POINT anchorScreen{};
    POINT previewAnchorClient{};
    int anchorValue{60};
};

std::wstring BuildEditorTitle(const ImageData& image) {
    std::wstring title = L"FrameSnap Editor - ";
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

std::wstring FormatBrushScaledValue(int scaledValue) {
    return FormatBrushWidthLabel(static_cast<float>(scaledValue) / 20.0f);
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

HFONT CreateIconFont(int height) {
    HFONT font = CreateFontW(
        -height,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH,
        L"Segoe Fluent Icons");
    if (font == nullptr) {
        font = CreateFontW(
            -height,
            0,
            0,
            0,
            FW_NORMAL,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Segoe MDL2 Assets");
    }
    return font;
}

const wchar_t* ButtonGlyph(UINT controlId) {
    switch (controlId) {
    case ControlPen:
        return L"\uED63";
    case ControlHighlighter:
        return L"\uE7E6";
    case ControlErase:
        return L"\uED60";
    case ControlEraseBrush:
        return L"\uED61";
    case ControlEyedropper:
        return L"\uEF3C";
    case ControlUndo:
        return L"\uE7A7";
    case ControlRedo:
        return L"\uE7A6";
    case ControlSave:
        return L"\uEA35";
    default:
        return L"";
    }
}

void NotifyBrushControlOwner(const BrushSizeControlState& state, HWND hwnd, UINT message) {
    POINT previewPoint = state.previewAnchorClient;
    if (!state.dragging) {
        GetCursorPos(&previewPoint);
        ScreenToClient(state.parent, &previewPoint);
    }
    SendMessageW(state.parent, message, static_cast<WPARAM>(state.value), MAKELPARAM(previewPoint.x, previewPoint.y));
    InvalidateRect(hwnd, nullptr, FALSE);
}

LRESULT CALLBACK BrushSizeWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<BrushSizeControlState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* parent = static_cast<HWND>(create->hwndParent);
        auto* controlState = new BrushSizeControlState{};
        controlState->parent = parent;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(controlState));
        return TRUE;
    }

    switch (message) {
    case WM_NCDESTROY:
        delete state;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, state != nullptr && state->dragging ? IDC_SIZEALL : IDC_HAND));
        return TRUE;
    case WM_LBUTTONDOWN:
        if (state != nullptr) {
            SetFocus(hwnd);
            SetCapture(hwnd);
            state->dragging = true;
            GetCursorPos(&state->anchorScreen);
            state->previewAnchorClient = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            MapWindowPoints(hwnd, state->parent, &state->previewAnchorClient, 1);
            state->anchorValue = state->value;
            NotifyBrushControlOwner(*state, hwnd, WM_EDITOR_BRUSH_CHANGED);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (state != nullptr && state->dragging) {
            POINT cursor{};
            GetCursorPos(&cursor);
            const int dx = cursor.x - state->anchorScreen.x;
            const int dy = state->anchorScreen.y - cursor.y;
            const int dominantDelta = std::abs(dx) >= std::abs(dy) ? dx : dy;
            const int delta = static_cast<int>(std::lround(dominantDelta * 0.65));
            const int nextValue = std::clamp(state->anchorValue + delta, state->minValue, state->maxValue);
            if (nextValue != state->value) {
                state->value = nextValue;
            }
            NotifyBrushControlOwner(*state, hwnd, WM_EDITOR_BRUSH_CHANGED);
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (state != nullptr) {
            const int wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            const int notches = wheelDelta / WHEEL_DELTA;
            if (notches != 0) {
                state->value = std::clamp(state->value + (notches * 10), state->minValue, state->maxValue);
                NotifyBrushControlOwner(*state, hwnd, WM_EDITOR_BRUSH_CHANGED);
            }
        }
        return 0;
    case WM_KEYDOWN:
        if (state != nullptr) {
            int step = 0;
            if (wParam == VK_LEFT || wParam == VK_DOWN) {
                step = -10;
            } else if (wParam == VK_RIGHT || wParam == VK_UP) {
                step = 10;
            }
            if (step != 0) {
                state->value = std::clamp(state->value + step, state->minValue, state->maxValue);
                NotifyBrushControlOwner(*state, hwnd, WM_EDITOR_BRUSH_CHANGED);
                return 0;
            }
        }
        break;
    case WM_LBUTTONUP:
        if (state != nullptr && state->dragging) {
            state->dragging = false;
            ReleaseCapture();
            NotifyBrushControlOwner(*state, hwnd, WM_EDITOR_BRUSH_DRAG_END);
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (state != nullptr && state->dragging) {
            state->dragging = false;
            NotifyBrushControlOwner(*state, hwnd, WM_EDITOR_BRUSH_DRAG_END);
        }
        return 0;
    case WM_EDITOR_BRUSH_SET_VALUE:
        if (state != nullptr) {
            state->value = std::clamp(static_cast<int>(wParam), state->minValue, state->maxValue);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_EDITOR_BRUSH_GET_VALUE:
        return state != nullptr ? static_cast<LRESULT>(state->value) : 60;
    case WM_PAINT:
        if (state != nullptr) {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);

            const int width = std::max(1L, ps.rcPaint.right - ps.rcPaint.left);
            const int height = std::max(1L, ps.rcPaint.bottom - ps.rcPaint.top);
            HDC backDc = CreateCompatibleDC(hdc);
            HBITMAP backBitmap = CreateCompatibleBitmap(hdc, width, height);
            HGDIOBJ oldBitmap = SelectObject(backDc, backBitmap);
            SetViewportOrgEx(backDc, -ps.rcPaint.left, -ps.rcPaint.top, nullptr);

            const COLORREF fill = state->dragging ? RGB(30, 58, 138) : RGB(20, 27, 36);
            const COLORREF border = state->dragging ? RGB(96, 165, 250) : RGB(52, 64, 82);
            HBRUSH fillBrush = CreateSolidBrush(fill);
            HPEN borderPen = CreatePen(PS_SOLID, 1, border);
            HGDIOBJ oldBrush = SelectObject(backDc, fillBrush);
            HGDIOBJ oldPen = SelectObject(backDc, borderPen);
            RoundRect(backDc, client.left, client.top, client.right, client.bottom, 16, 16);
            SelectObject(backDc, oldBrush);
            SelectObject(backDc, oldPen);
            DeleteObject(fillBrush);
            DeleteObject(borderPen);

            Gdiplus::Graphics graphics(backDc);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            const float brushWidth = static_cast<float>(state->value) / 20.0f;
            const float previewDiameter = std::clamp(brushWidth * 1.35f, 8.0f, 18.0f);
            const float previewX = 10.0f;
            const float previewY = ((client.bottom - client.top) - previewDiameter) * 0.5f + 4.0f;

            Gdiplus::SolidBrush previewFill(ToGdiColor(RGB(191, 219, 254), 155));
            Gdiplus::Pen previewPen(ToGdiColor(RGB(147, 197, 253)), 1.6f);
            graphics.FillEllipse(&previewFill, previewX, previewY, previewDiameter, previewDiameter);
            graphics.DrawEllipse(&previewPen, previewX, previewY, previewDiameter, previewDiameter);

            SetBkMode(backDc, TRANSPARENT);
            SelectObject(backDc, GetStockObject(DEFAULT_GUI_FONT));
            SetTextColor(backDc, RGB(148, 163, 184));
            RECT labelRect{28, 5, client.right - 6, 19};
            DrawTextW(backDc, L"Brush", -1, &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SetTextColor(backDc, state->dragging ? RGB(239, 246, 255) : RGB(232, 238, 247));
            RECT valueRect{28, 18, client.right - 8, client.bottom - 6};
            const std::wstring valueLabel = FormatBrushScaledValue(state->value);
            DrawTextW(backDc, valueLabel.c_str(), -1, &valueRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

            SetViewportOrgEx(backDc, 0, 0, nullptr);
            BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top, width, height, backDc, 0, 0, SRCCOPY);
            SelectObject(backDc, oldBitmap);
            DeleteObject(backBitmap);
            DeleteDC(backDc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        break;
    default:
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
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
    Gdiplus::Pen pen(ToGdiColor(color), 2.3f);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    Gdiplus::SolidBrush brush(ToGdiColor(color));
    Gdiplus::SolidBrush softBrush(ToGdiColor(color, 48));

    switch (controlId) {
    case ControlPen: {
        graphics.DrawLine(&pen, rect.X + 4.0f, rect.Y + rect.Height - 5.0f, rect.X + rect.Width - 7.0f, rect.Y + 5.5f);
        Gdiplus::PointF nib[]{
            {rect.X + rect.Width - 7.0f, rect.Y + 5.5f},
            {rect.X + rect.Width - 2.0f, rect.Y + 8.5f},
            {rect.X + rect.Width - 8.0f, rect.Y + 13.5f},
        };
        graphics.FillPolygon(&brush, nib, 3);
        graphics.DrawLine(&pen, rect.X + 2.5f, rect.Y + rect.Height - 2.0f, rect.X + 7.5f, rect.Y + rect.Height - 6.0f);
        break;
    }
    case ControlHighlighter: {
        Gdiplus::PointF body[]{
            {rect.X + 5.0f, rect.Y + rect.Height - 4.0f},
            {rect.X + 10.0f, rect.Y + rect.Height - 8.0f},
            {rect.X + rect.Width - 5.0f, rect.Y + 7.0f},
            {rect.X + rect.Width - 9.0f, rect.Y + 3.0f},
        };
        graphics.FillPolygon(&softBrush, body, 4);
        graphics.DrawPolygon(&pen, body, 4);
        graphics.DrawLine(&pen, rect.X + 6.0f, rect.Y + rect.Height - 4.5f, rect.X + rect.Width - 6.0f, rect.Y + 8.0f);
        graphics.FillRectangle(&brush, rect.X + 3.0f, rect.Y + rect.Height - 5.0f, rect.Width - 6.0f, 3.5f);
        break;
    }
    case ControlErase: {
        graphics.DrawArc(&pen, rect.X + 2.0f, rect.Y + 8.0f, rect.Width - 6.0f, rect.Height - 12.0f, 210.0f, 140.0f);
        Gdiplus::PointF eraser[]{
            {rect.X + rect.Width - 9.0f, rect.Y + 6.0f},
            {rect.X + rect.Width - 4.0f, rect.Y + 10.0f},
            {rect.X + rect.Width - 8.0f, rect.Y + 14.0f},
            {rect.X + rect.Width - 13.0f, rect.Y + 10.0f},
        };
        graphics.FillPolygon(&softBrush, eraser, 4);
        graphics.DrawPolygon(&pen, eraser, 4);
        break;
    }
    case ControlEraseBrush: {
        Gdiplus::PointF eraser[]{
            {rect.X + 5.0f, rect.Y + rect.Height - 4.0f},
            {rect.X + rect.Width - 4.0f, rect.Y + 7.0f},
            {rect.X + rect.Width - 8.0f, rect.Y + 3.0f},
            {rect.X + rect.Width - 14.0f, rect.Y + 9.0f},
            {rect.X + 9.0f, rect.Y + rect.Height - 3.0f},
        };
        graphics.FillPolygon(&softBrush, eraser, 5);
        graphics.DrawPolygon(&pen, eraser, 5);
        graphics.DrawLine(&pen, rect.X + 11.0f, rect.Y + rect.Height - 2.5f, rect.X + rect.Width - 8.0f, rect.Y + 8.5f);
        break;
    }
    case ControlEyedropper: {
        graphics.DrawLine(&pen, rect.X + 5.0f, rect.Y + rect.Height - 4.5f, rect.X + rect.Width - 7.0f, rect.Y + 6.0f);
        graphics.FillEllipse(&brush, rect.X + rect.Width - 10.0f, rect.Y + 2.5f, 7.5f, 7.5f);
        graphics.DrawEllipse(&pen, rect.X + 2.5f, rect.Y + rect.Height - 10.5f, 9.0f, 9.0f);
        graphics.DrawLine(&pen, rect.X + 4.0f, rect.Y + rect.Height - 3.0f, rect.X + 7.5f, rect.Y + rect.Height - 6.5f);
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
        graphics.DrawLine(&pen, rect.X + rect.Width * 0.5f, rect.Y + 4.0f, rect.X + rect.Width * 0.5f, rect.Y + rect.Height - 12.0f);
        graphics.DrawLine(&pen, rect.X + rect.Width * 0.5f, rect.Y + rect.Height - 12.0f, rect.X + rect.Width * 0.5f - 4.0f, rect.Y + rect.Height - 16.0f);
        graphics.DrawLine(&pen, rect.X + rect.Width * 0.5f, rect.Y + rect.Height - 12.0f, rect.X + rect.Width * 0.5f + 4.0f, rect.Y + rect.Height - 16.0f);
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
    if (iconFont_ != nullptr) {
        DeleteObject(iconFont_);
    }
    if (smallIcon_ != nullptr) {
        DestroyIcon(smallIcon_);
    }
    if (largeIcon_ != nullptr) {
        DestroyIcon(largeIcon_);
    }
}

HWND EditorWindow::Handle() const {
    return hwnd_;
}

bool EditorWindow::HandleAccelerator(const MSG& message) {
    if (hwnd_ == nullptr || !IsWindowVisible(hwnd_)) {
        return false;
    }
    if (message.message != WM_KEYDOWN && message.message != WM_SYSKEYDOWN) {
        return false;
    }
    if (message.hwnd != hwnd_ && !IsChild(hwnd_, message.hwnd)) {
        return false;
    }

    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    if (alt) {
        return false;
    }

    if (message.wParam == VK_ESCAPE) {
        HideBrushPreview();
        ShowWindow(hwnd_, SW_HIDE);
        return true;
    }

    if (!ctrl) {
        return false;
    }

    switch (message.wParam) {
    case 'C':
        CopyToClipboardAndClose();
        return true;
    case 'Y':
        SendMessageW(hwnd_, WM_COMMAND, ControlRedo, 0);
        return true;
    case 'Z':
        if (shift) {
            SendMessageW(hwnd_, WM_COMMAND, ControlRedo, 0);
        } else {
            SendMessageW(hwnd_, WM_COMMAND, ControlUndo, 0);
        }
        return true;
    default:
        return false;
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
    const BOOL darkMode = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
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

    WNDCLASSW brushWc{};
    brushWc.lpfnWndProc = BrushSizeWndProc;
    brushWc.hInstance = instance_;
    brushWc.hCursor = LoadCursorW(nullptr, IDC_SIZEALL);
    brushWc.hbrBackground = nullptr;
    brushWc.lpszClassName = kBrushSizeClassName;
    RegisterClassW(&brushWc);

    uiFont_ = CreateUiFont(15, FW_MEDIUM);
    titleFont_ = CreateUiFont(17, FW_SEMIBOLD);
    hintFont_ = CreateUiFont(13, FW_NORMAL);
    iconFont_ = CreateIconFont(18);

    hwnd_ = CreateWindowExW(
        WS_EX_APPWINDOW,
        kEditorClassName,
        L"FrameSnap Editor",
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

    smallIcon_ = util::CreateFrameSnapAppIcon(GetSystemMetrics(SM_CXSMICON));
    largeIcon_ = util::CreateFrameSnapAppIcon(GetSystemMetrics(SM_CXICON));
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
    widthSlider_ = CreateWindowW(kBrushSizeClassName, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_,
        reinterpret_cast<HMENU>(ControlWidth), instance_, nullptr);

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
    brushPreviewVisible_ = false;
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
    const int toolbarTop = 18;
    const int contentWidth = contentRight - kOuterPadding;
    const int brushBoxWidth = 74;
    const int brushBoxHeight = 42;

    enum class ToolbarMode {
        Full,
        Compact,
        IconOnly,
    };

    ToolbarMode mode = ToolbarMode::Full;
    int saveWidth = 120;
    int actionWidth = 92;
    std::array<int, 5> toolWidths{};
    std::array<const wchar_t*, 5> labels{};
    const wchar_t* undoLabel = L"Undo";
    const wchar_t* redoLabel = L"Redo";
    const wchar_t* saveLabel = L"Save copy";

    const auto configureMode = [&](ToolbarMode nextMode) {
        mode = nextMode;
        if (mode == ToolbarMode::Full) {
            saveWidth = 120;
            actionWidth = 92;
            toolWidths = {96, 132, 116, 114, 122};
            labels = {L"Pen", L"Highlighter", L"Erase line", L"Erase pen", L"Eyedropper"};
            undoLabel = L"Undo";
            redoLabel = L"Redo";
            saveLabel = L"Save copy";
        } else if (mode == ToolbarMode::Compact) {
            saveWidth = 104;
            actionWidth = 84;
            toolWidths = {82, 112, 94, 92, 100};
            labels = {L"Pen", L"Highlight", L"Line erase", L"Brush erase", L"Picker"};
            undoLabel = L"Undo";
            redoLabel = L"Redo";
            saveLabel = L"Save";
        } else {
            saveWidth = 48;
            actionWidth = 44;
            toolWidths = {44, 44, 44, 44, 44};
            labels = {L"", L"", L"", L"", L""};
            undoLabel = L"";
            redoLabel = L"";
            saveLabel = L"";
        }
    };

    const auto rowWidthForCurrentMode = [&]() {
        return std::accumulate(toolWidths.begin(), toolWidths.end(), 0) + brushBoxWidth + saveWidth + actionWidth * 2 + (kButtonGap * 8);
    };

    configureMode(ToolbarMode::Full);
    if (rowWidthForCurrentMode() > contentWidth) {
        configureMode(ToolbarMode::Compact);
    }
    if (rowWidthForCurrentMode() > contentWidth) {
        configureMode(ToolbarMode::IconOnly);
    }

    SetWindowTextW(penButton_, labels[0]);
    SetWindowTextW(highlighterButton_, labels[1]);
    SetWindowTextW(eraseButton_, labels[2]);
    SetWindowTextW(eraseBrushButton_, labels[3]);
    SetWindowTextW(eyedropperButton_, labels[4]);
    SetWindowTextW(undoButton_, undoLabel);
    SetWindowTextW(redoButton_, redoLabel);
    SetWindowTextW(saveButton_, saveLabel);

    const int adjustedSaveX = contentRight - saveWidth;
    const int adjustedRedoX = adjustedSaveX - kButtonGap - actionWidth;
    const int adjustedUndoX = adjustedRedoX - kButtonGap - actionWidth;

    HDWP defer = BeginDeferWindowPos(9);
    if (defer != nullptr) {
        int x = kOuterPadding;
        defer = DeferWindowPos(defer, penButton_, nullptr, x, toolbarTop, toolWidths[0], kButtonHeight, SWP_NOZORDER | SWP_NOACTIVATE);
        x += toolWidths[0] + kButtonGap;
        defer = DeferWindowPos(defer, highlighterButton_, nullptr, x, toolbarTop, toolWidths[1], kButtonHeight, SWP_NOZORDER | SWP_NOACTIVATE);
        x += toolWidths[1] + kButtonGap;
        defer = DeferWindowPos(defer, eraseButton_, nullptr, x, toolbarTop, toolWidths[2], kButtonHeight, SWP_NOZORDER | SWP_NOACTIVATE);
        x += toolWidths[2] + kButtonGap;
        defer = DeferWindowPos(defer, eraseBrushButton_, nullptr, x, toolbarTop, toolWidths[3], kButtonHeight, SWP_NOZORDER | SWP_NOACTIVATE);
        x += toolWidths[3] + kButtonGap;
        defer = DeferWindowPos(defer, eyedropperButton_, nullptr, x, toolbarTop, toolWidths[4], kButtonHeight, SWP_NOZORDER | SWP_NOACTIVATE);
        x += toolWidths[4] + kButtonGap;
        defer = DeferWindowPos(defer, widthSlider_, nullptr, x, toolbarTop, brushBoxWidth, brushBoxHeight, SWP_NOZORDER | SWP_NOACTIVATE);
        defer = DeferWindowPos(defer, undoButton_, nullptr, adjustedUndoX, toolbarTop, actionWidth, kButtonHeight, SWP_NOZORDER | SWP_NOACTIVATE);
        defer = DeferWindowPos(defer, redoButton_, nullptr, adjustedRedoX, toolbarTop, actionWidth, kButtonHeight, SWP_NOZORDER | SWP_NOACTIVATE);
        defer = DeferWindowPos(defer, saveButton_, nullptr, adjustedSaveX, toolbarTop, saveWidth, kButtonHeight, SWP_NOZORDER | SWP_NOACTIVATE);
        if (defer != nullptr) {
            EndDeferWindowPos(defer);
        }
    }

    colorPicker_->Move(
        static_cast<int>(sidebar.left + 18),
        kToolbarHeight + 18,
        static_cast<int>(sidebar.right - sidebar.left - 36),
        static_cast<int>(std::max<LONG>(240, client.bottom - kToolbarHeight - 36)));
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
    const float canvasWidth = static_cast<float>(std::max<LONG>(1, (canvas.right - canvas.left) - 32));
    const float canvasHeight = static_cast<float>(std::max<LONG>(1, (canvas.bottom - canvas.top) - 32));
    const float scale = std::min(canvasWidth / static_cast<float>(image_->width), canvasHeight / static_cast<float>(image_->height));
    const float width = image_->width * scale;
    const float height = image_->height * scale;
    const float x = canvas.left + 16.0f + (canvasWidth - width) * 0.5f;
    const float y = canvas.top + 16.0f + (canvasHeight - height) * 0.5f;
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
    const int value = widthTool == ActiveTool::Highlighter ? static_cast<int>(std::round(settings_->highlighterWidth * 20.0f))
                                                           : static_cast<int>(std::round(settings_->penWidth * 20.0f));
    SendMessageW(widthSlider_, WM_EDITOR_BRUSH_SET_VALUE, value, 0);
    EnableWindow(widthSlider_, activeTool_ != ActiveTool::EraseLine && activeTool_ != ActiveTool::Eyedropper);
    EnableWindow(undoButton_, !strokes_.empty());
    EnableWindow(redoButton_, !redoStrokes_.empty());
    RefreshButtons();
    InvalidateSidebar();
}

void EditorWindow::UpdateWidthFromScaledValue(int scaledValue) {
    if (settings_ == nullptr) {
        return;
    }

    const float width = static_cast<float>(scaledValue) / 20.0f;
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

float EditorWindow::PreviewStrokeDiameter(float width) const {
    if (image_ == nullptr || image_->width <= 0 || image_->height <= 0) {
        return std::max(10.0f, width * 2.0f);
    }

    Stroke previewStroke{};
    previewStroke.tool = ResolveWidthTool(activeTool_, inkTool_);
    previewStroke.width = width;
    const auto imageRect = ImageToViewRect();
    const float scale = imageRect.Width / static_cast<float>(std::max(1, image_->width));
    return std::max(10.0f, EffectiveStrokeWidth(previewStroke) * scale);
}

RECT EditorWindow::BrushPreviewRect(POINT point, float width) const {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const float diameter = PreviewStrokeDiameter(width);
    POINT center{point.x + 26, point.y - 28};
    if (center.y - static_cast<int>(diameter * 0.5f) < kToolbarHeight + 6) {
        center.y = point.y + 30;
    }
    center.x = std::clamp<int>(static_cast<int>(center.x), 24, std::max(24, static_cast<int>(client.right) - 24));
    center.y = std::clamp<int>(static_cast<int>(center.y), 24, std::max(24, static_cast<int>(client.bottom) - 24));

    const int radius = static_cast<int>(std::ceil(diameter * 0.5f + 6.0f));
    return {center.x - radius, center.y - radius, center.x + radius, center.y + radius};
}

void EditorWindow::UpdateBrushPreview(POINT point, float width) {
    RECT dirty = {};
    if (brushPreviewVisible_) {
        dirty = BrushPreviewRect(brushPreviewPoint_, brushPreviewWidth_);
    }
    brushPreviewVisible_ = true;
    brushPreviewPoint_ = point;
    brushPreviewWidth_ = width;
    dirty = UnionRectSafe(dirty, BrushPreviewRect(point, width));
    InvalidateRect(hwnd_, &dirty, FALSE);
}

void EditorWindow::HideBrushPreview() {
    if (!brushPreviewVisible_) {
        return;
    }
    const RECT dirty = BrushPreviewRect(brushPreviewPoint_, brushPreviewWidth_);
    brushPreviewVisible_ = false;
    InvalidateRect(hwnd_, &dirty, FALSE);
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
    const std::wstring label = ReadWindowText(drawItem.hwndItem);
    const bool iconOnly = label.empty();
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

    COLORREF fill = RGB(20, 27, 36);
    COLORREF border = RGB(52, 64, 82);
    COLORREF text = kBodyColor;
    int cornerRadius = 16;

    if (!enabled) {
        fill = RGB(17, 22, 30);
        border = RGB(33, 42, 55);
        text = RGB(94, 105, 122);
    } else if (controlId == ControlSave) {
        fill = pressed ? kAccentPressedColor : kAccentColor;
        border = fill;
        text = RGB(255, 255, 255);
    } else if (active) {
        fill = RGB(30, 58, 138);
        border = RGB(96, 165, 250);
        text = RGB(239, 246, 255);
    } else if (pressed) {
        fill = RGB(28, 36, 48);
        border = RGB(84, 96, 116);
        text = RGB(232, 238, 247);
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

    const wchar_t* glyph = ButtonGlyph(controlId);
    if (glyph[0] != L'\0') {
        SetBkMode(drawItem.hDC, TRANSPARENT);
        SetTextColor(drawItem.hDC, text);
        SelectObject(drawItem.hDC, iconFont_ != nullptr ? iconFont_ : uiFont_);
        RECT glyphRect = drawItem.rcItem;
        if (iconOnly) {
            DrawTextW(drawItem.hDC, glyph, -1, &glyphRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            glyphRect.left += 12;
            glyphRect.right = glyphRect.left + 24;
            DrawTextW(drawItem.hDC, glyph, -1, &glyphRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    if (iconOnly) {
        return;
    }

    SetBkMode(drawItem.hDC, TRANSPARENT);
    SetTextColor(drawItem.hDC, text);
    SelectObject(drawItem.hDC, uiFont_);
    RECT textRect = drawItem.rcItem;
    textRect.left += 42;
    DrawTextW(drawItem.hDC, label.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
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

    fillRect(RectWithSize(0, kToolbarHeight - 1, client.right, 1), kBorderColor);
    fillRect(RectWithSize(sidebar.left, kToolbarHeight, 1, client.bottom - kToolbarHeight), kBorderColor);

    Gdiplus::Graphics graphics(backDc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    graphics.SetInterpolationMode(sizing_ ? Gdiplus::InterpolationModeLowQuality : Gdiplus::InterpolationModeHighQualityBicubic);

    RECT canvasPaint = util::IntersectRectSafe(canvas, paintRect);
    if (!util::IsRectEmptySafe(canvasPaint)) {
        drawRoundedCard(canvas, kCanvasColor, kCanvasFrameColor, 22);
    }

    if (image_ != nullptr) {
        const auto imageRect = ImageToViewRect();
        RECT imageBounds = ImageViewRect();
        RECT expandedImageBounds = imageBounds;
        InflateRect(&expandedImageBounds, 16, 18);
        if (!util::IsRectEmptySafe(util::IntersectRectSafe(expandedImageBounds, paintRect))) {
            Gdiplus::Bitmap bitmap(image_->width, image_->height, image_->width * 4, PixelFormat32bppARGB, const_cast<BYTE*>(image_->pixels.data()));
            if (!sizing_) {
                Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(28, 0, 0, 0));
                graphics.FillRectangle(&shadowBrush, imageRect.X + 10.0f, imageRect.Y + 12.0f, imageRect.Width, imageRect.Height);
            }
            graphics.DrawImage(&bitmap, imageRect);
            Gdiplus::Pen imageBorder(ToGdiColor(RGB(71, 85, 105)), 1.0f);
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

    if (brushPreviewVisible_ && settings_ != nullptr) {
        const ActiveTool previewTool = ResolveWidthTool(activeTool_, inkTool_);
        const COLORREF previewColor = previewTool == ActiveTool::Highlighter ? settings_->highlighterColor : settings_->penColor;
        const float diameter = PreviewStrokeDiameter(brushPreviewWidth_);
        RECT rect = BrushPreviewRect(brushPreviewPoint_, brushPreviewWidth_);
        const float drawX = static_cast<float>(rect.left + 6);
        const float drawY = static_cast<float>(rect.top + 6);
        const float drawDiameter = diameter;
        Gdiplus::SolidBrush fillBrush(ToGdiColor(previewColor, previewTool == ActiveTool::Highlighter ? 72 : 40));
        Gdiplus::Pen outlinePen(ToGdiColor(RGB(255, 255, 255), 220), 2.0f);
        graphics.FillEllipse(&fillBrush, drawX, drawY, drawDiameter, drawDiameter);
        graphics.DrawEllipse(&outlinePen, drawX, drawY, drawDiameter, drawDiameter);
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
        minmax->ptMinTrackSize.x = 1100;
        minmax->ptMinTrackSize.y = 720;
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
        default:
            break;
        }
        break;
    case WM_EDITOR_BRUSH_CHANGED: {
        const int scaledValue = static_cast<int>(wParam);
        UpdateWidthFromScaledValue(scaledValue);
        UpdateBrushPreview({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}, static_cast<float>(scaledValue) / 20.0f);
        return 0;
    }
    case WM_EDITOR_BRUSH_DRAG_END:
        HideBrushPreview();
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
            HideBrushPreview();
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
    case WM_ENTERSIZEMOVE:
        sizing_ = true;
        return 0;
    case WM_EXITSIZEMOVE:
        sizing_ = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
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
        HideBrushPreview();
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
