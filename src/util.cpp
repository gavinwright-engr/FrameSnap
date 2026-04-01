#include "util.h"

namespace util {

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
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buffer[128]{};
    swprintf_s(buffer, L"%04u-%02u-%02u_%02u-%02u-%02u_%dx%d.png",
        st.wYear,
        st.wMonth,
        st.wDay,
        st.wHour,
        st.wMinute,
        st.wSecond,
        image.width,
        image.height);
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
    auto directory = LocalAppDataPath() / L"OneShot";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return directory;
}

std::filesystem::path DefaultSaveFolder() {
    PWSTR path = nullptr;
    std::filesystem::path result = EnsureAppDirectory() / L"Captures";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &path))) {
        result = std::filesystem::path(path) / L"OneShot";
        CoTaskMemFree(path);
    }
    std::error_code error;
    std::filesystem::create_directories(result, error);
    return result;
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

void WriteMetricsLog(const CaptureMetrics& metrics, const ImageData& image) {
    auto path = EnsureAppDirectory() / L"metrics.log";
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

bool SetRunAtStartup(bool enabled) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE,
            nullptr,
            &key,
            nullptr) != ERROR_SUCCESS) {
        return false;
    }

    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    bool success = false;
    if (enabled) {
        success = RegSetValueExW(key,
                      L"OneShot",
                      0,
                      REG_SZ,
                      reinterpret_cast<const BYTE*>(exePath),
                      static_cast<DWORD>((wcslen(exePath) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    } else {
        success = RegDeleteValueW(key, L"OneShot") == ERROR_SUCCESS || GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    RegCloseKey(key);
    return success;
}

}  // namespace util
