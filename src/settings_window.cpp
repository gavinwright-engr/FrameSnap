#include "settings_window.h"

#include "util.h"

namespace {

constexpr wchar_t kSettingsClassName[] = L"FrameSnapSettingsWindow";
constexpr wchar_t kWindowTitle[] = L"FrameSnap";

constexpr int kWindowMinWidth = 980;
constexpr int kWindowMinHeight = 700;
constexpr int kWindowDefaultWidth = 1080;
constexpr int kWindowDefaultHeight = 820;
constexpr int kOuterPadding = 24;
constexpr int kPanelGap = 18;
constexpr int kHeaderHeight = 92;
constexpr int kHotkeyPanelHeight = 250;
constexpr int kSecondaryPanelHeight = 214;
constexpr int kButtonHeight = 40;
constexpr int kFieldHeight = 36;
constexpr int kPanelRadius = 22;

constexpr COLORREF kWindowColor = RGB(10, 14, 20);
constexpr COLORREF kHeaderColor = RGB(14, 19, 26);
constexpr COLORREF kPanelColor = RGB(20, 27, 36);
constexpr COLORREF kFieldColor = RGB(14, 19, 26);
constexpr COLORREF kTitleColor = RGB(232, 238, 247);
constexpr COLORREF kBodyColor = RGB(194, 203, 217);
constexpr COLORREF kMutedColor = RGB(135, 148, 166);
constexpr COLORREF kAccentColor = RGB(59, 130, 246);
constexpr COLORREF kAccentPressedColor = RGB(37, 99, 235);
constexpr COLORREF kAccentBorderColor = RGB(96, 165, 250);
constexpr COLORREF kBorderColor = RGB(43, 55, 72);
constexpr COLORREF kFieldBorderColor = RGB(52, 64, 82);

enum ControlId {
    ControlAutoSave = 3001,
    ControlClickMode,
    ControlSound,
    ControlStartup,
    ControlPrintScreenOverride,
    ControlFolderEdit,
    ControlFolderBrowse,
    ControlHotkeyPreview,
    ControlRecordHotkey,
    ControlPreviewEdit,
    ControlThresholdEdit,
    ControlSaveButton,
    ControlExitButton,
};

SettingsWindow* gRecordingHookOwner = nullptr;

std::wstring ReadWindowText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1U, L'\0');
    GetWindowTextW(control, buffer.data(), length + 1);
    return buffer.data();
}

RECT RectWithSize(LONG left, LONG top, LONG width, LONG height) {
    return {left, top, left + width, top + height};
}

void FillSolidRect(HDC hdc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
}

void DrawRoundedCard(HDC hdc, const RECT& rect, COLORREF fill, COLORREF border, int radius) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

}  // namespace

SettingsWindow::SettingsWindow(HINSTANCE instance, HWND owner)
    : instance_(instance), owner_(owner) {}

SettingsWindow::~SettingsWindow() {
    StopRecordingHook();
    if (uiFont_ != nullptr) {
        DeleteObject(uiFont_);
    }
    if (titleFont_ != nullptr) {
        DeleteObject(titleFont_);
    }
    if (hintFont_ != nullptr) {
        DeleteObject(hintFont_);
    }
    if (windowBrush_ != nullptr) {
        DeleteObject(windowBrush_);
    }
    if (panelBrush_ != nullptr) {
        DeleteObject(panelBrush_);
    }
    if (fieldBrush_ != nullptr) {
        DeleteObject(fieldBrush_);
    }
}

HFONT SettingsWindow::CreateUiFont(int height, int weight) {
    HFONT font = CreateFontW(
        -height,
        0,
        0,
        0,
        weight,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        VARIABLE_PITCH,
        L"Segoe UI Variable Text");
    if (font == nullptr) {
        font = CreateFontW(
            -height,
            0,
            0,
            0,
            weight,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            VARIABLE_PITCH,
            L"Segoe UI");
    }
    return font;
}

bool SettingsWindow::IsModifierKey(UINT virtualKey) {
    switch (virtualKey) {
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
        return true;
    default:
        return false;
    }
}

