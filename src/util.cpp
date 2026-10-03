#include "util.h"

namespace util {

namespace {

struct GdiSnapshotCache {
    HDC memoryDc{};
    HBITMAP bitmap{};
    HGDIOBJ previousBitmap{};
    void* bits{};
    int width{};
    int height{};

    ~GdiSnapshotCache() {
        if (memoryDc != nullptr && previousBitmap != nullptr) {
            SelectObject(memoryDc, previousBitmap);
        }
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        if (memoryDc != nullptr) {
            DeleteDC(memoryDc);
        }
    }
};

bool EnsureSnapshotCache(HDC screenDc, int width, int height, GdiSnapshotCache& cache) {
    if (cache.memoryDc == nullptr) {
        cache.memoryDc = CreateCompatibleDC(screenDc);
        if (cache.memoryDc == nullptr) {
            return false;
        }
    }

    if (cache.bitmap != nullptr && cache.width == width && cache.height == height && cache.bits != nullptr) {
        return true;
    }

    if (cache.bitmap != nullptr) {
        if (cache.previousBitmap != nullptr) {
            SelectObject(cache.memoryDc, cache.previousBitmap);
        }
        DeleteObject(cache.bitmap);
        cache.bitmap = nullptr;
        cache.bits = nullptr;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    cache.bitmap = CreateDIBSection(screenDc, &info, DIB_RGB_COLORS, &cache.bits, nullptr, 0);
    cache.width = width;
    cache.height = height;
    if (cache.bitmap == nullptr || cache.bits == nullptr) {
        if (cache.bitmap != nullptr) {
            DeleteObject(cache.bitmap);
            cache.bitmap = nullptr;
        }
        cache.bits = nullptr;
        cache.width = 0;
        cache.height = 0;
        return false;
    }

    const HGDIOBJ oldBitmap = SelectObject(cache.memoryDc, cache.bitmap);
    if (oldBitmap == nullptr || oldBitmap == HGDI_ERROR) {
        DeleteObject(cache.bitmap);
        cache.bitmap = nullptr;
        cache.bits = nullptr;
        cache.width = 0;
        cache.height = 0;
        return false;
    }
    if (cache.previousBitmap == nullptr) {
        cache.previousBitmap = oldBitmap;
    }
    return true;
}

}  // namespace

std::wstring HResultMessage(HRESULT hr) {
    wchar_t* buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageW(flags, nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring message = length > 0 && buffer != nullptr ? std::wstring(buffer, length) : L"Unknown error";
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) {
        message.pop_back();
    }
    return message;
}

std::wstring FormatRectSize(int width, int height) {
    return std::to_wstring(width) + L" x " + std::to_wstring(height) + L" px";
}

std::wstring ModifierLabel(UINT modifiers) {
    std::wstring label;
    modifiers &= ~MOD_NOREPEAT;
    if ((modifiers & MOD_CONTROL) != 0U) {
        label += L"Ctrl+";
    }
    if ((modifiers & MOD_ALT) != 0U) {
        label += L"Alt+";
    }
    if ((modifiers & MOD_SHIFT) != 0U) {
        label += L"Shift+";
    }
    if ((modifiers & MOD_WIN) != 0U) {
        label += L"Win+";
    }
    return label;
}

std::wstring HotkeyLabel(const HotkeyBinding& binding) {
    std::wstring label = ModifierLabel(binding.modifiers);
    if (binding.virtualKey == 0U) {
        if (!label.empty()) {
            label.pop_back();
        }
        return label.empty() ? L"None" : label;
    }

    wchar_t name[64]{};
    const UINT scanCode = MapVirtualKeyW(binding.virtualKey, MAPVK_VK_TO_VSC_EX) << 16;
    if (GetKeyNameTextW(static_cast<LONG>(scanCode), name, static_cast<int>(std::size(name))) > 0) {
        label += name;
    } else if (binding.virtualKey >= 'A' && binding.virtualKey <= 'Z') {
        label.push_back(static_cast<wchar_t>(binding.virtualKey));
    } else if (binding.virtualKey >= '0' && binding.virtualKey <= '9') {
        label.push_back(static_cast<wchar_t>(binding.virtualKey));
    } else if (binding.virtualKey >= VK_F1 && binding.virtualKey <= VK_F24) {
        label += L"F" + std::to_wstring(binding.virtualKey - VK_F1 + 1);
    } else if (binding.virtualKey == VK_SNAPSHOT) {
        label += L"Print Screen";
    } else if (binding.virtualKey == VK_SPACE) {
        label += L"Space";
    } else {
        label += L"VK " + std::to_wstring(binding.virtualKey);
    }
    return label;
}

std::wstring TimestampedFileName(const ImageData& image) {
    static std::atomic<unsigned long long> sequence{0};
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buffer[160]{};
    swprintf_s(buffer, L"%04u-%02u-%02u_%02u-%02u-%02u_%03u_%lu_%llu_%dx%d.png",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        GetCurrentProcessId(), sequence.fetch_add(1), image.width, image.height);
    return buffer;
}

std::filesystem::path LocalAppDataPath() {
    PWSTR path = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
        result = path;
        CoTaskMemFree(path);
    }
    return result;
}

std::filesystem::path EnsureAppDirectory() {
    auto directory = LocalAppDataPath() / L"FrameSnap";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return directory;
}

std::filesystem::path DefaultSaveFolder() {
    PWSTR path = nullptr;
    std::filesystem::path result = EnsureAppDirectory() / L"Captures";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &path))) {
        result = std::filesystem::path(path) / L"FrameSnap";
        CoTaskMemFree(path);
    }
    return result;
}

