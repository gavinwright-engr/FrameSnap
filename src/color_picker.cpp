#include "color_picker.h"

namespace {

constexpr wchar_t kColorPickerClassName[] = L"ScreenshotterColorPicker";
constexpr int kWheelMargin = 16;
constexpr int kBottomPreviewHeight = 86;
constexpr COLORREF kPickerBackground = RGB(251, 253, 255);
constexpr COLORREF kPickerBorder = RGB(214, 223, 234);
constexpr COLORREF kPickerInnerBorder = RGB(226, 232, 240);

struct WheelCache {
    int diameter{};
    BITMAPINFO bmi{};
    std::vector<std::uint8_t> pixels;
};

WheelCache& CachedWheel() {
    static WheelCache cache;
    return cache;
}

COLORREF WheelHsvToColor(float hue, float saturation, float value) {
    hue = std::fmod(std::max(hue, 0.0f), 360.0f);
    saturation = std::clamp(saturation, 0.0f, 1.0f);
    value = std::clamp(value, 0.0f, 1.0f);

    const float chroma = value * saturation;
    const float x = chroma * (1.0f - std::fabs(std::fmod(hue / 60.0f, 2.0f) - 1.0f));
    const float m = value - chroma;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;

    if (hue < 60.0f) {
        r = chroma;
        g = x;
    } else if (hue < 120.0f) {
        r = x;
        g = chroma;
    } else if (hue < 180.0f) {
        g = chroma;
        b = x;
    } else if (hue < 240.0f) {
        g = x;
        b = chroma;
    } else if (hue < 300.0f) {
        r = x;
        b = chroma;
    } else {
        r = chroma;
        b = x;
    }

    return RGB(
        static_cast<int>((r + m) * 255.0f),
        static_cast<int>((g + m) * 255.0f),
        static_cast<int>((b + m) * 255.0f));
}

void EnsureWheelCache(int diameter) {
    auto& cache = CachedWheel();
    if (cache.diameter == diameter && !cache.pixels.empty()) {
        return;
    }

    cache = {};
    cache.diameter = diameter;
    cache.bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    cache.bmi.bmiHeader.biWidth = diameter;
    cache.bmi.bmiHeader.biHeight = -diameter;
    cache.bmi.bmiHeader.biPlanes = 1;
    cache.bmi.bmiHeader.biBitCount = 32;
    cache.bmi.bmiHeader.biCompression = BI_RGB;
    cache.pixels.assign(static_cast<std::size_t>(diameter) * static_cast<std::size_t>(diameter) * 4U, 0);

    const float center = diameter * 0.5f;
    const float radius = center - 1.0f;
    for (int y = 0; y < diameter; ++y) {
        for (int x = 0; x < diameter; ++x) {
            const float dx = x - center;
            const float dy = y - center;
            const float distance = std::sqrt(dx * dx + dy * dy);
            const auto index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(diameter) + static_cast<std::size_t>(x)) * 4U;
            if (distance > radius) {
                cache.pixels[index + 3] = 0;
                continue;
            }
            float angle = std::atan2(dy, dx);
            if (angle < 0.0f) {
                angle += std::numbers::pi_v<float> * 2.0f;
            }
            const float hue = angle * 180.0f / std::numbers::pi_v<float>;
            const float saturation = distance / radius;
            const COLORREF color = WheelHsvToColor(hue, saturation, 1.0f);
            cache.pixels[index + 0] = GetBValue(color);
            cache.pixels[index + 1] = GetGValue(color);
            cache.pixels[index + 2] = GetRValue(color);
            cache.pixels[index + 3] = 255;
        }
    }
}

}  // namespace

ColorPickerControl::ColorPickerControl(HINSTANCE instance, HWND parent, int controlId)
    : instance_(instance), parent_(parent), controlId_(controlId) {}

bool ColorPickerControl::Create(int x, int y, int width, int height) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = ColorPickerControl::WndProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kColorPickerClassName;
    RegisterClassW(&wc);

    hwnd_ = CreateWindowExW(
        0,
        kColorPickerClassName,
        L"",
        WS_CHILD | WS_VISIBLE,
        x,
        y,
        width,
        height,
        parent_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId_)),
        instance_,
        this);
    return hwnd_ != nullptr;
}

void ColorPickerControl::Move(int x, int y, int width, int height) const {
    if (hwnd_ != nullptr) {
        MoveWindow(hwnd_, x, y, width, height, TRUE);
    }
}

HWND ColorPickerControl::Handle() const {
    return hwnd_;
}

