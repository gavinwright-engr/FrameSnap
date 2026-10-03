#pragma once

#include "capture_engine.h"
#include "clipboard_publisher.h"
#include "editor_window.h"
#include "launch_options.h"
#include "overlay_window.h"
#include "preview_window.h"
#include "save_queue.h"
#include "settings_store.h"
#include "settings_window.h"

class FrameSnapApp {
public:
    FrameSnapApp(HINSTANCE instance, LaunchMode mode);
    ~FrameSnapApp();
    bool Initialize();
    int Run();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    bool CreateMainWindow();
    void CreateTrayIcon();
    void RemoveTrayIcon();
    void ShowTrayMenu(POINT point);
    bool RegisterAppHotkey(const HotkeyBinding& binding);
    void UnregisterAppHotkey();
    std::wstring BuildHotkeyStatusText() const;
    std::wstring BuildPrintScreenStatusText() const;
    void ShowSettings();
    void ExitApplication();
    void FinishSession();
    void FinishExit();
    void ReportError(const wchar_t* message);
    void BeginCapture();
    void HandleCaptureReady(std::unique_ptr<CaptureRequest> request);
    void ApplySettings(const AppSettings& settings);

    HINSTANCE instance_{};
    HWND hwnd_{};
    SettingsStore settingsStore_{};
    AppSettings settings_{};
    SaveQueue saveQueue_{};
    std::unique_ptr<OverlayWindow> overlay_;
    std::unique_ptr<PreviewWindow> preview_;
    std::unique_ptr<EditorWindow> editor_;
    std::unique_ptr<SettingsWindow> settingsWindow_;
    NOTIFYICONDATAW trayIcon_{};
    HICON trayIconHandle_{};
    ULONG_PTR gdiplusToken_{};
    std::shared_ptr<ImageData> frozenFrame_;
    std::chrono::microseconds lastOverlayLatency_{};
    UINT taskbarCreatedMessage_{};
    LaunchMode mode_;
    unsigned pendingSaves_{};
    int exitCode_{};
    bool hotkeyRegistered_{};
    bool settingsLoaded_{};
    bool trayVersion4_{};
    bool shuttingDown_{};
};
