#include "overlay_window.h"

#include "util.h"

namespace {

constexpr wchar_t kOverlayClassName[] = L"ScreenshotterOverlayWindow";
constexpr BYTE kOverlayAlpha = 175;

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

}  // namespace

OverlayWindow::OverlayWindow(HINSTANCE instance, HWND owner)
    : instance_(instance), owner_(owner) {}

HCURSOR OverlayWindow::CreateCaptureCursor() const {
    constexpr int size = 32;
    Gdiplus::Bitmap bitmap(size, size, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&bitmap);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

    Gdiplus::Pen outerPen(Gdiplus::Color(245, 15, 23, 42), 3.0f);
    Gdiplus::Pen innerPen(Gdiplus::Color(255, 255, 255, 255), 1.5f);
    outerPen.SetStartCap(Gdiplus::LineCapRound);
    outerPen.SetEndCap(Gdiplus::LineCapRound);
    innerPen.SetStartCap(Gdiplus::LineCapRound);
    innerPen.SetEndCap(Gdiplus::LineCapRound);

    const float center = 15.5f;
    graphics.DrawLine(&outerPen, center, 2.0f, center, 11.0f);
    graphics.DrawLine(&outerPen, center, 20.0f, center, 29.0f);
    graphics.DrawLine(&outerPen, 2.0f, center, 11.0f, center);
    graphics.DrawLine(&outerPen, 20.0f, center, 29.0f, center);
    graphics.DrawLine(&innerPen, center, 2.0f, center, 11.0f);
    graphics.DrawLine(&innerPen, center, 20.0f, center, 29.0f);
    graphics.DrawLine(&innerPen, 2.0f, center, 11.0f, center);
    graphics.DrawLine(&innerPen, 20.0f, center, 29.0f, center);

    Gdiplus::SolidBrush centerBrush(Gdiplus::Color(255, 59, 130, 246));
    Gdiplus::Pen centerRing(Gdiplus::Color(255, 255, 255, 255), 1.25f);
    graphics.FillEllipse(&centerBrush, center - 3.5f, center - 3.5f, 7.0f, 7.0f);
    graphics.DrawEllipse(&centerRing, center - 6.5f, center - 6.5f, 13.0f, 13.0f);

    HBITMAP colorBitmap = nullptr;
    bitmap.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &colorBitmap);
    HBITMAP maskBitmap = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO info{};
    info.fIcon = FALSE;
    info.xHotspot = 15;
    info.yHotspot = 15;
    info.hbmColor = colorBitmap;
    info.hbmMask = maskBitmap;
    HCURSOR cursor = CreateIconIndirect(&info);
    DeleteObject(colorBitmap);
    DeleteObject(maskBitmap);
    return cursor != nullptr ? cursor : LoadCursorW(nullptr, IDC_CROSS);
}

bool OverlayWindow::EnsureWindow() {
    if (hwnd_ != nullptr) {
        return true;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = OverlayWindow::WndProc;
    wc.hInstance = instance_;
    wc.hCursor = nullptr;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kOverlayClassName;
    RegisterClassW(&wc);

    if (captureCursor_ == nullptr) {
        captureCursor_ = CreateCaptureCursor();
    }

    virtualBounds_ = util::VirtualScreenBounds();
    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        kOverlayClassName,
        L"",
        WS_POPUP,
        virtualBounds_.left,
        virtualBounds_.top,
        virtualBounds_.right - virtualBounds_.left,
        virtualBounds_.bottom - virtualBounds_.top,
        owner_,
        nullptr,
        instance_,
        this);
    if (hwnd_ == nullptr) {
        return false;
    }
    SetLayeredWindowAttributes(hwnd_, 0, kOverlayAlpha, LWA_ALPHA);
    SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);
    return true;
}

bool OverlayWindow::BeginSession(const AppSettings& settings, std::chrono::steady_clock::time_point hotkeyStart, const std::shared_ptr<ImageData>& frozenFrame) {
    settings_ = settings;
    hotkeyStart_ = hotkeyStart;
    frozenFrame_ = frozenFrame;
    active_ = false;
    mouseDown_ = false;
    dragging_ = false;
    anchorSet_ = false;
    dragStart_ = {};
    dragCurrent_ = {};
    anchorPoint_ = {};
    hasLastVisualBounds_ = false;
    lastVisualBounds_ = {};

    if (!EnsureWindow()) {
        return false;
    }

    virtualBounds_ = util::VirtualScreenBounds();
    SetLayeredWindowAttributes(hwnd_, 0, frozenFrame_ != nullptr ? 255 : 0, LWA_ALPHA);
    SetWindowPos(hwnd_,
        HWND_TOPMOST,
        virtualBounds_.left,
        virtualBounds_.top,
        virtualBounds_.right - virtualBounds_.left,
        virtualBounds_.bottom - virtualBounds_.top,
        SWP_SHOWWINDOW);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    SetCapture(hwnd_);
    if (captureCursor_ != nullptr) {
        SetCursor(captureCursor_);
    }
    active_ = true;
    InvalidateRect(hwnd_, nullptr, TRUE);
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    SetLayeredWindowAttributes(hwnd_, 0, frozenFrame_ != nullptr ? 255 : kOverlayAlpha, LWA_ALPHA);
    return true;
}

