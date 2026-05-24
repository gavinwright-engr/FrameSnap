#include "app.h"

#include "util.h"

#include <mmsystem.h>

namespace {

constexpr UINT kTrayMessage = WM_APP + 20;
constexpr UINT kHotkeyId = 1;

enum TrayCommand {
    TrayCapture = 4001,
    TraySettings,
    TrayOpenFolder,
    TrayExit,
};

FrameSnapApp* gHookTarget = nullptr;

struct ChimeSpec {
    double leadInSeconds{};
    double bodySeconds{};
    double tailSeconds{};
    double splitSeconds{};
    double blendSeconds{};
    double attackSeconds{};
    double releaseSeconds{};
    double firstBaseHz{};
    double firstHarmonicHz{};
    double secondBaseHz{};
    double secondHarmonicHz{};
    double firstMix{};
    double firstHarmonicMix{};
    double secondMix{};
    double secondHarmonicMix{};
    double outputGain{};
};

bool IsModifierKey(UINT virtualKey) {
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

UINT CurrentModifierFlags(UINT activeKey = 0) {
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

void AppendStartupLog(const std::wstring& line) {
    const auto path = util::EnsureAppDirectory() / L"startup.log";
    std::wofstream stream(path, std::ios::app);
    if (!stream.is_open()) {
        return;
    }
    SYSTEMTIME st{};
    GetLocalTime(&st);
    stream << st.wYear << L"-" << st.wMonth << L"-" << st.wDay << L" "
           << st.wHour << L":" << st.wMinute << L":" << st.wSecond << L" "
           << line << L"\n";
}

std::vector<std::uint8_t> CreateCaptureSoundWav() {
    constexpr ChimeSpec kSpec{
        .leadInSeconds = 0.010,
        .bodySeconds = 0.106,
        .tailSeconds = 0.018,
        .splitSeconds = 0.047,
        .blendSeconds = 0.010,
        .attackSeconds = 0.008,
        .releaseSeconds = 0.030,
        .firstBaseHz = 1046.50,
        .firstHarmonicHz = 1567.98,
        .secondBaseHz = 1318.51,
        .secondHarmonicHz = 1975.53,
        .firstMix = 0.72,
        .firstHarmonicMix = 0.18,
        .secondMix = 0.60,
        .secondHarmonicMix = 0.14,
        .outputGain = 0.26,
    };
    constexpr int kSampleRate = 22050;
    constexpr double kPi = 3.14159265358979323846;
    const int leadInSamples = std::max(1, static_cast<int>(std::lround(kSampleRate * kSpec.leadInSeconds)));
    const int bodySamples = std::max(1, static_cast<int>(std::lround(kSampleRate * kSpec.bodySeconds)));
    const int tailSamples = std::max(1, static_cast<int>(std::lround(kSampleRate * kSpec.tailSeconds)));
    const int attackSamples = std::max(2, static_cast<int>(std::lround(kSampleRate * kSpec.attackSeconds)));
    const int releaseSamples = std::max(2, static_cast<int>(std::lround(kSampleRate * kSpec.releaseSeconds)));
    const int totalSamples = leadInSamples + bodySamples + tailSamples;

    std::vector<std::int16_t> samples;
    samples.reserve(static_cast<size_t>(totalSamples));
    const auto easeInOut = [](double progress) {
        progress = std::clamp(progress, 0.0, 1.0);
        return 0.5 - 0.5 * std::cos(progress * std::numbers::pi_v<double>);
    };

    for (int i = 0; i < leadInSamples; ++i) {
        samples.push_back(0);
    }

    for (int i = 0; i < bodySamples; ++i) {
        const double t = static_cast<double>(i) / kSampleRate;
        const double transitionStart = std::max(0.0, kSpec.splitSeconds - (kSpec.blendSeconds * 0.5));
        const double transitionEnd = std::min(kSpec.bodySeconds, kSpec.splitSeconds + (kSpec.blendSeconds * 0.5));

        double mixSecond = 0.0;
        if (t >= transitionEnd) {
            mixSecond = 1.0;
        } else if (t > transitionStart) {
            mixSecond = easeInOut((t - transitionStart) / std::max(0.001, transitionEnd - transitionStart));
        }
        const double mixFirst = 1.0 - mixSecond;

        double envelope = 1.0;
        if (i < attackSamples) {
            envelope *= easeInOut(static_cast<double>(i) / static_cast<double>(attackSamples - 1));
        }
        if (i >= (bodySamples - releaseSamples)) {
            const int releaseIndex = bodySamples - 1 - i;
            envelope *= easeInOut(static_cast<double>(releaseIndex) / static_cast<double>(releaseSamples - 1));
        }

        const double firstTone =
            kSpec.firstMix * std::sin(2.0 * kPi * kSpec.firstBaseHz * t) +
            kSpec.firstHarmonicMix * std::sin(2.0 * kPi * kSpec.firstHarmonicHz * t);
        const double secondTone =
            kSpec.secondMix * std::sin(2.0 * kPi * kSpec.secondBaseHz * t) +
            kSpec.secondHarmonicMix * std::sin(2.0 * kPi * kSpec.secondHarmonicHz * t);

        const double sample = ((mixFirst * firstTone) + (mixSecond * secondTone)) * envelope * kSpec.outputGain;
        const auto pcm = static_cast<std::int16_t>(std::clamp(sample, -1.0, 1.0) * 32767.0);
        samples.push_back(pcm);
    }

    for (int i = 0; i < tailSamples; ++i) {
        samples.push_back(0);
    }

    std::vector<std::uint8_t> wav;
    wav.reserve(44 + samples.size() * sizeof(std::int16_t));
    const auto appendBytes = [&wav](const auto* value, size_t size) {
        const auto* begin = reinterpret_cast<const std::uint8_t*>(value);
        wav.insert(wav.end(), begin, begin + size);
    };
    const auto appendFourCC = [&wav](const char (&fourcc)[5]) {
        wav.insert(wav.end(), fourcc, fourcc + 4);
    };
    const auto appendU32 = [&appendBytes](std::uint32_t value) {
        appendBytes(&value, sizeof(value));
    };
    const auto appendU16 = [&appendBytes](std::uint16_t value) {
        appendBytes(&value, sizeof(value));
    };

    const std::uint32_t dataSize = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
    appendFourCC("RIFF");
    appendU32(36U + dataSize);
    appendFourCC("WAVE");
    appendFourCC("fmt ");
    appendU32(16);
    appendU16(1);
    appendU16(1);
    appendU32(kSampleRate);
    appendU32(kSampleRate * sizeof(std::int16_t));
    appendU16(sizeof(std::int16_t));
    appendU16(16);
    appendFourCC("data");
    appendU32(dataSize);
    appendBytes(samples.data(), dataSize);
    return wav;
}

std::vector<std::uint8_t> CreateCaptureCompleteSoundWav() {
    constexpr ChimeSpec kSpec{
        .leadInSeconds = 0.010,
        .bodySeconds = 0.094,
        .tailSeconds = 0.018,
        .splitSeconds = 0.041,
        .blendSeconds = 0.010,
        .attackSeconds = 0.008,
        .releaseSeconds = 0.028,
        .firstBaseHz = 783.99,
        .firstHarmonicHz = 1174.66,
        .secondBaseHz = 987.77,
        .secondHarmonicHz = 1479.98,
        .firstMix = 0.72,
        .firstHarmonicMix = 0.16,
        .secondMix = 0.58,
        .secondHarmonicMix = 0.12,
        .outputGain = 0.21,
    };
    constexpr int kSampleRate = 22050;
    constexpr double kPi = 3.14159265358979323846;
    const int leadInSamples = std::max(1, static_cast<int>(std::lround(kSampleRate * kSpec.leadInSeconds)));
    const int bodySamples = std::max(1, static_cast<int>(std::lround(kSampleRate * kSpec.bodySeconds)));
    const int tailSamples = std::max(1, static_cast<int>(std::lround(kSampleRate * kSpec.tailSeconds)));
    const int attackSamples = std::max(2, static_cast<int>(std::lround(kSampleRate * kSpec.attackSeconds)));
    const int releaseSamples = std::max(2, static_cast<int>(std::lround(kSampleRate * kSpec.releaseSeconds)));
    const int totalSamples = leadInSamples + bodySamples + tailSamples;

    std::vector<std::int16_t> samples;
    samples.reserve(static_cast<size_t>(totalSamples));
    const auto easeInOut = [](double progress) {
        progress = std::clamp(progress, 0.0, 1.0);
        return 0.5 - 0.5 * std::cos(progress * std::numbers::pi_v<double>);
    };

    for (int i = 0; i < leadInSamples; ++i) {
        samples.push_back(0);
    }

    for (int i = 0; i < bodySamples; ++i) {
        const double t = static_cast<double>(i) / kSampleRate;
        const double transitionStart = std::max(0.0, kSpec.splitSeconds - (kSpec.blendSeconds * 0.5));
        const double transitionEnd = std::min(kSpec.bodySeconds, kSpec.splitSeconds + (kSpec.blendSeconds * 0.5));

        double mixSecond = 0.0;
        if (t >= transitionEnd) {
            mixSecond = 1.0;
        } else if (t > transitionStart) {
            mixSecond = easeInOut((t - transitionStart) / std::max(0.001, transitionEnd - transitionStart));
        }
        const double mixFirst = 1.0 - mixSecond;

        double envelope = 1.0;
        if (i < attackSamples) {
            envelope *= easeInOut(static_cast<double>(i) / static_cast<double>(attackSamples - 1));
        }
        if (i >= (bodySamples - releaseSamples)) {
            const int releaseIndex = bodySamples - 1 - i;
            envelope *= easeInOut(static_cast<double>(releaseIndex) / static_cast<double>(releaseSamples - 1));
        }

        const double firstTone =
            kSpec.firstMix * std::sin(2.0 * kPi * kSpec.firstBaseHz * t) +
            kSpec.firstHarmonicMix * std::sin(2.0 * kPi * kSpec.firstHarmonicHz * t);
        const double secondTone =
            kSpec.secondMix * std::sin(2.0 * kPi * kSpec.secondBaseHz * t) +
            kSpec.secondHarmonicMix * std::sin(2.0 * kPi * kSpec.secondHarmonicHz * t);

        const double sample = ((mixFirst * firstTone) + (mixSecond * secondTone)) * envelope * kSpec.outputGain;
        const auto pcm = static_cast<std::int16_t>(std::clamp(sample, -1.0, 1.0) * 32767.0);
        samples.push_back(pcm);
    }

    for (int i = 0; i < tailSamples; ++i) {
        samples.push_back(0);
    }

    std::vector<std::uint8_t> wav;
    wav.reserve(44 + samples.size() * sizeof(std::int16_t));
    const auto appendBytes = [&wav](const auto* value, size_t size) {
        const auto* begin = reinterpret_cast<const std::uint8_t*>(value);
        wav.insert(wav.end(), begin, begin + size);
    };
    const auto appendFourCC = [&wav](const char (&fourcc)[5]) {
        wav.insert(wav.end(), fourcc, fourcc + 4);
    };
    const auto appendU32 = [&appendBytes](std::uint32_t value) {
        appendBytes(&value, sizeof(value));
    };
    const auto appendU16 = [&appendBytes](std::uint16_t value) {
        appendBytes(&value, sizeof(value));
    };

    const std::uint32_t dataSize = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
    appendFourCC("RIFF");
    appendU32(36U + dataSize);
    appendFourCC("WAVE");
    appendFourCC("fmt ");
    appendU32(16);
    appendU16(1);
    appendU16(1);
    appendU32(kSampleRate);
    appendU32(kSampleRate * sizeof(std::int16_t));
    appendU16(sizeof(std::int16_t));
    appendU16(16);
    appendFourCC("data");
    appendU32(dataSize);
    appendBytes(samples.data(), dataSize);
    return wav;
}

HICON CreateFrameSnapTrayIcon() {
    constexpr int fallbackSize = 32;
    const int width = std::max(GetSystemMetrics(SM_CXSMICON), fallbackSize);
    const int height = std::max(GetSystemMetrics(SM_CYSMICON), fallbackSize);

    Gdiplus::Bitmap bitmap(width, height, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&bitmap);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

    Gdiplus::SolidBrush outerBrush(Gdiplus::Color(255, 15, 23, 42));
    Gdiplus::SolidBrush centerBrush(Gdiplus::Color(255, 29, 78, 216));
    Gdiplus::Pen ringPen(Gdiplus::Color(255, 148, 163, 184), 1.3f);
    Gdiplus::Pen reticlePen(Gdiplus::Color(255, 255, 255, 255), 2.0f);
    reticlePen.SetStartCap(Gdiplus::LineCapRound);
    reticlePen.SetEndCap(Gdiplus::LineCapRound);

    const auto outerRect = Gdiplus::RectF(2.0f, 2.0f, static_cast<Gdiplus::REAL>(width - 4), static_cast<Gdiplus::REAL>(height - 4));
    graphics.FillEllipse(&outerBrush, outerRect);
    graphics.DrawEllipse(&ringPen, outerRect);

    const float centerX = width / 2.0f;
    const float centerY = height / 2.0f;
    const float innerRadius = width / 5.4f;
    graphics.FillEllipse(&centerBrush, centerX - innerRadius, centerY - innerRadius, innerRadius * 2.0f, innerRadius * 2.0f);
    graphics.DrawLine(&reticlePen, centerX, 6.0f, centerX, centerY - innerRadius - 2.0f);
    graphics.DrawLine(&reticlePen, centerX, centerY + innerRadius + 2.0f, centerX, static_cast<float>(height - 6));
    graphics.DrawLine(&reticlePen, 6.0f, centerY, centerX - innerRadius - 2.0f, centerY);
    graphics.DrawLine(&reticlePen, centerX + innerRadius + 2.0f, centerY, static_cast<float>(width - 6), centerY);

    HBITMAP colorBitmap = nullptr;
    bitmap.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &colorBitmap);
    HBITMAP maskBitmap = CreateBitmap(width, height, 1, 1, nullptr);
    ICONINFO iconInfo{};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmColor = colorBitmap;
    iconInfo.hbmMask = maskBitmap;
    HICON icon = CreateIconIndirect(&iconInfo);
    DeleteObject(colorBitmap);
    DeleteObject(maskBitmap);
    return icon;
}

}  // namespace

FrameSnapApp::FrameSnapApp(HINSTANCE instance, bool launchBackground)
    : instance_(instance),
      launchBackground_(launchBackground) {}

FrameSnapApp::~FrameSnapApp() {
    StopShowSettingsListener();
    settingsStore_.Save(settings_);
    RemoveTrayIcon();
    if (trayIconHandle_ != nullptr) {
        DestroyIcon(trayIconHandle_);
        trayIconHandle_ = nullptr;
    }
    UnregisterAppHotkey();
    saveQueue_.Stop();
    clipboardPublisher_.Stop();
    if (gdiplusToken_ != 0) {
        Gdiplus::GdiplusShutdown(gdiplusToken_);
    }
}

bool FrameSnapApp::Initialize() {
    AppendStartupLog(L"Initialize: begin");
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);
    AppendStartupLog(L"Initialize: common controls");

