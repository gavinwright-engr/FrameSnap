#include "settings_window.h"

#include "util.h"

namespace {

constexpr wchar_t kSettingsClassName[] = L"OneShotSettingsWindow";
constexpr wchar_t kWindowTitle[] = L"OneShot Settings";
constexpr COLORREF kWindowColor = RGB(244, 247, 251);
constexpr COLORREF kPanelColor = RGB(255, 255, 255);
constexpr COLORREF kFieldColor = RGB(251, 253, 255);
constexpr COLORREF kTitleColor = RGB(17, 24, 39);
constexpr COLORREF kBodyColor = RGB(71, 85, 105);
constexpr COLORREF kAccentColor = RGB(29, 78, 216);
constexpr COLORREF kBorderColor = RGB(221, 228, 236);

enum ControlId {
    ControlAutoSave = 3001,
    ControlClickMode,
    ControlSound,
    ControlFolderEdit,
    ControlFolderBrowse,
    ControlHotkeyPreview,
    ControlRecordHotkey,
    ControlPreviewEdit,
    ControlThresholdEdit,
    ControlSaveButton,
};

std::wstring ReadWindowText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1U, L'\0');
    GetWindowTextW(control, buffer.data(), length + 1);
    return buffer.data();
}

RECT RectWithSize(LONG left, LONG top, LONG width, LONG height) {
    return {left, top, left + width, top + height};
}

void DrawPanel(HDC hdc, const RECT& rect) {
    HBRUSH panelBrush = CreateSolidBrush(kPanelColor);
    HPEN borderPen = CreatePen(PS_SOLID, 1, kBorderColor);
    HGDIOBJ oldBrush = SelectObject(hdc, panelBrush);
    HGDIOBJ oldPen = SelectObject(hdc, borderPen);
    RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, 18, 18);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(panelBrush);
    DeleteObject(borderPen);
}

}  // namespace

SettingsWindow::SettingsWindow(HINSTANCE instance, HWND owner)
    : instance_(instance), owner_(owner) {}

SettingsWindow::~SettingsWindow() {
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
        return (GetKeyState(vk) & 0x8000) != 0;
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
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPreference, sizeof(cornerPreference));
#ifdef DWMWA_SYSTEMBACKDROP_TYPE
    DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_MAINWINDOW;
    DwmSetWindowAttribute(hwnd_, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
#endif
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
    uiFont_ = CreateUiFont(18, FW_NORMAL);
    titleFont_ = CreateUiFont(24, FW_SEMIBOLD);
    hintFont_ = CreateUiFont(15, FW_NORMAL);

    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kSettingsClassName,
        kWindowTitle,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        664,
        472,
        owner_,
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
    hotkeyHint_ = CreateWindowW(
        L"STATIC",
        L"Press Record, then tap the shortcut exactly the way you want to use it.",
        WS_CHILD | WS_VISIBLE,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        instance_,
        nullptr);

    hotkeyPreview_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(ControlHotkeyPreview),
        instance_,
        nullptr);

    recordHotkeyButton_ = CreateWindowW(
        L"BUTTON",
        L"Record",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(ControlRecordHotkey),
        instance_,
        nullptr);

    clickModeCheckbox_ = CreateWindowW(
        L"BUTTON",
        L"Enable click-click rectangle mode",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(ControlClickMode),
        instance_,
        nullptr);

    soundCheckbox_ = CreateWindowW(
        L"BUTTON",
        L"Play a quick snip sound",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(ControlSound),
        instance_,
        nullptr);

    previewEdit_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_NUMBER,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(ControlPreviewEdit),
        instance_,
        nullptr);

    thresholdEdit_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_NUMBER,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(ControlThresholdEdit),
        instance_,
        nullptr);

    autoSaveCheckbox_ = CreateWindowW(
        L"BUTTON",
        L"Save every capture to disk",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(ControlAutoSave),
        instance_,
        nullptr);

    folderEdit_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(ControlFolderEdit),
        instance_,
        nullptr);

    browseButton_ = CreateWindowW(
        L"BUTTON",
        L"Browse",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(ControlFolderBrowse),
        instance_,
        nullptr);

    saveButton_ = CreateWindowW(
        L"BUTTON",
        L"Save Settings",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(ControlSaveButton),
        instance_,
        nullptr);
}

void SettingsWindow::ApplyFonts() {
    const std::array<HWND, 10> controls{
        hotkeyHint_,
        hotkeyPreview_,
        recordHotkeyButton_,
        autoSaveCheckbox_,
        clickModeCheckbox_,
        soundCheckbox_,
        folderEdit_,
        browseButton_,
        previewEdit_,
        thresholdEdit_,
    };
    for (const HWND control : controls) {
        if (control != nullptr) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        }
    }
    if (saveButton_ != nullptr) {
        SendMessageW(saveButton_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    }
}