UINT SettingsWindow::CurrentModifierFlags(UINT activeKey) {
    UINT modifiers = 0;
    const auto isDown = [](int vk) {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    };
    if (isDown(VK_CONTROL) || activeKey == VK_CONTROL || activeKey == VK_LCONTROL || activeKey == VK_RCONTROL) {
        modifiers |= MOD_CONTROL;
    }
    if (isDown(VK_MENU) || activeKey == VK_MENU || activeKey == VK_LMENU || activeKey == VK_RMENU) {
        modifiers |= MOD_ALT;
    }
    if (isDown(VK_SHIFT) || activeKey == VK_SHIFT || activeKey == VK_LSHIFT || activeKey == VK_RSHIFT) {
        modifiers |= MOD_SHIFT;
    }
    if (isDown(VK_LWIN) || isDown(VK_RWIN) || activeKey == VK_LWIN || activeKey == VK_RWIN) {
        modifiers |= MOD_WIN;
    }
    return modifiers;
}

void SettingsWindow::ApplyWindowChrome() const {
    DWM_WINDOW_CORNER_PREFERENCE cornerPreference = DWMWCP_ROUND;
    const BOOL darkMode = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPreference, sizeof(cornerPreference));
#ifdef DWMWA_SYSTEMBACKDROP_TYPE
    DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_MAINWINDOW;
    DwmSetWindowAttribute(hwnd_, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
#endif
    const COLORREF captionColor = kHeaderColor;
    const COLORREF textColor = kTitleColor;
    const COLORREF borderColor = kBorderColor;
    DwmSetWindowAttribute(hwnd_, DWMWA_CAPTION_COLOR, &captionColor, sizeof(captionColor));
    DwmSetWindowAttribute(hwnd_, DWMWA_TEXT_COLOR, &textColor, sizeof(textColor));
    DwmSetWindowAttribute(hwnd_, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));
}

bool SettingsWindow::EnsureWindow() {
    if (hwnd_ != nullptr) {
        return true;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = SettingsWindow::WndProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kSettingsClassName;
    RegisterClassW(&wc);

    windowBrush_ = CreateSolidBrush(kWindowColor);
    panelBrush_ = CreateSolidBrush(kPanelColor);
    fieldBrush_ = CreateSolidBrush(kFieldColor);
    uiFont_ = CreateUiFont(17, FW_MEDIUM);
    titleFont_ = CreateUiFont(28, FW_SEMIBOLD);
    hintFont_ = CreateUiFont(14, FW_NORMAL);

    hwnd_ = CreateWindowExW(
        0,
        kSettingsClassName,
        kWindowTitle,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        kWindowDefaultWidth,
        kWindowDefaultHeight,
        nullptr,
        nullptr,
        instance_,
        this);
    if (hwnd_ == nullptr) {
        return false;
    }

    ApplyWindowChrome();
    CreateControls();
    ApplyFonts();
    LayoutControls();
    return true;
}

void SettingsWindow::CreateControls() {
    hotkeyHint_ = CreateWindowW(L"STATIC",
        L"Capture key or combo. Print Screen-style keys are supported here too.",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);

    hotkeyPreview_ = CreateWindowW(L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(ControlHotkeyPreview), instance_, nullptr);

    hotkeyStatus_ = CreateWindowW(L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);

    recordHotkeyButton_ = CreateWindowW(L"BUTTON", L"Record Hotkey",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(ControlRecordHotkey), instance_, nullptr);

    printScreenOverrideCheckbox_ = CreateWindowW(L"BUTTON", L"Let FrameSnap own Print Screen",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(ControlPrintScreenOverride), instance_, nullptr);

    printScreenStatus_ = CreateWindowW(L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);

    hotkeyNote_ = CreateWindowW(L"STATIC",
        L"If Windows still opens Snipping Tool first, turn off Bluetooth & devices > Keyboard > Use the Print Screen key to open screen snipping, or set HKCU\\Control Panel\\Keyboard\\PrintScreenKeyForSnippingEnabled = 0.",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);

    clickModeCheckbox_ = CreateWindowW(L"BUTTON", L"Click-click rectangle mode",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(ControlClickMode), instance_, nullptr);

    soundCheckbox_ = CreateWindowW(L"BUTTON", L"Play capture sounds",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(ControlSound), instance_, nullptr);

    previewEdit_ = CreateWindowW(L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(ControlPreviewEdit), instance_, nullptr);

    thresholdEdit_ = CreateWindowW(L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(ControlThresholdEdit), instance_, nullptr);

    autoSaveCheckbox_ = CreateWindowW(L"BUTTON", L"Auto-save every capture",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(ControlAutoSave), instance_, nullptr);

    folderEdit_ = CreateWindowW(L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(ControlFolderEdit), instance_, nullptr);

    browseButton_ = CreateWindowW(L"BUTTON", L"Browse",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(ControlFolderBrowse), instance_, nullptr);

    startupCheckbox_ = CreateWindowW(L"BUTTON", L"Start FrameSnap when I sign in",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(ControlStartup), instance_, nullptr);

    appModeHint_ = CreateWindowW(L"STATIC",
        L"Manual launch opens the shell. Windows startup launches minimized. Closing the window minimizes it; use Exit FrameSnap to fully quit.",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);

    idleHint_ = CreateWindowW(L"STATIC",
        L"To stay ready on a hotkey, FrameSnap keeps one background process alive. Idle work is event-driven only: the save thread and clipboard thread sleep until needed, and the frozen frame is released immediately on cancel.",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);

    saveButton_ = CreateWindowW(L"BUTTON", L"Save Settings",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(ControlSaveButton), instance_, nullptr);

    exitButton_ = CreateWindowW(L"BUTTON", L"Exit FrameSnap",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(ControlExitButton), instance_, nullptr);

    const std::array<HWND, 4> edits{hotkeyPreview_, previewEdit_, thresholdEdit_, folderEdit_};
    for (const HWND edit : edits) {
        SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(10, 10));
    }
}

