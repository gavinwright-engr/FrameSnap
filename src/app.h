#pragma once

#include "capture_engine.h"
#include "clipboard_publisher.h"
#include "common.h"
#include "editor_window.h"
#include "overlay_window.h"
#include "preview_window.h"
#include "save_queue.h"
#include "settings_store.h"
#include "settings_window.h"
#include "types.h"

class ScreenshotterApp {
public:
    explicit ScreenshotterApp(HINSTANCE instance);
    ~ScreenshotterApp();

    bool Initialize();
    int Run();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    bool CreateMainWindow();
    void CreateTrayIcon();
    void RemoveTrayIcon();
    void ShowTrayMenu();
    bool RegisterAppHotkey(const HotkeyBinding& binding);
    void UnregisterAppHotkey();
    void BeginCapture();
    void HandleCaptureReady(std::unique_ptr<CaptureRequest> request);
    void ApplySettings(const AppSettings& settings);

    HINSTANCE instance_{};
    HWND hwnd_{};
    SettingsStore settingsStore_{};
    AppSettings settings_{};
    CaptureEngine captureEngine_{};
    ClipboardPublisher clipboardPublisher_{};
    SaveQueue saveQueue_{};
    std::unique_ptr<OverlayWindow> overlay_;
    std::unique_ptr<PreviewWindow> preview_;
    std::unique_ptr<EditorWindow> editor_;
    std::unique_ptr<SettingsWindow> settingsWindow_;
    NOTIFYICONDATAW trayIcon_{};
    HICON trayIconHandle_{};
    ULONG_PTR gdiplusToken_{};
    std::chrono::microseconds lastOverlayLatency_{};
    bool hotkeyRegistered_{false};
};