    Gdiplus::GdiplusStartupInput startupInput;
    if (Gdiplus::GdiplusStartup(&gdiplusToken_, &startupInput, nullptr) != Gdiplus::Ok) {
        AppendStartupLog(L"Initialize: GDI+ startup failed");
        return false;
    }
    AppendStartupLog(L"Initialize: GDI+ ready");

    settings_ = settingsStore_.Load();
    AppendStartupLog(L"Initialize: settings loaded");
    AppendStartupLog(std::wstring(L"Initialize: launch_mode=") + (launchBackground_ ? L"background" : L"manual"));
    if (settings_.printScreenOverrideEnabled) {
        const bool overrideOk = util::SetPrintScreenSnippingEnabled(false);
        AppendStartupLog(std::wstring(L"Initialize: print_screen_override_requested=1 result=") + (overrideOk ? L"ok" : L"failed"));
    } else {
        AppendStartupLog(std::wstring(L"Initialize: print_screen_override_requested=0 current_windows_snipping=") +
            (util::IsPrintScreenSnippingEnabled() ? L"enabled" : L"disabled"));
    }
    util::SetRunAtStartup(settings_.runAtStartupEnabled, true);
    AppendStartupLog(L"Initialize: startup registration attempted");
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");

    if (!CreateMainWindow()) {
        AppendStartupLog(L"Initialize: main window creation failed");
        return false;
    }
    AppendStartupLog(L"Initialize: main window created");
    StartShowSettingsListener();