void SettingsWindow::ApplyFonts() {
    const std::array<HWND, 16> controls{
        hotkeyHint_, hotkeyPreview_, hotkeyStatus_, recordHotkeyButton_, printScreenOverrideCheckbox_, printScreenStatus_,
        hotkeyNote_, autoSaveCheckbox_, clickModeCheckbox_, soundCheckbox_, startupCheckbox_, appModeHint_,
        folderEdit_, browseButton_, idleHint_, exitButton_
    };
    for (const HWND control : controls) {
        if (control != nullptr) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        }
    }

    const std::array<HWND, 3> smallerControls{previewEdit_, thresholdEdit_, saveButton_};
    for (const HWND control : smallerControls) {
        if (control != nullptr) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(hintFont_), TRUE);
        }
    }
}

void SettingsWindow::LayoutControls() {
    RECT client{};
    GetClientRect(hwnd_, &client);

    const int contentWidth = std::max<LONG>(0, client.right - (kOuterPadding * 2));
    const int columnWidth = std::max(320, (contentWidth - kPanelGap) / 2);
    const int rowTop = kHeaderHeight + kOuterPadding;

    hotkeyPanelRect_ = RectWithSize(kOuterPadding, rowTop, contentWidth, kHotkeyPanelHeight);
    capturePanelRect_ = RectWithSize(kOuterPadding, hotkeyPanelRect_.bottom + kPanelGap, columnWidth, kSecondaryPanelHeight);
    storagePanelRect_ = RectWithSize(capturePanelRect_.right + kPanelGap, capturePanelRect_.top, contentWidth - columnWidth - kPanelGap, kSecondaryPanelHeight);
    appPanelRect_ = RectWithSize(kOuterPadding, capturePanelRect_.bottom + kPanelGap, contentWidth,
        std::max<LONG>(140, client.bottom - capturePanelRect_.bottom - kPanelGap - kOuterPadding));

    const int panelPadding = 20;
    const int hotkeyContentTop = hotkeyPanelRect_.top + 54;
    const int recordButtonWidth = 160;
    const int previewWidth = std::max(220L, hotkeyPanelRect_.right - hotkeyPanelRect_.left - panelPadding * 3 - recordButtonWidth);
    MoveWindow(hotkeyHint_, hotkeyPanelRect_.left + panelPadding, hotkeyContentTop - 26, hotkeyPanelRect_.right - hotkeyPanelRect_.left - panelPadding * 2, 22, TRUE);
    MoveWindow(hotkeyPreview_, hotkeyPanelRect_.left + panelPadding, hotkeyContentTop + 6, previewWidth, kFieldHeight, TRUE);
    MoveWindow(recordHotkeyButton_, hotkeyPanelRect_.right - panelPadding - recordButtonWidth, hotkeyContentTop + 4, recordButtonWidth, kButtonHeight, TRUE);
    MoveWindow(hotkeyStatus_, hotkeyPanelRect_.left + panelPadding, hotkeyContentTop + 48, hotkeyPanelRect_.right - hotkeyPanelRect_.left - panelPadding * 2, 22, TRUE);
    MoveWindow(printScreenOverrideCheckbox_, hotkeyPanelRect_.left + panelPadding, hotkeyContentTop + 82, hotkeyPanelRect_.right - hotkeyPanelRect_.left - panelPadding * 2, 26, TRUE);
    MoveWindow(printScreenStatus_, hotkeyPanelRect_.left + panelPadding, hotkeyContentTop + 110, hotkeyPanelRect_.right - hotkeyPanelRect_.left - panelPadding * 2, 22, TRUE);
    MoveWindow(hotkeyNote_, hotkeyPanelRect_.left + panelPadding, hotkeyContentTop + 136, hotkeyPanelRect_.right - hotkeyPanelRect_.left - panelPadding * 2, 58, TRUE);

    const int captureContentTop = capturePanelRect_.top + 56;
    const int fieldWidth = std::max(120L, (capturePanelRect_.right - capturePanelRect_.left - panelPadding * 2 - 16) / 2);
    MoveWindow(clickModeCheckbox_, capturePanelRect_.left + panelPadding, captureContentTop, capturePanelRect_.right - capturePanelRect_.left - panelPadding * 2, 26, TRUE);
    MoveWindow(soundCheckbox_, capturePanelRect_.left + panelPadding, captureContentTop + 34, capturePanelRect_.right - capturePanelRect_.left - panelPadding * 2, 26, TRUE);
    MoveWindow(previewEdit_, capturePanelRect_.left + panelPadding, captureContentTop + 112, fieldWidth, kFieldHeight, TRUE);
    MoveWindow(thresholdEdit_, capturePanelRect_.left + panelPadding + fieldWidth + 16, captureContentTop + 112, fieldWidth, kFieldHeight, TRUE);

    const int storageContentTop = storagePanelRect_.top + 56;
    const int browseWidth = 120;
    const int folderWidth = std::max(180L, storagePanelRect_.right - storagePanelRect_.left - panelPadding * 2 - browseWidth - 12);
    MoveWindow(autoSaveCheckbox_, storagePanelRect_.left + panelPadding, storageContentTop, storagePanelRect_.right - storagePanelRect_.left - panelPadding * 2, 26, TRUE);
    MoveWindow(folderEdit_, storagePanelRect_.left + panelPadding, storageContentTop + 112, folderWidth, kFieldHeight, TRUE);
    MoveWindow(browseButton_, storagePanelRect_.left + panelPadding + folderWidth + 12, storageContentTop + 110, browseWidth, kButtonHeight, TRUE);

    const int appContentTop = appPanelRect_.top + 54;
    MoveWindow(startupCheckbox_, appPanelRect_.left + panelPadding, appContentTop, appPanelRect_.right - appPanelRect_.left - panelPadding * 2, 28, TRUE);
    MoveWindow(appModeHint_, appPanelRect_.left + panelPadding, appContentTop + 34, appPanelRect_.right - appPanelRect_.left - panelPadding * 2, 38, TRUE);
    MoveWindow(idleHint_, appPanelRect_.left + panelPadding, appContentTop + 78, appPanelRect_.right - appPanelRect_.left - panelPadding * 2, 52, TRUE);
    MoveWindow(exitButton_, appPanelRect_.right - panelPadding - 340, appPanelRect_.bottom - panelPadding - kButtonHeight, 160, kButtonHeight, TRUE);
    MoveWindow(saveButton_, appPanelRect_.right - panelPadding - 160, appPanelRect_.bottom - panelPadding - kButtonHeight, 160, kButtonHeight, TRUE);
}