RECT VirtualScreenBounds() {
    return {
        GetSystemMetrics(SM_XVIRTUALSCREEN),
        GetSystemMetrics(SM_YVIRTUALSCREEN),
        GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
        GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN),
    };
}

RECT NormalizeRect(RECT rect) {
    if (rect.left > rect.right) {
        std::swap(rect.left, rect.right);
    }
    if (rect.top > rect.bottom) {
        std::swap(rect.top, rect.bottom);
    }
    return rect;
}

RECT IntersectRectSafe(const RECT& a, const RECT& b) {
    RECT result{};
    IntersectRect(&result, &a, &b);
    return result;
}

bool IsRectEmptySafe(const RECT& rect) {
    return rect.right <= rect.left || rect.bottom <= rect.top;
}

SIZE RectSize(const RECT& rect) {
    return {rect.right - rect.left, rect.bottom - rect.top};
}

std::wstring ColorToHex(COLORREF color) {
    wchar_t buffer[16]{};
    swprintf_s(buffer, L"#%02X%02X%02X", GetRValue(color), GetGValue(color), GetBValue(color));
    return buffer;
}

std::shared_ptr<ImageData> CaptureScreenSnapshotGdi(const RECT& bounds) {
    const RECT normalized = NormalizeRect(bounds);
    if (IsRectEmptySafe(normalized)) {
        return nullptr;
    }

    const auto wideWidth = static_cast<long long>(normalized.right) - normalized.left;
    const auto wideHeight = static_cast<long long>(normalized.bottom) - normalized.top;
    if (wideWidth > static_cast<long long>(kMaxImageBytes / 4) || wideHeight > static_cast<long long>(kMaxImageBytes / 4)) return nullptr;
    const int width = static_cast<int>(wideWidth);
    const int height = static_cast<int>(wideHeight);
    const auto imageBytes = ImageByteSize(width, height);
    if (!imageBytes) return nullptr;
    HDC screenDc = GetDC(nullptr);
    if (screenDc == nullptr) {
        return nullptr;
    }

    GdiSnapshotCache cache; // Release desktop pixels and GDI storage after this capture.
    if (!EnsureSnapshotCache(screenDc, width, height, cache)) {
        ReleaseDC(nullptr, screenDc);
        return nullptr;
    }
    const BOOL copied = BitBlt(cache.memoryDc, 0, 0, width, height, screenDc, normalized.left, normalized.top, SRCCOPY);

    auto image = std::make_shared<ImageData>();
    if (copied) {
        image->width = width;
        image->height = height;
        image->hdrSource = false;
        image->sourceRect = normalized;
        image->pixels.resize(*imageBytes);
        memcpy(image->pixels.data(), cache.bits, image->pixels.size());
        // GDI does not initialize alpha. Screenshots must be opaque in the editor, PNG, and clipboard.
        for (std::size_t offset = 3; offset < image->pixels.size(); offset += 4) image->pixels[offset] = 255;
    }

    ReleaseDC(nullptr, screenDc);
    return copied ? image : nullptr;
}

