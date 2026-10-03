#pragma once

#include "common.h"
#include "image_layout.h"

struct FloatPoint {
    float x{};
    float y{};
};

enum class ActiveTool {
    Pen,
    Highlighter,
    EraseLine,
    EraseBrush,
    Eyedropper,
};

struct HotkeyBinding {
    UINT modifiers{MOD_CONTROL | MOD_ALT};
    UINT virtualKey{'S'};
};

struct AppSettings {
    HotkeyBinding hotkey{};
    bool runAtStartupEnabled{false};
    bool autoSaveEnabled{false};
    bool clickModeEnabled{true};
    bool soundEnabled{false};
    std::wstring saveFolder;
    UINT previewTimeoutMs{3000};
    UINT dragThresholdPx{4};
    float penWidth{3.0f};
    float highlighterWidth{14.0f};
    COLORREF penColor{RGB(255, 72, 72)};
    COLORREF highlighterColor{RGB(255, 230, 0)};
};

struct CaptureRequest {
    RECT selection{};
    bool clickModeCompletion{};
    std::chrono::steady_clock::time_point hotkeyStart{};
    std::chrono::steady_clock::time_point commitTime{};
};

struct CaptureFrame {
    HMONITOR monitor{};
    RECT bounds{};
    DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
    bool hdrSource{};
};

struct ImageData {
    int width{};
    int height{};
    bool hdrSource{};
    RECT sourceRect{};
    std::vector<std::uint8_t> pixels;
    std::wstring savedPath;

    bool IsValid() const {
        const auto size = ImageByteSize(width, height);
        return size.has_value() && pixels.size() == *size;
    }
};

struct CaptureMetrics {
    std::chrono::microseconds hotkeyToOverlay{};
    std::chrono::microseconds commitToClipboard{};
    std::chrono::microseconds commitToPreview{};
    std::chrono::microseconds commitToSaveEnqueue{};
    bool saveDropped{};
};

struct CaptureResult {
    std::shared_ptr<ImageData> image;
    CaptureMetrics metrics{};
};

struct SaveJob {
    std::shared_ptr<ImageData> image;
    std::wstring path;
};

struct Stroke {
    ActiveTool tool{ActiveTool::Pen};
    float width{1.0f};
    COLORREF color{RGB(255, 0, 0)};
    std::vector<FloatPoint> points;
};

struct MarkupDocument {
    std::shared_ptr<ImageData> baseImage;
    std::vector<Stroke> strokes;
};
