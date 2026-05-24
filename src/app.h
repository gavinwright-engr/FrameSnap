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

class FrameSnapApp {
public:
    FrameSnapApp(HINSTANCE instance, bool launchBackground);
    ~FrameSnapApp();

    bool Initialize();
    int Run();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    bool CreateMainWindow();
    void CreateTrayIcon();
    void RemoveTrayIcon();
    void ShowTrayMenu();
    void StartShowSettingsListener();
    void StopShowSettingsListener();
    bool RegisterAppHotkey(const HotkeyBinding& binding);
    bool RegisterKeyboardHook(const HotkeyBinding& binding);
    void UnregisterAppHotkey();
    void UnregisterKeyboardHook();
    bool HandleLowLevelKeyboard(WPARAM wParam, const KBDLLHOOKSTRUCT& info);
    std::wstring BuildHotkeyStatusText() const;
    std::wstring BuildPrintScreenStatusText() const;
    void ExitApplication();
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
    HHOOK keyboardHook_{};
    HANDLE showSettingsEvent_{};
    ULONG_PTR gdiplusToken_{};
    std::thread showSettingsThread_;
    std::shared_ptr<ImageData> frozenFrame_;
    std::chrono::microseconds lastOverlayLatency_{};
    HotkeyBinding hookedBinding_{};
    UINT taskbarCreatedMessage_{};
    bool usingKeyboardHook_{false};
    bool hookKeyDown_{false};
    bool hotkeyRegistered_{false};
    bool launchBackground_{false};
    std::atomic<bool> shuttingDown_{false};
};
