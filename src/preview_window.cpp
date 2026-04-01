#include "preview_window.h"

#include "util.h"

namespace {

constexpr wchar_t kPreviewClassName[] = L"ScreenshotterPreviewWindow";
constexpr UINT_PTR kPreviewTimerId = 1;

RECT WorkAreaForRect(const RECT& rect) {
    MONITORINFO info{sizeof(info)};
    const HMONITOR monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfoW(monitor, &info);
    return info.rcWork;
}

}  // namespace

PreviewWindow::PreviewWindow(HINSTANCE instance, HWND owner)
    : instance_(instance), owner_(owner) {}

bool PreviewWindow::EnsureWindow() {
    if (hwnd_ != nullptr) {
        return true;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = PreviewWindow::WndProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kPreviewClassName;
    RegisterClassW(&wc);

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kPreviewClassName,
        L"OneShot Preview",
        WS_POPUP,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        280,
        180,
        owner_,
        nullptr,
        instance_,
        this);
    if (hwnd_ == nullptr) {
        return false;
    }
    SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);
    return true;
}

void PreviewWindow::Show(const std::shared_ptr<ImageData>& image, UINT timeoutMs) {
    image_ = image;
    if (!EnsureWindow() || image_ == nullptr) {
        return;
    }
    const RECT workArea = WorkAreaForRect(image_->sourceRect);
    constexpr int width = 280;
    constexpr int height = 180;
    const int x = workArea.right - width - 20;
    const int y = workArea.bottom - height - 20;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    KillTimer(hwnd_, kPreviewTimerId);
    SetTimer(hwnd_, kPreviewTimerId, timeoutMs, nullptr);
    InvalidateRect(hwnd_, nullptr, TRUE);
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
    FillRect(hdc, &client, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    FrameRect(hdc, &client, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

    if (image_ != nullptr) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = image_->width;
        info.bmiHeader.biHeight = -image_->height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        const int thumbWidth = client.right - client.left - 16;
        const int thumbHeight = client.bottom - client.top - 44;
        StretchDIBits(
            hdc,
            8,
            8,
            thumbWidth,
            thumbHeight,
            0,
            0,
            image_->width,
            image_->height,
            image_->pixels.data(),
            &info,
            DIB_RGB_COLORS,
            SRCCOPY);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        std::wstring label = util::FormatRectSize(image_->width, image_->height);
        if (image_->hdrSource) {
            label += L"  HDR source";
        }
        RECT textRect{8, client.bottom - 30, client.right - 8, client.bottom - 8};
        DrawTextW(hdc, label.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

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