void OverlayWindow::Cancel() {
    if (hwnd_ != nullptr) {
        ReleaseCapture();
        SetLayeredWindowAttributes(hwnd_, 0, 0, LWA_ALPHA);
        ShowWindow(hwnd_, SW_HIDE);
    }
    active_ = false;
    mouseDown_ = false;
    dragging_ = false;
    anchorSet_ = false;
    dragStart_ = {};
    dragCurrent_ = {};
    anchorPoint_ = {};
    hasLastVisualBounds_ = false;
    frozenFrame_.reset();
}

bool OverlayWindow::IsActive() const {
    return active_;
}

POINT OverlayWindow::ScreenFromClientPoint(POINT point) const {
    point.x += virtualBounds_.left;
    point.y += virtualBounds_.top;
    return point;
}

RECT OverlayWindow::CurrentSelectionRect() const {
    if (dragging_ || (mouseDown_ && !anchorSet_)) {
        return util::NormalizeRect({
            ScreenFromClientPoint(dragStart_).x,
            ScreenFromClientPoint(dragStart_).y,
            ScreenFromClientPoint(dragCurrent_).x,
            ScreenFromClientPoint(dragCurrent_).y,
        });
    }
    if (anchorSet_) {
        return util::NormalizeRect({
            anchorPoint_.x,
            anchorPoint_.y,
            ScreenFromClientPoint(dragCurrent_).x,
            ScreenFromClientPoint(dragCurrent_).y,
        });
    }
    return {};
}

RECT OverlayWindow::CurrentVisualBounds() const {
    const RECT selection = CurrentSelectionRect();
    if (!util::IsRectEmptySafe(selection)) {
        RECT localSelection{
            selection.left - virtualBounds_.left,
            selection.top - virtualBounds_.top,
            selection.right - virtualBounds_.left,
            selection.bottom - virtualBounds_.top,
        };
        const RECT hudRect{
            localSelection.left - 10,
            std::max(0L, localSelection.top - 36),
            localSelection.left + 230,
            localSelection.top + 8,
        };
        RECT unionRect = UnionRectSafe(localSelection, hudRect);
        InflateRect(&unionRect, 10, 10);
        return unionRect;
    }
    if (anchorSet_) {
        RECT anchorRect{
            anchorPoint_.x - virtualBounds_.left - 10,
            anchorPoint_.y - virtualBounds_.top - 10,
            anchorPoint_.x - virtualBounds_.left + 10,
            anchorPoint_.y - virtualBounds_.top + 10,
        };
        return anchorRect;
    }
    return {};
}

void OverlayWindow::InvalidateVisualDelta() {
    if (hwnd_ == nullptr || !active_) {
        return;
    }
    const RECT current = CurrentVisualBounds();
    RECT dirty = hasLastVisualBounds_ ? UnionRectSafe(lastVisualBounds_, current) : current;
    if (util::IsRectEmptySafe(dirty)) {
        if (!hasLastVisualBounds_) {
            return;
        }
        dirty = lastVisualBounds_;
    }
    InvalidateRect(hwnd_, util::IsRectEmptySafe(dirty) ? nullptr : &dirty, FALSE);
    lastVisualBounds_ = current;
    hasLastVisualBounds_ = !util::IsRectEmptySafe(current);
}

void OverlayWindow::FinishSelection(const RECT& rect, bool clickModeCompletion) {
    Cancel();
    auto request = std::make_unique<CaptureRequest>();
    request->selection = util::NormalizeRect(rect);
    request->clickModeCompletion = clickModeCompletion;
    request->hotkeyStart = hotkeyStart_;
    request->commitTime = std::chrono::steady_clock::now();
    PostMessageW(owner_, WM_APP_CAPTURE_READY, 0, reinterpret_cast<LPARAM>(request.release()));
}

