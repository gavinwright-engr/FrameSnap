#pragma once

#include "common.h"
#include "types.h"

namespace util {

std::wstring HResultMessage(HRESULT hr);
std::wstring FormatRectSize(int width, int height);
std::wstring ModifierLabel(UINT modifiers);
std::wstring HotkeyLabel(const HotkeyBinding& binding);
std::wstring TimestampedFileName(const ImageData& image);
std::filesystem::path LocalAppDataPath();
std::filesystem::path EnsureAppDirectory();
std::filesystem::path DefaultSaveFolder();
RECT VirtualScreenBounds();
RECT NormalizeRect(RECT rect);
RECT IntersectRectSafe(const RECT& a, const RECT& b);
bool IsRectEmptySafe(const RECT& rect);
SIZE RectSize(const RECT& rect);
std::wstring ColorToHex(COLORREF color);
std::shared_ptr<ImageData> CaptureScreenSnapshotGdi(const RECT& bounds);
std::shared_ptr<ImageData> CropImage(const std::shared_ptr<ImageData>& image, const RECT& selection);
HICON CreateFrameSnapAppIcon(int size);
void WriteMetricsLog(const CaptureMetrics& metrics, const ImageData& image);
bool IsRunAtStartupEnabled();
bool SetRunAtStartup(bool enabled, bool backgroundLaunch = true);
bool IsPrintScreenSnippingEnabled();
bool SetPrintScreenSnippingEnabled(bool enabled);

template <typename T>
constexpr T Clamp(T value, T minValue, T maxValue) {
    return std::max(minValue, std::min(value, maxValue));
}

}  // namespace util
