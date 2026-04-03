#include "preview_window.h"

#include "util.h"

namespace {

constexpr wchar_t kPreviewClassName[] = L"FrameSnapPreviewWindow";
constexpr UINT_PTR kPreviewTimerId = 1;
constexpr int kPreviewWidth = 312;
constexpr int kPreviewHeight = 220;
constexpr int kPreviewPadding = 12;
constexpr int kFooterHeight = 44;
constexpr COLORREF kPreviewWindowColor = RGB(10, 14, 20);
constexpr COLORREF kPreviewCardColor = RGB(20, 27, 36);
constexpr COLORREF kPreviewBorderColor = RGB(43, 55, 72);
constexpr COLORREF kPreviewFrameColor = RGB(71, 85, 105);
constexpr COLORREF kPreviewMediaColor = RGB(8, 11, 16);
constexpr COLORREF kPreviewTitleColor = RGB(232, 238, 247);
constexpr COLORREF kPreviewBodyColor = RGB(194, 203, 217);
constexpr COLORREF kPreviewMutedColor = RGB(135, 148, 166);
constexpr COLORREF kPreviewAccentFill = RGB(30, 58, 138);
constexpr COLORREF kPreviewAccentText = RGB(191, 219, 254);
constexpr COLORREF kPreviewActionFill = RGB(37, 99, 235);
constexpr COLORREF kPreviewActionText = RGB(255, 255, 255);

RECT WorkAreaForRect(const RECT& rect) {
    MONITORINFO info{sizeof(info)};
    const HMONITOR monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfoW(monitor, &info);
    return info.rcWork;
}

HFONT CreatePreviewFont(int height, int weight) {
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

RECT RectWithSize(LONG left, LONG top, LONG width, LONG height) {
    return {left, top, left + width, top + height};
}

Gdiplus::Color ToGdiColor(COLORREF color, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

}  // namespace

PreviewWindow::PreviewWindow(HINSTANCE instance, HWND owner)
    : instance_(instance), owner_(owner) {}

PreviewWindow::~PreviewWindow() {
    if (titleFont_ != nullptr) {
        DeleteObject(titleFont_);
    }
    if (bodyFont_ != nullptr) {
        DeleteObject(bodyFont_);
    }
}

bool PreviewWindow::EnsureWindow() {
    if (hwnd_ != nullptr) {
        return true;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = PreviewWindow::WndProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kPreviewClassName;
    RegisterClassW(&wc);

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kPreviewClassName,
        L"FrameSnap Preview",
        WS_POPUP,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        kPreviewWidth,
        kPreviewHeight,
        owner_,
        nullptr,
        instance_,
        this);
    if (hwnd_ == nullptr) {
        return false;
    }
    titleFont_ = CreatePreviewFont(14, FW_SEMIBOLD);
    bodyFont_ = CreatePreviewFont(12, FW_NORMAL);
    DWM_WINDOW_CORNER_PREFERENCE cornerPreference = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPreference, sizeof(cornerPreference));
    SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);
    return true;
}