void SettingsWindow::LoadSettings(const AppSettings& settings) {
    pendingHotkey_ = settings.hotkey;
    Button_SetCheck(startupCheckbox_, settings.runAtStartupEnabled ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(printScreenOverrideCheckbox_, settings.printScreenOverrideEnabled ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(autoSaveCheckbox_, settings.autoSaveEnabled ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(clickModeCheckbox_, settings.clickModeEnabled ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(soundCheckbox_, settings.soundEnabled ? BST_CHECKED : BST_UNCHECKED);
    SetWindowTextW(folderEdit_, settings.saveFolder.c_str());
    SetWindowTextW(previewEdit_, std::to_wstring(settings.previewTimeoutMs).c_str());
    SetWindowTextW(thresholdEdit_, std::to_wstring(settings.dragThresholdPx).c_str());
    SetRecordingHotkey(false);
}

AppSettings SettingsWindow::ReadSettings() const {
    AppSettings settings{};
    settings.runAtStartupEnabled = Button_GetCheck(startupCheckbox_) == BST_CHECKED;
    settings.printScreenOverrideEnabled = Button_GetCheck(printScreenOverrideCheckbox_) == BST_CHECKED;
    settings.autoSaveEnabled = Button_GetCheck(autoSaveCheckbox_) == BST_CHECKED;
    settings.clickModeEnabled = Button_GetCheck(clickModeCheckbox_) == BST_CHECKED;
    settings.soundEnabled = Button_GetCheck(soundCheckbox_) == BST_CHECKED;
    settings.saveFolder = ReadWindowText(folderEdit_);
    settings.previewTimeoutMs = static_cast<UINT>(_wtoi(ReadWindowText(previewEdit_).c_str()));
    settings.dragThresholdPx = static_cast<UINT>(_wtoi(ReadWindowText(thresholdEdit_).c_str()));
    settings.hotkey = pendingHotkey_;

    settings.previewTimeoutMs = settings.previewTimeoutMs == 0 ? 3000 : settings.previewTimeoutMs;
    settings.dragThresholdPx = settings.dragThresholdPx == 0 ? 4 : settings.dragThresholdPx;
    if (settings.saveFolder.empty()) {
        settings.saveFolder = util::DefaultSaveFolder().wstring();
    }
    if (settings.hotkey.virtualKey == 0U) {
        settings.hotkey.virtualKey = 'S';
    }
    return settings;
}

void SettingsWindow::BrowseForFolder() {
    ComPtr<IFileDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf())))) {
        return;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    if (SUCCEEDED(dialog->Show(hwnd_))) {
        ComPtr<IShellItem> item;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                SetWindowTextW(folderEdit_, path);
                CoTaskMemFree(path);
            }
        }
    }
}