    overlay_ = std::make_unique<OverlayWindow>(instance_, hwnd_);
    preview_ = std::make_unique<PreviewWindow>(instance_, hwnd_);
    editor_ = std::make_unique<EditorWindow>(instance_, hwnd_);
    settingsWindow_ = std::make_unique<SettingsWindow>(instance_, hwnd_);
    AppendStartupLog(L"Initialize: child controllers ready");

    captureEngine_.Initialize();
    AppendStartupLog(L"Initialize: capture engine initialized");
    clipboardPublisher_.Start();
    AppendStartupLog(L"Initialize: clipboard thread started");
    saveQueue_.Start();
    AppendStartupLog(L"Initialize: save queue started");

    hotkeyRegistered_ = RegisterAppHotkey(settings_.hotkey);
    if (!hotkeyRegistered_) {
        AppendStartupLog(L"Initialize: hotkey registration failed for " + util::HotkeyLabel(settings_.hotkey));
    } else {
        AppendStartupLog(std::wstring(L"Initialize: hotkey registered via ") + (usingKeyboardHook_ ? L"hook" : L"registerhotkey"));
    }
    CreateTrayIcon();
    AppendStartupLog(L"Initialize: tray icon created");
    if (settingsWindow_ != nullptr && (!launchBackground_ || !hotkeyRegistered_)) {
        settingsWindow_->Show(settings_, BuildHotkeyStatusText(), BuildPrintScreenStatusText(), SW_SHOWNORMAL);
    }
    if (!hotkeyRegistered_) {
        const std::wstring message = L"FrameSnap couldn't register " + util::HotkeyLabel(settings_.hotkey) +
                                     L" because Windows or another app is already using it.\n\nRecord a different shortcut in Settings.";
        MessageBoxW(hwnd_, message.c_str(), kFrameSnapAppName, MB_OK | MB_ICONINFORMATION);
    }
    return true;
}