void SettingsWindow::LayoutControls() {
    const RECT hotkeyPanel = RectWithSize(20, 78, 608, 104);
    const RECT capturePanel = RectWithSize(20, 198, 294, 178);
    const RECT storagePanel = RectWithSize(334, 198, 294, 178);

    MoveWindow(hotkeyHint_, hotkeyPanel.left + 18, hotkeyPanel.top + 16, 440, 20, TRUE);
    MoveWindow(hotkeyPreview_, hotkeyPanel.left + 18, hotkeyPanel.top + 48, 380, 34, TRUE);
    MoveWindow(recordHotkeyButton_, hotkeyPanel.left + 414, hotkeyPanel.top + 48, 150, 34, TRUE);

    MoveWindow(clickModeCheckbox_, capturePanel.left + 18, capturePanel.top + 34, 244, 24, TRUE);
    MoveWindow(soundCheckbox_, capturePanel.left + 18, capturePanel.top + 66, 200, 24, TRUE);
    MoveWindow(previewEdit_, capturePanel.left + 18, capturePanel.top + 124, 104, 30, TRUE);
    MoveWindow(thresholdEdit_, capturePanel.left + 156, capturePanel.top + 124, 104, 30, TRUE);

    MoveWindow(autoSaveCheckbox_, storagePanel.left + 18, storagePanel.top + 34, 226, 24, TRUE);
    MoveWindow(folderEdit_, storagePanel.left + 18, storagePanel.top + 124, 178, 30, TRUE);
    MoveWindow(browseButton_, storagePanel.left + 206, storagePanel.top + 124, 70, 30, TRUE);

    MoveWindow(saveButton_, 480, 390, 148, 34, TRUE);
}

void SettingsWindow::LoadSettings(const AppSettings& settings) {
    pendingHotkey_ = settings.hotkey;
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

void SettingsWindow::SetRecordingHotkey(bool recording, const std::wstring& previewOverride) {
    recordingHotkey_ = recording;
    SetWindowTextW(recordHotkeyButton_, recording ? L"Listening..." : L"Record");
    UpdateHotkeyPreview(previewOverride);
    if (recording) {
        SetFocus(hwnd_);
    }
}

bool SettingsWindow::TryRecordHotkey(UINT message, WPARAM wParam) {
    if (!recordingHotkey_) {
        return false;
    }

    const UINT virtualKey = static_cast<UINT>(wParam);
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

HBRUSH SettingsWindow::BrushForControl(HWND control) const {
    RECT rect{};
    GetWindowRect(control, &rect);
    MapWindowPoints(HWND_DESKTOP, hwnd_, reinterpret_cast<LPPOINT>(&rect), 2);
    POINT center{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
    const RECT hotkeyPanel = RectWithSize(20, 78, 608, 104);
    const RECT capturePanel = RectWithSize(20, 198, 294, 178);
    const RECT storagePanel = RectWithSize(334, 198, 294, 178);
    if (PtInRect(&hotkeyPanel, center) || PtInRect(&capturePanel, center) || PtInRect(&storagePanel, center)) {
        return panelBrush_;
    }
    return windowBrush_;
}

void SettingsWindow::Show(const AppSettings& settings) {
    if (!EnsureWindow()) {
        return;
    }
    LoadSettings(settings);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
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

LRESULT SettingsWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_SIZE:
        LayoutControls();
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (TryRecordHotkey(message, wParam)) {
            return 0;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd_, &ps);
        RECT client{};
        GetClientRect(hwnd_, &client);
        FillRect(hdc, &client, windowBrush_);

        HBRUSH accentBrush = CreateSolidBrush(kAccentColor);
        RECT accentBar = RectWithSize(0, 0, client.right, 10);
        FillRect(hdc, &accentBar, accentBrush);
        DeleteObject(accentBrush);

        SetBkMode(hdc, TRANSPARENT);
        SelectObject(hdc, titleFont_);
        SetTextColor(hdc, kTitleColor);
        RECT titleRect = RectWithSize(20, 20, client.right - 40, 30);
        DrawTextW(hdc, L"OneShot", -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(hdc, hintFont_);
        SetTextColor(hdc, kBodyColor);
        RECT subtitleRect = RectWithSize(20, 48, client.right - 40, 20);
        DrawTextW(hdc, L"Fast screenshot settings with a recorded hotkey.", -1, &subtitleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        const RECT hotkeyPanel = RectWithSize(20, 78, 608, 104);
        const RECT capturePanel = RectWithSize(20, 198, 294, 178);
        const RECT storagePanel = RectWithSize(334, 198, 294, 178);
        DrawPanel(hdc, hotkeyPanel);
        DrawPanel(hdc, capturePanel);
        DrawPanel(hdc, storagePanel);

        SelectObject(hdc, uiFont_);
        SetTextColor(hdc, kTitleColor);
        RECT hotkeyTitle = RectWithSize(hotkeyPanel.left + 18, hotkeyPanel.top + 14, 200, 20);
        DrawTextW(hdc, L"Screenshot hotkey", -1, &hotkeyTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT captureTitle = RectWithSize(capturePanel.left + 18, capturePanel.top + 14, 160, 20);
        DrawTextW(hdc, L"Capture behavior", -1, &captureTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT storageTitle = RectWithSize(storagePanel.left + 18, storagePanel.top + 14, 160, 20);
        DrawTextW(hdc, L"Storage and queue", -1, &storageTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(hdc, hintFont_);
        SetTextColor(hdc, kBodyColor);
        RECT previewLabel = RectWithSize(capturePanel.left + 18, capturePanel.top + 100, 120, 18);
        DrawTextW(hdc, L"Preview timeout (ms)", -1, &previewLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT thresholdLabel = RectWithSize(capturePanel.left + 156, capturePanel.top + 100, 120, 18);
        DrawTextW(hdc, L"Drag threshold (px)", -1, &thresholdLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT folderLabel = RectWithSize(storagePanel.left + 18, storagePanel.top + 100, 120, 18);
        DrawTextW(hdc, L"Save folder", -1, &folderLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, kBodyColor);
        return reinterpret_cast<LRESULT>(BrushForControl(reinterpret_cast<HWND>(lParam)));
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
        SetTextColor(hdc, kTitleColor);
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
            ShowWindow(hwnd_, SW_HIDE);
            return 0;
        }
        default:
            break;
        }
        break;
    case WM_CLOSE:
        SetRecordingHotkey(false);
        ShowWindow(hwnd_, SW_HIDE);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}
