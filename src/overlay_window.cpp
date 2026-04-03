#include "overlay_window.h"

#include "util.h"
#include <mmsystem.h>

namespace {

constexpr wchar_t kOverlayClassName[] = L"FrameSnapOverlayWindow";
constexpr wchar_t kFreezeClassName[] = L"FrameSnapFreezeWindow";
constexpr BYTE kOverlayAlpha = 175;
constexpr UINT_PTR kPresentTimerId = 1;

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

UINT RefreshIntervalMsForPoint(POINT screenPoint) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    const HMONITOR monitor = MonitorFromPoint(screenPoint, MONITOR_DEFAULTTONEAREST);
    if (monitor != nullptr && GetMonitorInfoW(monitor, &info)) {
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        if (EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) &&
            mode.dmDisplayFrequency > 1U &&
            mode.dmDisplayFrequency != 0xFFFFFFFFU) {
            return std::clamp(static_cast<UINT>(std::lround(1000.0 / static_cast<double>(mode.dmDisplayFrequency))), 4U, 17U);
        }
    }
    return 16U;
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

bool OverlayWindow::EnsureFreezeWindow() {
    if (freezeHwnd_ != nullptr) {
        return true;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = OverlayWindow::FreezeWndProc;
    wc.hInstance = instance_;
    wc.hCursor = nullptr;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kFreezeClassName;
    RegisterClassW(&wc);

    virtualBounds_ = util::VirtualScreenBounds();
    freezeHwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kFreezeClassName,
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
    if (freezeHwnd_ == nullptr) {
        return false;
    }
    SetWindowDisplayAffinity(freezeHwnd_, WDA_EXCLUDEFROMCAPTURE);
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
    pendingPresent_ = false;

    if (!EnsureWindow()) {
        return false;
    }
    if (frozenFrame_ != nullptr && !EnsureFreezeWindow()) {
        frozenFrame_.reset();
    }

    virtualBounds_ = util::VirtualScreenBounds();
    if (freezeHwnd_ != nullptr) {
        if (frozenFrame_ != nullptr) {
            SetWindowPos(
                freezeHwnd_,
                HWND_TOPMOST,
                virtualBounds_.left,
                virtualBounds_.top,
                virtualBounds_.right - virtualBounds_.left,
                virtualBounds_.bottom - virtualBounds_.top,
                SWP_SHOWWINDOW | SWP_NOACTIVATE);
            ShowWindow(freezeHwnd_, SW_SHOWNOACTIVATE);
            InvalidateRect(freezeHwnd_, nullptr, FALSE);
            UpdateWindow(freezeHwnd_);
        } else {
            ShowWindow(freezeHwnd_, SW_HIDE);
        }
    }
    SetLayeredWindowAttributes(hwnd_, 0, 0, LWA_ALPHA);
    SetWindowPos(hwnd_,
        freezeHwnd_ != nullptr && frozenFrame_ != nullptr ? freezeHwnd_ : HWND_TOPMOST,
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
    if (!highResTimerEnabled_) {
        highResTimerEnabled_ = timeBeginPeriod(1) == TIMERR_NOERROR;
    }
    active_ = true;
    POINT cursor{};
    GetCursorPos(&cursor);
    ConfigurePresentTimer(cursor);
    UpdateWindowRegion();
    InvalidateRect(hwnd_, nullptr, FALSE);
    UpdateWindow(hwnd_);
    SetLayeredWindowAttributes(hwnd_, 0, kOverlayAlpha, LWA_ALPHA);
    return true;
}

void OverlayWindow::Cancel() {
    KillTimer(hwnd_, kPresentTimerId);
    if (freezeHwnd_ != nullptr) {
        ShowWindow(freezeHwnd_, SW_HIDE);
    }
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
    pendingPresent_ = false;
    if (highResTimerEnabled_) {
        timeEndPeriod(1);
        highResTimerEnabled_ = false;
    }
    if (hwnd_ != nullptr) {
        UpdateWindowRegion();
    }
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
    UpdateWindowRegion();
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

void OverlayWindow::PresentNow() {
    if (hwnd_ == nullptr || !active_) {
        return;
    }
    InvalidateVisualDelta();
    UpdateWindow(hwnd_);
    DwmFlush();
}

void OverlayWindow::ConfigurePresentTimer(POINT screenPoint) {
    if (hwnd_ == nullptr) {
        return;
    }
    const UINT interval = RefreshIntervalMsForPoint(screenPoint);
    if (interval == presentIntervalMs_ && active_) {
        return;
    }
    presentIntervalMs_ = interval;
    KillTimer(hwnd_, kPresentTimerId);
    SetTimer(hwnd_, kPresentTimerId, presentIntervalMs_, nullptr);
}

void OverlayWindow::UpdateWindowRegion() {
    if (hwnd_ == nullptr) {
        return;
    }
    RECT client{};
    GetClientRect(hwnd_, &client);
    HRGN overlayRegion = CreateRectRgn(client.left, client.top, client.right, client.bottom);
    const RECT selection = CurrentSelectionRect();
    if (!util::IsRectEmptySafe(selection)) {
        RECT localSelection{
            selection.left - virtualBounds_.left,
            selection.top - virtualBounds_.top,
            selection.right - virtualBounds_.left,
            selection.bottom - virtualBounds_.top,
        };
        if ((localSelection.right - localSelection.left) > 4 && (localSelection.bottom - localSelection.top) > 4) {
            InflateRect(&localSelection, -2, -2);
            HRGN holeRegion = CreateRectRgn(localSelection.left, localSelection.top, localSelection.right, localSelection.bottom);
            CombineRgn(overlayRegion, overlayRegion, holeRegion, RGN_DIFF);
            DeleteObject(holeRegion);
        }
    }
    SetWindowRgn(hwnd_, overlayRegion, FALSE);
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

void OverlayWindow::PaintFrozenFrame() {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(freezeHwnd_, &ps);
    RECT client{};
    GetClientRect(freezeHwnd_, &client);

    if (frozenFrame_ != nullptr && frozenFrame_->width > 0 && frozenFrame_->height > 0 && !frozenFrame_->pixels.empty()) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = frozenFrame_->width;
        info.bmiHeader.biHeight = -frozenFrame_->height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        StretchDIBits(
            hdc,
            ps.rcPaint.left,
            ps.rcPaint.top,
            ps.rcPaint.right - ps.rcPaint.left,
            ps.rcPaint.bottom - ps.rcPaint.top,
            ps.rcPaint.left,
            ps.rcPaint.top,
            ps.rcPaint.right - ps.rcPaint.left,
            ps.rcPaint.bottom - ps.rcPaint.top,
            frozenFrame_->pixels.data(),
            &info,
            DIB_RGB_COLORS,
            SRCCOPY);
    } else {
        FillRect(hdc, &client, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    }

    EndPaint(freezeHwnd_, &ps);
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
    HBRUSH background = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(backDc, &paintLocal, background);
    DeleteObject(background);

    Gdiplus::Graphics graphics(backDc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const RECT selection = CurrentSelectionRect();
    if (!util::IsRectEmptySafe(selection)) {
        RECT localSelection{
            selection.left - virtualBounds_.left,
            selection.top - virtualBounds_.top,
            selection.right - virtualBounds_.left,
            selection.bottom - virtualBounds_.top,
        };
        RECT paintSelection{
            localSelection.left - ps.rcPaint.left,
            localSelection.top - ps.rcPaint.top,
            localSelection.right - ps.rcPaint.left,
            localSelection.bottom - ps.rcPaint.top,
        };
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
        HGDIOBJ oldPen = SelectObject(backDc, pen);
        HGDIOBJ oldBrush = SelectObject(backDc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(backDc, paintSelection.left, paintSelection.top, paintSelection.right, paintSelection.bottom);
        SelectObject(backDc, oldBrush);
        SelectObject(backDc, oldPen);
        DeleteObject(pen);

        const auto size = util::RectSize(selection);
        std::wstring hud = util::FormatRectSize(size.cx, size.cy);
        SetBkMode(backDc, TRANSPARENT);
        SetTextColor(backDc, RGB(255, 255, 255));
        RECT hudRect{
            paintSelection.left,
            std::max(0L, paintSelection.top - 28),
            paintSelection.left + 220,
            paintSelection.top,
        };
        DrawTextW(backDc, hud.c_str(), -1, &hudRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else if (anchorSet_) {
        RECT anchorRect{
            anchorPoint_.x - virtualBounds_.left - 4 - ps.rcPaint.left,
            anchorPoint_.y - virtualBounds_.top - 4 - ps.rcPaint.top,
            anchorPoint_.x - virtualBounds_.left + 4 - ps.rcPaint.left,
            anchorPoint_.y - virtualBounds_.top + 4 - ps.rcPaint.top,
        };
        HBRUSH brush = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(backDc, &anchorRect, brush);
        DeleteObject(brush);
    }

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

LRESULT CALLBACK OverlayWindow::FreezeWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<OverlayWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }
    return self != nullptr ? self->HandleFreezeMessage(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
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
            PostMessageW(owner_, WM_APP_CAPTURE_CANCELLED, 0, 0);
            return 0;
        }
        break;
    case WM_RBUTTONUP:
        Cancel();
        PostMessageW(owner_, WM_APP_CAPTURE_CANCELLED, 0, 0);
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
        ConfigurePresentTimer(ScreenFromClientPoint(dragCurrent_));
        PresentNow();
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
        ConfigurePresentTimer(ScreenFromClientPoint(dragCurrent_));
        pendingPresent_ = true;
        return 0;
    }
    case WM_LBUTTONUP: {
        const POINT current{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        dragCurrent_ = current;
        mouseDown_ = false;
        pendingPresent_ = false;

        if (dragging_) {
            FinishSelection(CurrentSelectionRect(), false);
            return 0;
        }

        if (settings_.clickModeEnabled) {
            const POINT screenPoint = ScreenFromClientPoint(current);
            if (!anchorSet_) {
                anchorSet_ = true;
                anchorPoint_ = screenPoint;
                PresentNow();
            } else {
                const RECT rect{anchorPoint_.x, anchorPoint_.y, screenPoint.x, screenPoint.y};
                FinishSelection(rect, true);
            }
        }
        return 0;
    }
    case WM_TIMER:
        if (wParam == kPresentTimerId && pendingPresent_) {
            pendingPresent_ = false;
            PresentNow();
            return 0;
        }
        break;
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

LRESULT OverlayWindow::HandleFreezeMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintFrozenFrame();
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    default:
        break;
    }
    return DefWindowProcW(freezeHwnd_, message, wParam, lParam);
}