int FrameSnapApp::Run() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0)) {
        if (editor_ != nullptr && editor_->HandleAccelerator(message)) {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

bool FrameSnapApp::CreateMainWindow() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = FrameSnapApp::WndProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kFrameSnapMainWindowClassName;
    if (RegisterClassW(&wc) == 0) {
        const DWORD error = GetLastError();
        AppendStartupLog(L"CreateMainWindow: RegisterClassW failed " + std::to_wstring(error));
        return false;
    }

    hwnd_ = CreateWindowExW(
        0,
        kFrameSnapMainWindowClassName,
        kFrameSnapAppName,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        640,
        480,
        nullptr,
        nullptr,
        instance_,
        this);
    if (hwnd_ == nullptr) {
        const DWORD error = GetLastError();
        AppendStartupLog(L"CreateMainWindow: CreateWindowExW failed " + std::to_wstring(error));
    }
    return hwnd_ != nullptr;
}

void FrameSnapApp::CreateTrayIcon() {
    if (hwnd_ == nullptr) {
        return;
    }
    trayIcon_.cbSize = sizeof(trayIcon_);
    trayIcon_.hWnd = hwnd_;
    trayIcon_.uID = 1;
    trayIcon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    trayIcon_.uCallbackMessage = kTrayMessage;
    if (trayIconHandle_ == nullptr) {
        trayIconHandle_ = CreateFrameSnapTrayIcon();
    }
    trayIcon_.hIcon = trayIconHandle_ != nullptr ? trayIconHandle_ : LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(trayIcon_.szTip, kFrameSnapAppName);
    Shell_NotifyIconW(NIM_DELETE, &trayIcon_);
    if (Shell_NotifyIconW(NIM_ADD, &trayIcon_)) {
        trayIcon_.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &trayIcon_);
    }
}