std::shared_ptr<ImageData> CropImage(const std::shared_ptr<ImageData>& image, const RECT& selection) {
    if (image == nullptr || !image->IsValid() ||
        static_cast<long long>(image->sourceRect.right) - image->sourceRect.left != image->width ||
        static_cast<long long>(image->sourceRect.bottom) - image->sourceRect.top != image->height) return nullptr;
    const RECT normalized = NormalizeRect(selection);
    const RECT clipped = IntersectRectSafe(normalized, image->sourceRect);
    if (IsRectEmptySafe(clipped)) {
        return nullptr;
    }

    auto cropped = std::make_shared<ImageData>();
    cropped->width = clipped.right - clipped.left;
    cropped->height = clipped.bottom - clipped.top;
    cropped->hdrSource = image->hdrSource;
    cropped->sourceRect = clipped;
    cropped->pixels.resize(static_cast<std::size_t>(cropped->width) * static_cast<std::size_t>(cropped->height) * 4U);

    const LONG srcOffsetX = clipped.left - image->sourceRect.left;
    const LONG srcOffsetY = clipped.top - image->sourceRect.top;
    for (LONG y = 0; y < cropped->height; ++y) {
        const auto* srcRow = image->pixels.data() +
            (static_cast<std::size_t>(srcOffsetY + y) * static_cast<std::size_t>(image->width) + static_cast<std::size_t>(srcOffsetX)) * 4U;
        auto* dstRow = cropped->pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(cropped->width) * 4U;
        memcpy(dstRow, srcRow, static_cast<std::size_t>(cropped->width) * 4U);
    }
    return cropped;
}

HICON CreateFrameSnapAppIcon(int size) {
    const int extent = std::max(16, size);
    Gdiplus::Bitmap bitmap(extent, extent, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&bitmap);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

    Gdiplus::SolidBrush outerBrush(Gdiplus::Color(255, 15, 23, 42));
    Gdiplus::SolidBrush centerBrush(Gdiplus::Color(255, 29, 78, 216));
    Gdiplus::Pen ringPen(Gdiplus::Color(255, 148, 163, 184), std::max(1.0f, extent / 24.0f));
    Gdiplus::Pen reticlePen(Gdiplus::Color(255, 255, 255, 255), std::max(1.8f, extent / 18.0f));
    reticlePen.SetStartCap(Gdiplus::LineCapRound);
    reticlePen.SetEndCap(Gdiplus::LineCapRound);

    const auto outerRect = Gdiplus::RectF(2.0f, 2.0f, static_cast<Gdiplus::REAL>(extent - 4), static_cast<Gdiplus::REAL>(extent - 4));
    graphics.FillEllipse(&outerBrush, outerRect);
    graphics.DrawEllipse(&ringPen, outerRect);

    const float centerX = extent / 2.0f;
    const float centerY = extent / 2.0f;
    const float innerRadius = extent / 5.2f;
    graphics.FillEllipse(&centerBrush, centerX - innerRadius, centerY - innerRadius, innerRadius * 2.0f, innerRadius * 2.0f);
    graphics.DrawLine(&reticlePen, centerX, 6.0f, centerX, centerY - innerRadius - 2.0f);
    graphics.DrawLine(&reticlePen, centerX, centerY + innerRadius + 2.0f, centerX, static_cast<float>(extent - 6));
    graphics.DrawLine(&reticlePen, 6.0f, centerY, centerX - innerRadius - 2.0f, centerY);
    graphics.DrawLine(&reticlePen, centerX + innerRadius + 2.0f, centerY, static_cast<float>(extent - 6), centerY);

    HBITMAP colorBitmap = nullptr;
    bitmap.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &colorBitmap);
    HBITMAP maskBitmap = CreateBitmap(extent, extent, 1, 1, nullptr);
    ICONINFO iconInfo{};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmColor = colorBitmap;
    iconInfo.hbmMask = maskBitmap;
    HICON icon = CreateIconIndirect(&iconInfo);
    DeleteObject(colorBitmap);
    DeleteObject(maskBitmap);
    return icon;
}

