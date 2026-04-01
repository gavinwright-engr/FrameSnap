#pragma once

#include "common.h"
#include "types.h"

class SettingsWindow {
public:
    SettingsWindow(HINSTANCE instance, HWND owner);
    ~SettingsWindow();

    void Show(const AppSettings& settings);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
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
    HBRUSH BrushForControl(HWND control) const;
    static HFONT CreateUiFont(int height, int weight);
    static UINT CurrentModifierFlags(UINT activeKey = 0);
    static bool IsModifierKey(UINT virtualKey);

    HINSTANCE instance_{};
    HWND owner_{};
    HWND hwnd_{};
    HWND autoSaveCheckbox_{};
    HWND clickModeCheckbox_{};
    HWND soundCheckbox_{};
    HWND folderEdit_{};
    HWND browseButton_{};
    HWND hotkeyPreview_{};
    HWND recordHotkeyButton_{};
    HWND hotkeyHint_{};
    HWND previewEdit_{};
    HWND thresholdEdit_{};
    HWND saveButton_{};
    HFONT uiFont_{};
    HFONT titleFont_{};
    HFONT hintFont_{};
    HBRUSH windowBrush_{};
    HBRUSH panelBrush_{};
    HBRUSH fieldBrush_{};
    HotkeyBinding pendingHotkey_{};
    bool recordingHotkey_{false};
};