void OverlayWindow::Paint() {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd_, &ps);
    const int paintWidth = std::max(1L, ps.rcPaint.right - ps.rcPaint.left);
    const int paintHeight = std::max(1L, ps.rcPaint.bottom - ps.rcPaint.top);

    HDC backDc = CreateCompatibleDC(hdc);
    HBITMAP backBitmap = CreateCompatibleBitmap(hdc, paintWidth, paintHeight);
    HGDIOBJ oldBitmap = SelectObject(backDc, backBitmap);

    RECT paintLocal{0, 0, paintWidth, paintHeight};
    if (frozenFrame_ != nullptr && frozenFrame_->width > 0 && frozenFrame_->height > 0 && !frozenFrame_->pixels.empty()) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = frozenFrame_->width;
        info.bmiHeader.biHeight = -frozenFrame_->height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        StretchDIBits(
            backDc,
            0,
            0,
            frozenFrame_->width,
            frozenFrame_->height,
            0,
            0,
            frozenFrame_->width,
            frozenFrame_->height,
            frozenFrame_->pixels.data(),
            &info,
            DIB_RGB_COLORS,
            SRCCOPY);
        Gdiplus::Graphics graphics(backDc);
        Gdiplus::SolidBrush dimBrush(Gdiplus::Color(112, 0, 0, 0));
        graphics.FillRectangle(&dimBrush, 0.0f, 0.0f, static_cast<Gdiplus::REAL>(virtualBounds_.right - virtualBounds_.left),
            static_cast<Gdiplus::REAL>(virtualBounds_.bottom - virtualBounds_.top));
    } else {
        HBRUSH background = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(backDc, &paintLocal, background);
        DeleteObject(background);
    }
    SetViewportOrgEx(backDc, -ps.rcPaint.left, -ps.rcPaint.top, nullptr);

    const RECT selection = CurrentSelectionRect();
    if (!util::IsRectEmptySafe(selection)) {
        RECT localSelection{
            selection.left - virtualBounds_.left,
            selection.top - virtualBounds_.top,
            selection.right - virtualBounds_.left,
            selection.bottom - virtualBounds_.top,
        };
        if (frozenFrame_ != nullptr && frozenFrame_->width > 0 && frozenFrame_->height > 0 && !frozenFrame_->pixels.empty()) {
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = frozenFrame_->width;
            info.bmiHeader.biHeight = -frozenFrame_->height;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            StretchDIBits(
                backDc,
                localSelection.left,
                localSelection.top,
                localSelection.right - localSelection.left,
                localSelection.bottom - localSelection.top,
                localSelection.left,
                localSelection.top,
                localSelection.right - localSelection.left,
                localSelection.bottom - localSelection.top,
                frozenFrame_->pixels.data(),
                &info,
                DIB_RGB_COLORS,
                SRCCOPY);
        }
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
        HGDIOBJ oldPen = SelectObject(backDc, pen);
        HGDIOBJ oldBrush = SelectObject(backDc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(backDc, localSelection.left, localSelection.top, localSelection.right, localSelection.bottom);
        SelectObject(backDc, oldBrush);
        SelectObject(backDc, oldPen);
        DeleteObject(pen);

        const auto size = util::RectSize(selection);
        std::wstring hud = util::FormatRectSize(size.cx, size.cy);
        SetBkMode(backDc, TRANSPARENT);
        SetTextColor(backDc, RGB(255, 255, 255));
        RECT hudRect{
            localSelection.left,
            std::max(0L, localSelection.top - 28),
            localSelection.left + 220,
            localSelection.top,
        };
        DrawTextW(backDc, hud.c_str(), -1, &hudRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else if (anchorSet_) {
        RECT anchorRect{
            anchorPoint_.x - virtualBounds_.left - 4,
            anchorPoint_.y - virtualBounds_.top - 4,
            anchorPoint_.x - virtualBounds_.left + 4,
            anchorPoint_.y - virtualBounds_.top + 4,
        };
        HBRUSH brush = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(backDc, &anchorRect, brush);
        DeleteObject(brush);
    }

    SetViewportOrgEx(backDc, 0, 0, nullptr);
    BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top, paintWidth, paintHeight, backDc, 0, 0, SRCCOPY);
    SelectObject(backDc, oldBitmap);
    DeleteObject(backBitmap);
    DeleteDC(backDc);
    EndPaint(hwnd_, &ps);
}

LRESULT CALLBACK OverlayWindow::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<OverlayWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }
    return self != nullptr ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT OverlayWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_SETCURSOR:
        if (active_ && captureCursor_ != nullptr) {
            SetCursor(captureCursor_);
            return TRUE;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            Cancel();
            return 0;
        }
        break;
    case WM_RBUTTONUP:
        Cancel();
        return 0;
    case WM_LBUTTONDOWN:
        mouseDown_ = true;
        dragging_ = false;
        dragStart_ = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        dragCurrent_ = dragStart_;
        hasLastVisualBounds_ = false;
        lastVisualBounds_ = {};
        if (anchorSet_) {
            dragCurrent_ = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE: {
        dragCurrent_ = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (mouseDown_) {
            const int dx = dragCurrent_.x - dragStart_.x;
            const int dy = dragCurrent_.y - dragStart_.y;
            if ((dx * dx + dy * dy) >= static_cast<int>(settings_.dragThresholdPx * settings_.dragThresholdPx)) {
                dragging_ = true;
            }
        }
        InvalidateVisualDelta();
        return 0;
    }
    case WM_LBUTTONUP: {
        const POINT current{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        dragCurrent_ = current;
        mouseDown_ = false;

        if (dragging_) {
            FinishSelection(CurrentSelectionRect(), false);
            return 0;
        }

        if (settings_.clickModeEnabled) {
            const POINT screenPoint = ScreenFromClientPoint(current);
            if (!anchorSet_) {
                anchorSet_ = true;
                anchorPoint_ = screenPoint;
                InvalidateVisualDelta();
            } else {
                const RECT rect{anchorPoint_.x, anchorPoint_.y, screenPoint.x, screenPoint.y};
                FinishSelection(rect, true);
            }
        }
        return 0;
    }
    case WM_PAINT:
        Paint();
        return 0;
    case WM_KILLFOCUS:
        if (active_) {
            SetFocus(hwnd_);
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}