void SettingsWindow::UpdateHotkeyPreview(const std::wstring& overrideText) const {
    const std::wstring text = overrideText.empty() ? util::HotkeyLabel(pendingHotkey_) : overrideText;
    SetWindowTextW(hotkeyPreview_, text.c_str());
}

void SettingsWindow::UpdateStatus(const std::wstring& hotkeyStatus, const std::wstring& printScreenStatus) {
    hotkeyStatusText_ = hotkeyStatus;
    printScreenStatusText_ = printScreenStatus;
    if (hotkeyStatus_ != nullptr) {
        SetWindowTextW(hotkeyStatus_, hotkeyStatusText_.c_str());
    }
    if (printScreenStatus_ != nullptr) {
        SetWindowTextW(printScreenStatus_, printScreenStatusText_.c_str());
    }
}

void SettingsWindow::StartRecordingHook() {
    if (recordingHook_ != nullptr) {
        return;
    }
    gRecordingHookOwner = this;
    recordingHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, SettingsWindow::LowLevelKeyboardProc, instance_, 0);
    if (recordingHook_ == nullptr && gRecordingHookOwner == this) {
        gRecordingHookOwner = nullptr;
    }
}

void SettingsWindow::StopRecordingHook() {
    if (recordingHook_ != nullptr) {
        UnhookWindowsHookEx(recordingHook_);
        recordingHook_ = nullptr;
    }
    if (gRecordingHookOwner == this) {
        gRecordingHookOwner = nullptr;
    }
}