void RotateLog(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size < 256 * 1024) return;
    auto previous = path;
    previous += L".previous";
    std::filesystem::remove(previous, error);
    error.clear();
    std::filesystem::rename(path, previous, error);
}

void WriteMetricsLog(const CaptureMetrics& metrics, const ImageData& image) {
    auto path = EnsureAppDirectory() / L"metrics.log";
    RotateLog(path);
    std::wofstream stream(path, std::ios::app);
    if (!stream.is_open()) {
        return;
    }
    SYSTEMTIME st{};
    GetLocalTime(&st);
    stream << st.wYear << L"-" << st.wMonth << L"-" << st.wDay << L" "
           << st.wHour << L":" << st.wMinute << L":" << st.wSecond
           << L" overlay_us=" << metrics.hotkeyToOverlay.count()
           << L" clipboard_us=" << metrics.commitToClipboard.count()
           << L" preview_us=" << metrics.commitToPreview.count()
           << L" save_enqueue_us=" << metrics.commitToSaveEnqueue.count()
           << L" save_dropped=" << (metrics.saveDropped ? 1 : 0)
           << L" size=" << image.width << L"x" << image.height
           << L" hdr=" << (image.hdrSource ? 1 : 0)
           << L"\n";
}

bool IsRunAtStartupEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0,
            KEY_QUERY_VALUE,
            &key) != ERROR_SUCCESS) {
        return false;
    }

    DWORD type = 0;
    DWORD size = 0;
    const LONG result = RegGetValueW(
        key,
        nullptr,
        L"FrameSnap",
        RRF_RT_REG_SZ,
        &type,
        nullptr,
        &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_SZ && size > sizeof(wchar_t);
}

bool SetRunAtStartup(bool enabled) {
    constexpr auto runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    // Touch only this application's entry, never another screenshot product's.
    if (!enabled) {
        HKEY key = nullptr;
        const LONG opened = RegOpenKeyExW(HKEY_CURRENT_USER, runKey, 0, KEY_SET_VALUE, &key);
        if (opened == ERROR_FILE_NOT_FOUND) return true;
        if (opened != ERROR_SUCCESS) return false;
        const LONG removed = RegDeleteValueW(key, L"FrameSnap");
        RegCloseKey(key);
        return removed == ERROR_SUCCESS || removed == ERROR_FILE_NOT_FOUND;
    }
    std::wstring executable(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) return false;
    executable.resize(length);
    const std::wstring command = L"\"" + executable + L"\" --background";
    // The documented Run value limit is 260 characters, including arguments.
    if (command.size() >= 260) return false;
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, runKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    const LONG written = RegSetValueExW(key, L"FrameSnap", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return written == ERROR_SUCCESS;
}

bool IsPrintScreenSnippingEnabled() {
    DWORD value = 1;
    DWORD size = sizeof(value);
    const LONG result = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Control Panel\\Keyboard",
        L"PrintScreenKeyForSnippingEnabled",
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &size);
    if (result != ERROR_SUCCESS) {
        return true;
    }
    return value != 0;
}

}  // namespace util