void ColorPickerControl::SetColor(COLORREF color) {
    ColorToHsv(color, hue_, saturation_, value_);
    if (hwnd_ != nullptr) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

COLORREF ColorPickerControl::GetColor() const {
    return HsvToColor(hue_, saturation_, value_);
}

RECT ColorPickerControl::WheelRect() const {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int diameter = std::min(client.bottom - client.top - kBottomPreviewHeight - kWheelMargin * 2, client.right - client.left - 44);
    return {kWheelMargin, kWheelMargin, kWheelMargin + diameter, kWheelMargin + diameter};
}

RECT ColorPickerControl::ValueRect() const {
    const RECT wheel = WheelRect();
    return {wheel.right + 10, wheel.top, wheel.right + 28, wheel.bottom};
}

void ColorPickerControl::Paint() {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int width = std::max(1L, client.right - client.left);
    const int height = std::max(1L, client.bottom - client.top);
    HDC backDc = CreateCompatibleDC(hdc);
    HBITMAP backBitmap = CreateCompatibleBitmap(hdc, width, height);
    HGDIOBJ oldBitmap = SelectObject(backDc, backBitmap);

    HBRUSH backgroundBrush = CreateSolidBrush(kPickerBackground);
    FillRect(backDc, &client, backgroundBrush);
    DeleteObject(backgroundBrush);

    HPEN borderPen = CreatePen(PS_SOLID, 1, kPickerBorder);
    HGDIOBJ oldPen = SelectObject(backDc, borderPen);
    HGDIOBJ oldBrush = SelectObject(backDc, GetStockObject(HOLLOW_BRUSH));
    RoundRect(backDc, client.left, client.top, client.right, client.bottom, 18, 18);
    SelectObject(backDc, oldBrush);
    SelectObject(backDc, oldPen);
    DeleteObject(borderPen);

    const RECT wheel = WheelRect();
    const int wheelWidth = wheel.right - wheel.left;
    const int wheelHeight = wheel.bottom - wheel.top;
    EnsureWheelCache(wheelWidth);
    const auto& wheelCache = CachedWheel();
    StretchDIBits(backDc, wheel.left, wheel.top, wheelWidth, wheelHeight, 0, 0, wheelWidth, wheelHeight, wheelCache.pixels.data(), &wheelCache.bmi,
        DIB_RGB_COLORS, SRCCOPY);

    HPEN innerPen = CreatePen(PS_SOLID, 1, kPickerInnerBorder);
    oldPen = SelectObject(backDc, innerPen);
    oldBrush = SelectObject(backDc, GetStockObject(HOLLOW_BRUSH));
    Ellipse(backDc, wheel.left, wheel.top, wheel.right, wheel.bottom);
    SelectObject(backDc, oldBrush);
    SelectObject(backDc, oldPen);
    DeleteObject(innerPen);

    const RECT valueRect = ValueRect();
    for (int y = valueRect.top; y < valueRect.bottom; ++y) {
        const float normalized = 1.0f - static_cast<float>(y - valueRect.top) / static_cast<float>(std::max(1L, valueRect.bottom - valueRect.top - 1));
        const COLORREF color = HsvToColor(hue_, saturation_, normalized);
        HBRUSH brush = CreateSolidBrush(color);
        RECT row{valueRect.left, y, valueRect.right, y + 1};
        FillRect(backDc, &row, brush);
        DeleteObject(brush);
    }
    HPEN valuePen = CreatePen(PS_SOLID, 1, kPickerInnerBorder);
    oldPen = SelectObject(backDc, valuePen);
    oldBrush = SelectObject(backDc, GetStockObject(HOLLOW_BRUSH));
    RoundRect(backDc, valueRect.left - 1, valueRect.top - 1, valueRect.right + 1, valueRect.bottom + 1, 10, 10);
    SelectObject(backDc, oldBrush);
    SelectObject(backDc, oldPen);
    DeleteObject(valuePen);

    const float angle = hue_ * std::numbers::pi_v<float> / 180.0f;
    const int selectorX = static_cast<int>(wheel.left + wheelWidth / 2.0f + std::cos(angle) * saturation_ * (wheelWidth / 2.0f - 1.0f));
    const int selectorY = static_cast<int>(wheel.top + wheelHeight / 2.0f + std::sin(angle) * saturation_ * (wheelHeight / 2.0f - 1.0f));
    HPEN selectorPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    HBRUSH selectorBrush = CreateSolidBrush(HsvToColor(hue_, saturation_, value_));
    oldPen = SelectObject(backDc, selectorPen);
    oldBrush = SelectObject(backDc, selectorBrush);
    Ellipse(backDc, selectorX - 6, selectorY - 6, selectorX + 6, selectorY + 6);
    SelectObject(backDc, oldBrush);
    SelectObject(backDc, oldPen);
    DeleteObject(selectorBrush);
    DeleteObject(selectorPen);

    const int valueY = valueRect.top + static_cast<int>((1.0f - value_) * static_cast<float>(valueRect.bottom - valueRect.top));
    HPEN markerPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    oldPen = SelectObject(backDc, markerPen);
    MoveToEx(backDc, valueRect.left - 2, valueY, nullptr);
    LineTo(backDc, valueRect.right + 2, valueY);
    SelectObject(backDc, oldPen);
    DeleteObject(markerPen);

    RECT swatch{wheel.left, wheel.bottom + 18, valueRect.right, wheel.bottom + 18 + 52};
    HBRUSH swatchBrush = CreateSolidBrush(GetColor());
    FillRect(backDc, &swatch, swatchBrush);
    DeleteObject(swatchBrush);
    HPEN swatchPen = CreatePen(PS_SOLID, 1, kPickerBorder);
    oldPen = SelectObject(backDc, swatchPen);
    oldBrush = SelectObject(backDc, GetStockObject(HOLLOW_BRUSH));
    RoundRect(backDc, swatch.left, swatch.top, swatch.right, swatch.bottom, 14, 14);
    SelectObject(backDc, oldBrush);
    SelectObject(backDc, oldPen);
    DeleteObject(swatchPen);

    BitBlt(hdc, 0, 0, width, height, backDc, 0, 0, SRCCOPY);
    SelectObject(backDc, oldBitmap);
    DeleteObject(backBitmap);
    DeleteDC(backDc);

    EndPaint(hwnd_, &ps);
}

void ColorPickerControl::NotifyParent() const {
    PostMessageW(parent_, WM_APP_COLOR_CHANGED, static_cast<WPARAM>(controlId_), static_cast<LPARAM>(GetColor()));
}

void ColorPickerControl::UpdateFromPoint(POINT point) {
    const RECT wheel = WheelRect();
    const RECT valueRect = ValueRect();
    if (PtInRect(&wheel, point) || draggingWheel_) {
        const float centerX = (wheel.left + wheel.right) / 2.0f;
        const float centerY = (wheel.top + wheel.bottom) / 2.0f;
        const float dx = point.x - centerX;
        const float dy = point.y - centerY;
        float angle = std::atan2(dy, dx);
        if (angle < 0.0f) {
            angle += std::numbers::pi_v<float> * 2.0f;
        }
        hue_ = angle * 180.0f / std::numbers::pi_v<float>;
        const float radius = (wheel.right - wheel.left) / 2.0f;
        saturation_ = std::clamp(std::sqrt(dx * dx + dy * dy) / radius, 0.0f, 1.0f);
        draggingWheel_ = true;
    }
    if (PtInRect(&valueRect, point) || draggingValue_) {
        value_ = 1.0f - std::clamp(static_cast<float>(point.y - valueRect.top) / static_cast<float>(std::max(1L, valueRect.bottom - valueRect.top)), 0.0f, 1.0f);
        draggingValue_ = true;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    NotifyParent();
}

COLORREF ColorPickerControl::HsvToColor(float hue, float saturation, float value) {
    hue = std::fmod(std::max(hue, 0.0f), 360.0f);
    saturation = std::clamp(saturation, 0.0f, 1.0f);
    value = std::clamp(value, 0.0f, 1.0f);

    const float chroma = value * saturation;
    const float x = chroma * (1.0f - std::fabs(std::fmod(hue / 60.0f, 2.0f) - 1.0f));
    const float m = value - chroma;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;

    if (hue < 60.0f) {
        r = chroma;
        g = x;
    } else if (hue < 120.0f) {
        r = x;
        g = chroma;
    } else if (hue < 180.0f) {
        g = chroma;
        b = x;
    } else if (hue < 240.0f) {
        g = x;
        b = chroma;
    } else if (hue < 300.0f) {
        r = x;
        b = chroma;
    } else {
        r = chroma;
        b = x;
    }

    return RGB(
        static_cast<int>((r + m) * 255.0f),
        static_cast<int>((g + m) * 255.0f),
        static_cast<int>((b + m) * 255.0f));
}

void ColorPickerControl::ColorToHsv(COLORREF color, float& hue, float& saturation, float& value) {
    const float r = GetRValue(color) / 255.0f;
    const float g = GetGValue(color) / 255.0f;
    const float b = GetBValue(color) / 255.0f;
    const float maxValue = std::max({r, g, b});
    const float minValue = std::min({r, g, b});
    const float delta = maxValue - minValue;

    hue = 0.0f;
    if (delta > 0.0f) {
        if (maxValue == r) {
            hue = 60.0f * std::fmod(((g - b) / delta), 6.0f);
        } else if (maxValue == g) {
            hue = 60.0f * (((b - r) / delta) + 2.0f);
        } else {
            hue = 60.0f * (((r - g) / delta) + 4.0f);
        }
        if (hue < 0.0f) {
            hue += 360.0f;
        }
    }

    saturation = maxValue == 0.0f ? 0.0f : delta / maxValue;
    value = maxValue;
}

LRESULT CALLBACK ColorPickerControl::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<ColorPickerControl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<ColorPickerControl*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }
    return self != nullptr ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT ColorPickerControl::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_LBUTTONDOWN:
        SetCapture(hwnd_);
        draggingWheel_ = false;
        draggingValue_ = false;
        UpdateFromPoint({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        return 0;
    case WM_MOUSEMOVE:
        if ((wParam & MK_LBUTTON) != 0U) {
            UpdateFromPoint({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        }
        return 0;
    case WM_LBUTTONUP:
        draggingWheel_ = false;
        draggingValue_ = false;
        ReleaseCapture();
        return 0;
    case WM_PAINT:
        Paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}