void FrameSnapApp::RemoveTrayIcon() {
    if (trayIcon_.hWnd != nullptr) {
        Shell_NotifyIconW(NIM_DELETE, &trayIcon_);
        trayIcon_.hWnd = nullptr;
    }
}

void FrameSnapApp::ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, TrayCapture, L"Capture");
    AppendMenuW(menu, MF_STRING, TraySettings, L"Settings");
    AppendMenuW(menu, MF_STRING, TrayOpenFolder, L"Open save folder");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, TrayExit, L"Exit");

    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(hwnd_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
}

void FrameSnapApp::StartShowSettingsListener() {
    if (showSettingsEvent_ != nullptr) {
        return;
    }
    showSettingsEvent_ = CreateEventW(nullptr, FALSE, FALSE, kFrameSnapShowSettingsEventName);
    if (showSettingsEvent_ == nullptr) {
        AppendStartupLog(L"ShowSettingsListener: CreateEventW failed " + std::to_wstring(GetLastError()));
        return;
    }
    showSettingsThread_ = std::thread([this] {
        for (;;) {
            const DWORD result = WaitForSingleObject(showSettingsEvent_, INFINITE);
            if (result != WAIT_OBJECT_0 || shuttingDown_) {
                break;
            }
            PostMessageW(hwnd_, WM_APP_SHOW_SETTINGS, 0, 0);
        }
    });
}