void SettingsWindow::SetRecordingHotkey(bool recording, const std::wstring& previewOverride) {
    recordingHotkey_ = recording;
    if (recordingHotkey_) {
        StartRecordingHook();
    } else {
        StopRecordingHook();
    }
    SetWindowTextW(recordHotkeyButton_, recording ? L"Listening..." : L"Record Hotkey");
    InvalidateRect(recordHotkeyButton_, nullptr, FALSE);
    UpdateHotkeyPreview(previewOverride);
    if (recording) {
        SetFocus(hwnd_);
    }
}

bool SettingsWindow::CommitRecordedHotkey(UINT message, UINT virtualKey) {
    if (!recordingHotkey_) {
        return false;
    }

    if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) {
        if (virtualKey == VK_ESCAPE) {
            SetRecordingHotkey(false);
            return true;
        }
        const UINT modifiers = CurrentModifierFlags(virtualKey);
        if (IsModifierKey(virtualKey)) {
            const std::wstring preview = modifiers == 0U ? L"Listening..." : util::ModifierLabel(modifiers) + L"...";
            UpdateHotkeyPreview(preview);
            return true;
        }
        pendingHotkey_ = HotkeyBinding{modifiers, virtualKey};
        SetRecordingHotkey(false);
        return true;
    }

    if (message == WM_KEYUP || message == WM_SYSKEYUP) {
        const UINT modifiers = CurrentModifierFlags();
        const std::wstring preview = modifiers == 0U ? L"Listening..." : util::ModifierLabel(modifiers) + L"...";
        UpdateHotkeyPreview(preview);
        return true;
    }

    return false;
}

bool SettingsWindow::TryRecordHotkey(UINT message, WPARAM wParam) {
    return CommitRecordedHotkey(message, static_cast<UINT>(wParam));
}

bool SettingsWindow::HandleLowLevelKeyboard(WPARAM wParam, const KBDLLHOOKSTRUCT& info) {
    if ((info.flags & LLKHF_INJECTED) != 0) {
        return false;
    }
    const bool handled = CommitRecordedHotkey(static_cast<UINT>(wParam), static_cast<UINT>(info.vkCode));
    return recordingHotkey_ || handled;
}

