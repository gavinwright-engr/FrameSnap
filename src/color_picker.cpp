#include "color_picker.h"

namespace {

constexpr wchar_t kColorPickerClassName[] = L"ScreenshotterColorPicker";

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
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

COLORREF ColorPickerControl::GetColor() const {
    return HsvToColor(hue_, saturation_, value_);
}

RECT ColorPickerControl::WheelRect() const {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int diameter = std::min(client.bottom - client.top - 12, client.right - client.left - 40);
    return {6, 6, 6 + diameter, 6 + diameter};
}

RECT ColorPickerControl::ValueRect() const {
    const RECT wheel = WheelRect();
    return {wheel.right + 8, wheel.top, wheel.right + 24, wheel.bottom};
}

void ColorPickerControl::Paint() {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT client{};
    GetClientRect(hwnd_, &client);
    FillRect(hdc, &client, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    const RECT wheel = WheelRect();
    const int wheelWidth = wheel.right - wheel.left;
    const int wheelHeight = wheel.bottom - wheel.top;
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = wheelWidth;
    bmi.bmiHeader.biHeight = -wheelHeight;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(wheelWidth) * static_cast<std::size_t>(wheelHeight) * 4U);
    const float centerX = wheelWidth / 2.0f;
    const float centerY = wheelHeight / 2.0f;
    const float radius = std::min(centerX, centerY) - 1.0f;
    for (int y = 0; y < wheelHeight; ++y) {
        for (int x = 0; x < wheelWidth; ++x) {
            const float dx = x - centerX;
            const float dy = y - centerY;
            const float distance = std::sqrt(dx * dx + dy * dy);
            const auto index = (static_cast<std::size_t>(y) * wheelWidth + static_cast<std::size_t>(x)) * 4U;
            if (distance > radius) {
                pixels[index + 3] = 255;
                continue;
            }
            float angle = std::atan2(dy, dx);
            if (angle < 0.0f) {
                angle += std::numbers::pi_v<float> * 2.0f;
            }
            const float hue = angle * 180.0f / std::numbers::pi_v<float>;
            const float saturation = distance / radius;
            const COLORREF color = HsvToColor(hue, saturation, 1.0f);
            pixels[index + 0] = GetBValue(color);
            pixels[index + 1] = GetGValue(color);
            pixels[index + 2] = GetRValue(color);
            pixels[index + 3] = 255;
        }
    }
    StretchDIBits(hdc, wheel.left, wheel.top, wheelWidth, wheelHeight, 0, 0, wheelWidth, wheelHeight, pixels.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);

    const RECT valueRect = ValueRect();
    for (int y = valueRect.top; y < valueRect.bottom; ++y) {
        const float normalized = 1.0f - static_cast<float>(y - valueRect.top) / static_cast<float>(std::max(1L, valueRect.bottom - valueRect.top - 1));
        const COLORREF color = HsvToColor(hue_, saturation_, normalized);
        HBRUSH brush = CreateSolidBrush(color);
        RECT row{valueRect.left, y, valueRect.right, y + 1};
        FillRect(hdc, &row, brush);
        DeleteObject(brush);
    }

    const float angle = hue_ * std::numbers::pi_v<float> / 180.0f;
    const int selectorX = static_cast<int>(wheel.left + wheelWidth / 2.0f + std::cos(angle) * saturation_ * (wheelWidth / 2.0f - 1.0f));
    const int selectorY = static_cast<int>(wheel.top + wheelHeight / 2.0f + std::sin(angle) * saturation_ * (wheelHeight / 2.0f - 1.0f));
    Ellipse(hdc, selectorX - 4, selectorY - 4, selectorX + 4, selectorY + 4);

    const int valueY = valueRect.top + static_cast<int>((1.0f - value_) * static_cast<float>(valueRect.bottom - valueRect.top));
    MoveToEx(hdc, valueRect.left - 2, valueY, nullptr);
    LineTo(hdc, valueRect.right + 2, valueY);

    RECT swatch{valueRect.left, valueRect.bottom + 8, valueRect.right, valueRect.bottom + 28};
    HBRUSH swatchBrush = CreateSolidBrush(GetColor());
    FillRect(hdc, &swatch, swatchBrush);
    DeleteObject(swatchBrush);
    FrameRect(hdc, &swatch, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

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
    InvalidateRect(hwnd_, nullptr, TRUE);
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
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}