void FrameSnapApp::StopShowSettingsListener() {
    shuttingDown_ = true;
    if (showSettingsEvent_ != nullptr) {
        SetEvent(showSettingsEvent_);
    }
    if (showSettingsThread_.joinable()) {
        showSettingsThread_.join();
    }
    if (showSettingsEvent_ != nullptr) {
        CloseHandle(showSettingsEvent_);
        showSettingsEvent_ = nullptr;
    }
}

bool FrameSnapApp::RegisterAppHotkey(const HotkeyBinding& binding) {
    UnregisterAppHotkey();
    const UINT modifiers = binding.modifiers & (MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN);
    if (binding.virtualKey != 0U &&
        RegisterHotKey(hwnd_, kHotkeyId, modifiers | MOD_NOREPEAT, binding.virtualKey) == TRUE) {
        usingKeyboardHook_ = false;
        return true;
    }

    const bool allowHookFallback = binding.virtualKey == VK_SNAPSHOT || modifiers == 0U;
    if (!allowHookFallback) {
        return false;
    }
    return RegisterKeyboardHook({modifiers, binding.virtualKey});
}

bool FrameSnapApp::RegisterKeyboardHook(const HotkeyBinding& binding) {
    hookedBinding_ = binding;
    hookKeyDown_ = false;
    gHookTarget = this;
    keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, FrameSnapApp::LowLevelKeyboardProc, instance_, 0);
    if (keyboardHook_ == nullptr) {
        if (gHookTarget == this) {
            gHookTarget = nullptr;
        }
        usingKeyboardHook_ = false;
        return false;
    }
    usingKeyboardHook_ = true;
    return true;
}

void FrameSnapApp::UnregisterAppHotkey() {
    UnregisterHotKey(hwnd_, kHotkeyId);
    UnregisterKeyboardHook();
}

void FrameSnapApp::UnregisterKeyboardHook() {
    if (keyboardHook_ != nullptr) {
        UnhookWindowsHookEx(keyboardHook_);
        keyboardHook_ = nullptr;
    }
    if (gHookTarget == this) {
        gHookTarget = nullptr;
    }
    usingKeyboardHook_ = false;
    hookKeyDown_ = false;
}

std::wstring FrameSnapApp::BuildHotkeyStatusText() const {
    const std::wstring label = util::HotkeyLabel(settings_.hotkey);
    if (hotkeyRegistered_) {
        if (usingKeyboardHook_) {
            return L"Status: ready through low-level hook for " + label;
        }
        return L"Status: ready through RegisterHotKey for " + label;
    }
    return L"Status: conflict or unsupported key for " + label;
}

std::wstring FrameSnapApp::BuildPrintScreenStatusText() const {
    const bool windowsSnippingEnabled = util::IsPrintScreenSnippingEnabled();
    if (settings_.printScreenOverrideEnabled) {
        return windowsSnippingEnabled
            ? L"Windows Print Screen snipping is still enabled. FrameSnap may need a restart or manual system toggle."
            : L"Windows Print Screen snipping is disabled. FrameSnap can own Print Screen-style keys.";
    }
    return windowsSnippingEnabled
        ? L"Windows Print Screen snipping is enabled."
        : L"Windows Print Screen snipping is disabled outside FrameSnap.";
}

void FrameSnapApp::ExitApplication() {
    if (shuttingDown_.exchange(true)) {
        return;
    }
    frozenFrame_.reset();
    if (overlay_ != nullptr) {
        overlay_->Cancel();
    }
    if (preview_ != nullptr) {
        preview_->Hide();
    }
    RemoveTrayIcon();
    if (settingsWindow_ != nullptr && settingsWindow_->Handle() != nullptr && IsWindow(settingsWindow_->Handle())) {
        DestroyWindow(settingsWindow_->Handle());
    }
    if (hwnd_ != nullptr && IsWindow(hwnd_)) {
        DestroyWindow(hwnd_);
    } else {
        PostQuitMessage(0);
    }
}