void PreviewWindow::Show(const std::shared_ptr<ImageData>& image, UINT timeoutMs) {
    image_ = image;
    if (!EnsureWindow() || image_ == nullptr) {
        return;
    }
    const RECT workArea = WorkAreaForRect(image_->sourceRect);
    const int x = workArea.right - kPreviewWidth - 18;
    const int y = workArea.bottom - kPreviewHeight - 18;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, kPreviewWidth, kPreviewHeight, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    KillTimer(hwnd_, kPreviewTimerId);
    SetTimer(hwnd_, kPreviewTimerId, timeoutMs, nullptr);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PreviewWindow::Hide() {
    if (hwnd_ != nullptr) {
        KillTimer(hwnd_, kPreviewTimerId);
        ShowWindow(hwnd_, SW_HIDE);
    }
}

std::shared_ptr<ImageData> PreviewWindow::CurrentImage() const {
    return image_;
}

void PreviewWindow::Paint() {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int paintWidth = std::max(1L, ps.rcPaint.right - ps.rcPaint.left);
    const int paintHeight = std::max(1L, ps.rcPaint.bottom - ps.rcPaint.top);

    HDC backDc = CreateCompatibleDC(hdc);
    HBITMAP backBitmap = CreateCompatibleBitmap(hdc, paintWidth, paintHeight);
    HGDIOBJ oldBitmap = SelectObject(backDc, backBitmap);
    SetViewportOrgEx(backDc, -ps.rcPaint.left, -ps.rcPaint.top, nullptr);

    HBRUSH windowBrush = CreateSolidBrush(kPreviewWindowColor);
    FillRect(backDc, &client, windowBrush);
    DeleteObject(windowBrush);

    const RECT cardRect = RectWithSize(kPreviewPadding, kPreviewPadding, client.right - kPreviewPadding * 2, client.bottom - kPreviewPadding * 2);
    HBRUSH shadowBrush = CreateSolidBrush(RGB(0, 0, 0));
    RECT shadowRect = cardRect;
    OffsetRect(&shadowRect, 0, 5);
    HGDIOBJ shadowOldBrush = SelectObject(backDc, shadowBrush);
    HGDIOBJ shadowOldPen = SelectObject(backDc, GetStockObject(NULL_PEN));
    RoundRect(backDc, shadowRect.left, shadowRect.top, shadowRect.right, shadowRect.bottom, 20, 20);
    SelectObject(backDc, shadowOldBrush);
    SelectObject(backDc, shadowOldPen);
    DeleteObject(shadowBrush);

    HBRUSH cardBrush = CreateSolidBrush(kPreviewCardColor);
    HPEN cardPen = CreatePen(PS_SOLID, 1, kPreviewBorderColor);
    HGDIOBJ oldBrush = SelectObject(backDc, cardBrush);
    HGDIOBJ oldPen = SelectObject(backDc, cardPen);
    RoundRect(backDc, cardRect.left, cardRect.top, cardRect.right, cardRect.bottom, 20, 20);
    SelectObject(backDc, oldBrush);
    SelectObject(backDc, oldPen);
    DeleteObject(cardBrush);
    DeleteObject(cardPen);

    Gdiplus::Graphics graphics(backDc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    const RECT mediaRect = RectWithSize(cardRect.left + 12, cardRect.top + 12, cardRect.right - cardRect.left - 24, cardRect.bottom - cardRect.top - kFooterHeight - 26);
    HBRUSH mediaBrush = CreateSolidBrush(kPreviewMediaColor);
    HPEN mediaPen = CreatePen(PS_SOLID, 1, RGB(44, 55, 72));
    oldBrush = SelectObject(backDc, mediaBrush);
    oldPen = SelectObject(backDc, mediaPen);
    RoundRect(backDc, mediaRect.left, mediaRect.top, mediaRect.right, mediaRect.bottom, 16, 16);
    SelectObject(backDc, oldBrush);
    SelectObject(backDc, oldPen);
    DeleteObject(mediaBrush);
    DeleteObject(mediaPen);

    if (image_ != nullptr) {
        const float mediaWidth = static_cast<float>(mediaRect.right - mediaRect.left);
        const float mediaHeight = static_cast<float>(mediaRect.bottom - mediaRect.top);
        const float scale = std::min(mediaWidth / static_cast<float>(image_->width), mediaHeight / static_cast<float>(image_->height));
        const float drawWidth = image_->width * scale;
        const float drawHeight = image_->height * scale;
        const float drawX = mediaRect.left + (mediaWidth - drawWidth) * 0.5f;
        const float drawY = mediaRect.top + (mediaHeight - drawHeight) * 0.5f;

        Gdiplus::Bitmap bitmap(image_->width, image_->height, image_->width * 4, PixelFormat32bppARGB, const_cast<BYTE*>(image_->pixels.data()));
        graphics.DrawImage(&bitmap, Gdiplus::RectF(drawX, drawY, drawWidth, drawHeight));
        Gdiplus::Pen imageFrame(ToGdiColor(kPreviewFrameColor), 1.0f);
        graphics.DrawRectangle(&imageFrame, Gdiplus::RectF(drawX, drawY, drawWidth, drawHeight));
    }

    const RECT footerRect = RectWithSize(cardRect.left + 12, cardRect.bottom - kFooterHeight - 10, cardRect.right - cardRect.left - 24, kFooterHeight);
    HBRUSH footerBrush = CreateSolidBrush(kPreviewCardColor);
    FillRect(backDc, &footerRect, footerBrush);
    DeleteObject(footerBrush);

    HPEN dividerPen = CreatePen(PS_SOLID, 1, RGB(35, 45, 58));
    oldPen = SelectObject(backDc, dividerPen);
    MoveToEx(backDc, footerRect.left, footerRect.top, nullptr);
    LineTo(backDc, footerRect.right, footerRect.top);
    SelectObject(backDc, oldPen);
    DeleteObject(dividerPen);

    SetBkMode(backDc, TRANSPARENT);
    SelectObject(backDc, bodyFont_ != nullptr ? bodyFont_ : GetStockObject(DEFAULT_GUI_FONT));
    if (image_ != nullptr) {
        const std::wstring sizeLabel = util::FormatRectSize(image_->width, image_->height);
        RECT resolutionPill = RectWithSize(footerRect.left, footerRect.top + 10, 120, 24);
        HBRUSH resBrush = CreateSolidBrush(RGB(17, 23, 31));
        HPEN resPen = CreatePen(PS_SOLID, 1, RGB(52, 64, 82));
        oldBrush = SelectObject(backDc, resBrush);
        oldPen = SelectObject(backDc, resPen);
        RoundRect(backDc, resolutionPill.left, resolutionPill.top, resolutionPill.right, resolutionPill.bottom, 14, 14);
        SelectObject(backDc, oldBrush);
        SelectObject(backDc, oldPen);
        DeleteObject(resBrush);
        DeleteObject(resPen);

        SetTextColor(backDc, kPreviewBodyColor);
        DrawTextW(backDc, sizeLabel.c_str(), -1, &resolutionPill, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (image_->hdrSource) {
            RECT hdrRect = RectWithSize(footerRect.right - 146, footerRect.top + 10, 54, 24);
            HBRUSH hdrBrush = CreateSolidBrush(kPreviewAccentFill);
            HPEN hdrPen = CreatePen(PS_SOLID, 1, RGB(96, 165, 250));
            oldBrush = SelectObject(backDc, hdrBrush);
            oldPen = SelectObject(backDc, hdrPen);
            RoundRect(backDc, hdrRect.left, hdrRect.top, hdrRect.right, hdrRect.bottom, 14, 14);
            SelectObject(backDc, oldBrush);
            SelectObject(backDc, oldPen);
            DeleteObject(hdrBrush);
            DeleteObject(hdrPen);
            SetTextColor(backDc, kPreviewAccentText);
            DrawTextW(backDc, L"HDR", -1, &hdrRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        RECT actionRect = RectWithSize(footerRect.right - (86), footerRect.top + 8, 86, 28);
        HBRUSH actionBrush = CreateSolidBrush(kPreviewActionFill);
        HPEN actionPen = CreatePen(PS_SOLID, 1, kPreviewActionFill);
        oldBrush = SelectObject(backDc, actionBrush);
        oldPen = SelectObject(backDc, actionPen);
        RoundRect(backDc, actionRect.left, actionRect.top, actionRect.right, actionRect.bottom, 16, 16);
        SelectObject(backDc, oldBrush);
        SelectObject(backDc, oldPen);
        DeleteObject(actionBrush);
        DeleteObject(actionPen);

        SetTextColor(backDc, kPreviewActionText);
        DrawTextW(backDc, L"Edit", -1, &actionRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    SetViewportOrgEx(backDc, 0, 0, nullptr);
    BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top, paintWidth, paintHeight, backDc, 0, 0, SRCCOPY);
    SelectObject(backDc, oldBitmap);
    DeleteObject(backBitmap);
    DeleteDC(backDc);

    EndPaint(hwnd_, &ps);
}

LRESULT CALLBACK PreviewWindow::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<PreviewWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<PreviewWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }
    return self != nullptr ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT PreviewWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_LBUTTONUP:
        Hide();
        PostMessageW(owner_, WM_APP_PREVIEW_CLICKED, 0, 0);
        return 0;
    case WM_TIMER:
        if (wParam == kPreviewTimerId) {
            Hide();
        }
        return 0;
    case WM_PAINT:
        Paint();
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}