HBRUSH SettingsWindow::BrushForControl(HWND control) const {
    RECT rect{};
    GetWindowRect(control, &rect);
    MapWindowPoints(HWND_DESKTOP, hwnd_, reinterpret_cast<LPPOINT>(&rect), 2);
    POINT center{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
    if (PtInRect(&hotkeyPanelRect_, center) || PtInRect(&capturePanelRect_, center) || PtInRect(&storagePanelRect_, center) ||
        PtInRect(&appPanelRect_, center)) {
        return panelBrush_;
    }
    return windowBrush_;
}

void SettingsWindow::DrawActionButton(const DRAWITEMSTRUCT& drawItem) const {
    const bool pressed = (drawItem.itemState & ODS_SELECTED) != 0;
    const bool disabled = (drawItem.itemState & ODS_DISABLED) != 0;
    const UINT controlId = drawItem.CtlID;
    const bool accent = controlId == ControlSaveButton || (controlId == ControlRecordHotkey && recordingHotkey_);

    COLORREF fill = accent ? kAccentColor : kPanelColor;
    COLORREF border = accent ? kAccentBorderColor : kFieldBorderColor;
    COLORREF text = accent ? RGB(239, 246, 255) : kTitleColor;
    if (pressed) {
        fill = accent ? kAccentPressedColor : RGB(28, 36, 48);
    }
    if (disabled) {
        fill = RGB(24, 31, 42);
        border = RGB(42, 52, 68);
        text = kMutedColor;
    }

    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(drawItem.hDC, brush);
    HGDIOBJ oldPen = SelectObject(drawItem.hDC, pen);
    RoundRect(drawItem.hDC, drawItem.rcItem.left, drawItem.rcItem.top, drawItem.rcItem.right, drawItem.rcItem.bottom, 16, 16);
    SelectObject(drawItem.hDC, oldBrush);
    SelectObject(drawItem.hDC, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);

    SetBkMode(drawItem.hDC, TRANSPARENT);
    SetTextColor(drawItem.hDC, text);
    SelectObject(drawItem.hDC, uiFont_);
    RECT textRect = drawItem.rcItem;
    const std::wstring label = ReadWindowText(drawItem.hwndItem);
    DrawTextW(drawItem.hDC, label.c_str(), -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void SettingsWindow::Show(const AppSettings& settings, const std::wstring& hotkeyStatus, const std::wstring& printScreenStatus, int showCommand) {
    if (!EnsureWindow()) {
        return;
    }
    LoadSettings(settings);
    UpdateStatus(hotkeyStatus, printScreenStatus);
    ShowWindow(hwnd_, showCommand);
    if (showCommand != SW_SHOWMINNOACTIVE && showCommand != SW_SHOWMINIMIZED && showCommand != SW_MINIMIZE) {
        SetForegroundWindow(hwnd_);
    }
}

HWND SettingsWindow::Handle() const {
    return hwnd_;
}

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<SettingsWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }
    return self != nullptr ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK SettingsWindow::LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code < 0 || gRecordingHookOwner == nullptr) {
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }
    const auto* info = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    if (info != nullptr && gRecordingHookOwner->HandleLowLevelKeyboard(wParam, *info)) {
        return 1;
    }
    return CallNextHookEx(gRecordingHookOwner->recordingHook_, code, wParam, lParam);
}