bool FrameSnapApp::HandleLowLevelKeyboard(WPARAM wParam, const KBDLLHOOKSTRUCT& info) {
    if ((info.flags & LLKHF_INJECTED) != 0 || hookedBinding_.virtualKey == 0U) {
        return false;
    }

    const UINT virtualKey = static_cast<UINT>(info.vkCode);
    const bool keyDown = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
    const bool keyUp = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;
    if (!keyDown && !keyUp) {
        return false;
    }

    if (virtualKey == hookedBinding_.virtualKey && keyUp) {
        hookKeyDown_ = false;
        return true;
    }

    if (!keyDown || virtualKey != hookedBinding_.virtualKey || hookKeyDown_ || IsModifierKey(virtualKey)) {
        return false;
    }

    const UINT activeModifiers = CurrentModifierFlags(virtualKey) & (MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN);
    const UINT expectedModifiers = hookedBinding_.modifiers & (MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN);
    if (activeModifiers != expectedModifiers) {
        return false;
    }

    hookKeyDown_ = true;
    PostMessageW(hwnd_, WM_HOTKEY, kHotkeyId, 0);
    return true;
}

void FrameSnapApp::BeginCapture() {
    if (overlay_ == nullptr || overlay_->IsActive()) {
        return;
    }
    if (preview_ != nullptr) {
        preview_->Hide();
    }
    if (editor_ != nullptr && editor_->Handle() != nullptr) {
        ShowWindow(editor_->Handle(), SW_HIDE);
    }
    DwmFlush();
    const auto hotkeyStart = std::chrono::steady_clock::now();
    if (settings_.soundEnabled) {
        static const auto captureSound = CreateCaptureSoundWav();
        if (!captureSound.empty()) {
            PlaySoundW(reinterpret_cast<LPCWSTR>(captureSound.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
        }
    }
    frozenFrame_ = util::CaptureScreenSnapshotGdi(util::VirtualScreenBounds());
    if (frozenFrame_ == nullptr) {
        frozenFrame_ = captureEngine_.Capture(util::VirtualScreenBounds());
    }
    overlay_->BeginSession(settings_, hotkeyStart, frozenFrame_);
    lastOverlayLatency_ = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - hotkeyStart);
}

void FrameSnapApp::HandleCaptureReady(std::unique_ptr<CaptureRequest> request) {
    auto result = frozenFrame_ != nullptr ? util::CropImage(frozenFrame_, request->selection) : captureEngine_.Capture(request->selection);
    frozenFrame_.reset();
    if (result == nullptr) {
        MessageBeep(MB_ICONERROR);
        return;
    }

    CaptureMetrics metrics{};
    metrics.hotkeyToOverlay = lastOverlayLatency_;

    clipboardPublisher_.PublishBlocking(result);
    metrics.commitToClipboard = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - request->commitTime);
    if (settings_.soundEnabled) {
        static const auto captureCompleteSound = CreateCaptureCompleteSoundWav();
        if (!captureCompleteSound.empty()) {
            PlaySoundW(reinterpret_cast<LPCWSTR>(captureCompleteSound.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
        }
    }

    if (settings_.autoSaveEnabled) {
        const auto savePath = (std::filesystem::path(settings_.saveFolder) / util::TimestampedFileName(*result)).wstring();
        result->savedPath = savePath;
        const auto saveStart = std::chrono::steady_clock::now();
        metrics.saveDropped = !saveQueue_.Enqueue({result, savePath});
        metrics.commitToSaveEnqueue = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - saveStart);
    }

    const auto previewStart = std::chrono::steady_clock::now();
    preview_->Show(result, settings_.previewTimeoutMs);
    metrics.commitToPreview = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - previewStart);
    util::WriteMetricsLog(metrics, *result);
}

