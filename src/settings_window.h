#pragma once

#include "common.h"
#include "types.h"

class SettingsWindow {
public:
    SettingsWindow(HINSTANCE instance, HWND owner);
    ~SettingsWindow();

    void Show(const AppSettings& settings, const std::wstring& hotkeyStatus, const std::wstring& printScreenStatus, int showCommand = SW_SHOWNORMAL);
    void UpdateStatus(const std::wstring& hotkeyStatus, const std::wstring& printScreenStatus);
    HWND Handle() const;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    bool EnsureWindow();
    void CreateControls();
    void ApplyFonts();
    void ApplyWindowChrome() const;
    void LayoutControls();
    void LoadSettings(const AppSettings& settings);
    AppSettings ReadSettings() const;
    void BrowseForFolder();
    void UpdateHotkeyPreview(const std::wstring& overrideText = L"") const;
    void SetRecordingHotkey(bool recording, const std::wstring& previewOverride = L"");
    bool TryRecordHotkey(UINT message, WPARAM wParam);
    bool CommitRecordedHotkey(UINT message, UINT virtualKey);
    bool HandleLowLevelKeyboard(WPARAM wParam, const KBDLLHOOKSTRUCT& info);
    void StartRecordingHook();
    void StopRecordingHook();
    HBRUSH BrushForControl(HWND control) const;
    void DrawActionButton(const DRAWITEMSTRUCT& drawItem) const;
    static HFONT CreateUiFont(int height, int weight);
    static UINT CurrentModifierFlags(UINT activeKey = 0);
    static bool IsModifierKey(UINT virtualKey);

    HINSTANCE instance_{};
    HWND owner_{};
    HWND hwnd_{};
    HWND autoSaveCheckbox_{};
    HWND clickModeCheckbox_{};
    HWND soundCheckbox_{};
    HWND startupCheckbox_{};
    HWND printScreenOverrideCheckbox_{};
    HWND folderEdit_{};
    HWND browseButton_{};
    HWND hotkeyPreview_{};
    HWND recordHotkeyButton_{};
    HWND hotkeyHint_{};
    HWND hotkeyStatus_{};
    HWND printScreenStatus_{};
    HWND hotkeyNote_{};
    HWND appModeHint_{};
    HWND idleHint_{};
    HWND previewEdit_{};
    HWND thresholdEdit_{};
    HWND saveButton_{};
    HWND exitButton_{};
    HFONT uiFont_{};
    HFONT titleFont_{};
    HFONT hintFont_{};
    HBRUSH windowBrush_{};
    HBRUSH panelBrush_{};
    HBRUSH fieldBrush_{};
    HotkeyBinding pendingHotkey_{};
    RECT hotkeyPanelRect_{};
    RECT capturePanelRect_{};
    RECT storagePanelRect_{};
    RECT appPanelRect_{};
    HHOOK recordingHook_{};
    std::wstring hotkeyStatusText_;
    std::wstring printScreenStatusText_;
    bool recordingHotkey_{false};
};