LRESULT SettingsWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = kWindowMinWidth;
        info->ptMinTrackSize.y = kWindowMinHeight;
        return 0;
    }
    case WM_SIZE:
        LayoutControls();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (TryRecordHotkey(message, wParam)) {
            return 0;
        }
        break;
    case WM_DRAWITEM: {
        auto* drawItem = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (drawItem != nullptr &&
            (drawItem->CtlID == ControlRecordHotkey || drawItem->CtlID == ControlFolderBrowse || drawItem->CtlID == ControlSaveButton ||
                drawItem->CtlID == ControlExitButton)) {
            DrawActionButton(*drawItem);
            return TRUE;
        }
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd_, &ps);
        RECT client{};
        GetClientRect(hwnd_, &client);

        const int width = std::max(1L, client.right - client.left);
        const int height = std::max(1L, client.bottom - client.top);
        HDC backDc = CreateCompatibleDC(hdc);
        HBITMAP backBitmap = CreateCompatibleBitmap(hdc, width, height);
        HGDIOBJ oldBitmap = SelectObject(backDc, backBitmap);

        FillSolidRect(backDc, client, kWindowColor);
        FillSolidRect(backDc, RectWithSize(0, 0, client.right, kHeaderHeight), kHeaderColor);
        FillSolidRect(backDc, RectWithSize(0, kHeaderHeight - 1, client.right, 1), kBorderColor);

        DrawRoundedCard(backDc, hotkeyPanelRect_, kPanelColor, kBorderColor, kPanelRadius);
        DrawRoundedCard(backDc, capturePanelRect_, kPanelColor, kBorderColor, kPanelRadius);
        DrawRoundedCard(backDc, storagePanelRect_, kPanelColor, kBorderColor, kPanelRadius);
        DrawRoundedCard(backDc, appPanelRect_, kPanelColor, kBorderColor, kPanelRadius);

        SetBkMode(backDc, TRANSPARENT);
        SelectObject(backDc, titleFont_);
        SetTextColor(backDc, kTitleColor);
        RECT titleRect = RectWithSize(kOuterPadding, 20, client.right - kOuterPadding * 2, 34);
        DrawTextW(backDc, L"FrameSnap", -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(backDc, hintFont_);
        SetTextColor(backDc, kBodyColor);
        RECT subtitleRect = RectWithSize(kOuterPadding, 54, client.right - kOuterPadding * 2, 22);
        DrawTextW(backDc, L"Shell, hotkeys, startup, and Print Screen ownership in one place.", -1, &subtitleRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        SelectObject(backDc, uiFont_);
        SetTextColor(backDc, kTitleColor);
        RECT hotkeyTitle = RectWithSize(hotkeyPanelRect_.left + 20, hotkeyPanelRect_.top + 16, 260, 20);
        RECT captureTitle = RectWithSize(capturePanelRect_.left + 20, capturePanelRect_.top + 16, 260, 20);
        RECT storageTitle = RectWithSize(storagePanelRect_.left + 20, storagePanelRect_.top + 16, 260, 20);
        RECT appTitle = RectWithSize(appPanelRect_.left + 20, appPanelRect_.top + 16, 320, 20);
        DrawTextW(backDc, L"Hotkey and key capture", -1, &hotkeyTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DrawTextW(backDc, L"Capture behavior", -1, &captureTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DrawTextW(backDc, L"Storage", -1, &storageTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DrawTextW(backDc, L"App behavior", -1, &appTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(backDc, hintFont_);
        SetTextColor(backDc, kMutedColor);
        RECT previewLabel = RectWithSize(capturePanelRect_.left + 20, capturePanelRect_.top + 146, 140, 18);
        RECT thresholdLabel = RectWithSize(capturePanelRect_.left + 20 + ((capturePanelRect_.right - capturePanelRect_.left - 40 - 16) / 2) + 16,
            capturePanelRect_.top + 146, 160, 18);
        RECT folderLabel = RectWithSize(storagePanelRect_.left + 20, storagePanelRect_.top + 146, 180, 18);
        DrawTextW(backDc, L"Preview timeout (ms)", -1, &previewLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DrawTextW(backDc, L"Drag threshold (px)", -1, &thresholdLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DrawTextW(backDc, L"Save folder", -1, &folderLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        BitBlt(hdc, 0, 0, width, height, backDc, 0, 0, SRCCOPY);
        SelectObject(backDc, oldBitmap);
        DeleteObject(backBitmap);
        DeleteDC(backDc);
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND control = reinterpret_cast<HWND>(lParam);
        SetBkMode(hdc, TRANSPARENT);
        const bool muted = control == hotkeyHint_ || control == hotkeyNote_ || control == idleHint_ || control == appModeHint_;
        const bool strong = control == hotkeyStatus_ || control == printScreenStatus_;
        SetTextColor(hdc, strong ? kTitleColor : (muted ? kMutedColor : kBodyColor));
        return reinterpret_cast<LRESULT>(BrushForControl(control));
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkColor(hdc, kFieldColor);
        SetTextColor(hdc, kTitleColor);
        return reinterpret_cast<LRESULT>(fieldBrush_);
    }
    case WM_CTLCOLORBTN: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkColor(hdc, kPanelColor);
        SetTextColor(hdc, kBodyColor);
        return reinterpret_cast<LRESULT>(BrushForControl(reinterpret_cast<HWND>(lParam)));
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ControlFolderBrowse:
            BrowseForFolder();
            return 0;
        case ControlRecordHotkey:
            SetRecordingHotkey(!recordingHotkey_, recordingHotkey_ ? L"" : L"Listening...");
            return 0;
        case ControlSaveButton: {
            auto settings = std::make_unique<AppSettings>(ReadSettings());
            PostMessageW(owner_, WM_APP_SETTINGS_APPLIED, 0, reinterpret_cast<LPARAM>(settings.release()));
            return 0;
        }
        case ControlExitButton:
            PostMessageW(owner_, WM_APP_EXIT_REQUESTED, 0, 0);
            return 0;
        default:
            break;
        }
        break;
    case WM_CLOSE:
        SetRecordingHotkey(false);
        ShowWindow(hwnd_, SW_MINIMIZE);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}