void FrameSnapApp::ApplySettings(const AppSettings& settings) {
    AppSettings merged = settings;
    merged.penColor = settings_.penColor;
    merged.highlighterColor = settings_.highlighterColor;
    merged.penWidth = settings_.penWidth;
    merged.highlighterWidth = settings_.highlighterWidth;
    if (!util::SetPrintScreenSnippingEnabled(!merged.printScreenOverrideEnabled)) {
        MessageBoxW(hwnd_,
            L"FrameSnap couldn't update the Windows Print Screen override setting.",
            kFrameSnapAppName,
            MB_OK | MB_ICONWARNING);
        if (settingsWindow_ != nullptr) {
            settingsWindow_->UpdateStatus(BuildHotkeyStatusText(), BuildPrintScreenStatusText());
        }
        return;
    }
    if (!RegisterAppHotkey(merged.hotkey)) {
        hotkeyRegistered_ = RegisterAppHotkey(settings_.hotkey);
        const std::wstring message = L"FrameSnap couldn't register " + util::HotkeyLabel(merged.hotkey) +
                                     L". It is likely already in use or unsupported.";
        MessageBoxW(hwnd_, message.c_str(), kFrameSnapAppName, MB_OK | MB_ICONWARNING);
        if (settingsWindow_ != nullptr) {
            settingsWindow_->UpdateStatus(BuildHotkeyStatusText(), BuildPrintScreenStatusText());
        }
        return;
    }
    hotkeyRegistered_ = true;
    settings_ = merged;
    util::SetRunAtStartup(settings_.runAtStartupEnabled, true);
    settingsStore_.Save(settings_);
    AppendStartupLog(std::wstring(L"ApplySettings: hotkey_mode=") + (usingKeyboardHook_ ? L"hook" : L"registerhotkey") +
        L" print_screen_override=" + (settings_.printScreenOverrideEnabled ? L"1" : L"0"));
    if (settingsWindow_ != nullptr) {
        settingsWindow_->UpdateStatus(BuildHotkeyStatusText(), BuildPrintScreenStatusText());
    }
}

LRESULT CALLBACK FrameSnapApp::LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code < 0 || gHookTarget == nullptr) {
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }
    const auto* info = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    if (info != nullptr && gHookTarget->HandleLowLevelKeyboard(wParam, *info)) {
        return 1;
    }
    return CallNextHookEx(gHookTarget->keyboardHook_, code, wParam, lParam);
}

LRESULT CALLBACK FrameSnapApp::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<FrameSnapApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<FrameSnapApp*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }
    return self != nullptr ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT FrameSnapApp::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (taskbarCreatedMessage_ != 0 && message == taskbarCreatedMessage_) {
        CreateTrayIcon();
        return 0;
    }

    switch (message) {
    case WM_QUERYENDSESSION:
        settingsStore_.Save(settings_);
        return TRUE;
    case WM_ENDSESSION:
        if (wParam != 0) {
            ExitApplication();
        }
        return 0;
    case WM_CLOSE:
        ExitApplication();
        return 0;
    case WM_HOTKEY:
        BeginCapture();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case TrayCapture:
            BeginCapture();
            return 0;
        case TraySettings:
            settingsWindow_->Show(settings_, BuildHotkeyStatusText(), BuildPrintScreenStatusText(), SW_SHOWNORMAL);
            return 0;
        case TrayOpenFolder:
            ShellExecuteW(hwnd_, L"open", settings_.saveFolder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        case TrayExit:
            ExitApplication();
            return 0;
        default:
            break;
        }
        break;
    case WM_DISPLAYCHANGE:
        captureEngine_.RefreshOutputs();
        return 0;
    case WM_APP_CAPTURE_READY: {
        std::unique_ptr<CaptureRequest> request(reinterpret_cast<CaptureRequest*>(lParam));
        HandleCaptureReady(std::move(request));
        return 0;
    }
    case WM_APP_CAPTURE_CANCELLED:
        frozenFrame_.reset();
        return 0;
    case WM_APP_PREVIEW_CLICKED:
        if (const auto image = preview_->CurrentImage()) {
            editor_->Show(image, settings_);
        }
        return 0;
    case WM_APP_SETTINGS_APPLIED: {
        std::unique_ptr<AppSettings> settings(reinterpret_cast<AppSettings*>(lParam));
        ApplySettings(*settings);
        return 0;
    }
    case WM_APP_SHOW_SETTINGS:
        if (settingsWindow_ != nullptr) {
            const int showCommand = wParam == 1 ? SW_SHOWMINNOACTIVE : SW_SHOWNORMAL;
            settingsWindow_->Show(settings_, BuildHotkeyStatusText(), BuildPrintScreenStatusText(), showCommand);
        }
        return 0;
    case WM_APP_EXIT_REQUESTED:
        ExitApplication();
        return 0;
    case kTrayMessage:
        if (lParam == WM_CONTEXTMENU || lParam == WM_RBUTTONUP) {
            ShowTrayMenu();
        } else if (lParam == WM_LBUTTONDBLCLK) {
            BeginCapture();
        }
        return 0;
    case WM_DESTROY:
        hwnd_ = nullptr;
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}
