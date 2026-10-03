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

void AppendStartupLog(const std::wstring& line) {
    const auto path = util::EnsureAppDirectory() / L"startup.log";
    util::RotateLog(path);
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

} // namespace

FrameSnapApp::FrameSnapApp(HINSTANCE instance, LaunchMode mode)
    : instance_(instance), mode_(mode) {}

FrameSnapApp::~FrameSnapApp() {
    UnregisterAppHotkey();
    RemoveTrayIcon();
    saveQueue_.Stop();
    if (settingsLoaded_) settingsStore_.Save(settings_);
    // Destroy windows/controllers while their fonts, callbacks, and GDI+ are alive.
    overlay_.reset();
    preview_.reset();
    editor_.reset();
    settingsWindow_.reset();
    if (hwnd_ != nullptr && IsWindow(hwnd_)) DestroyWindow(hwnd_);
    if (trayIconHandle_ != nullptr) DestroyIcon(trayIconHandle_);
    if (gdiplusToken_ != 0) Gdiplus::GdiplusShutdown(gdiplusToken_);
}

bool FrameSnapApp::Initialize() {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    if (!InitCommonControlsEx(&icc)) return false;
    Gdiplus::GdiplusStartupInput startupInput;
    if (Gdiplus::GdiplusStartup(&gdiplusToken_, &startupInput, nullptr) != Gdiplus::Ok) return false;
    settings_ = settingsStore_.Load();
    settingsLoaded_ = true;
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    if (!CreateMainWindow()) return false;
    // Startup registration and Windows keyboard preferences are never rewritten on launch.
    if (mode_ == LaunchMode::Capture) {
        PostMessageW(hwnd_, WM_APP_BEGIN_CAPTURE, 0, 0);
    } else {
        if (mode_ == LaunchMode::Background) hotkeyRegistered_ = RegisterAppHotkey(settings_.hotkey);
        CreateTrayIcon();
        if (mode_ == LaunchMode::Settings) ShowSettings();
        // Sign-in is always silent, even if the hotkey is already in use.
        if (mode_ == LaunchMode::Background && !hotkeyRegistered_) AppendStartupLog(L"Hotkey unavailable; choose another in Settings.");
    }
    return true;
}

int FrameSnapApp::Run() {
    MSG message{};
    BOOL result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (editor_ != nullptr && editor_->HandleAccelerator(message)) continue;
        if (settingsWindow_ != nullptr && settingsWindow_->HandleAccelerator(message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return result == -1 ? 1 : exitCode_;
}

bool FrameSnapApp::CreateMainWindow() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = FrameSnapApp::WndProc;
    wc.hInstance = instance_;
    wc.lpszClassName = kFrameSnapMainWindowClassName;
    if (RegisterClassW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    // Hidden top-level window receives session and Explorer-restart broadcasts.
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kFrameSnapMainWindowClassName, kFrameSnapAppName,
        WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance_, this);
    return hwnd_ != nullptr;
}

void FrameSnapApp::CreateTrayIcon() {
    if (hwnd_ == nullptr || shuttingDown_ || mode_ == LaunchMode::Capture) return;
    trayIcon_.cbSize = sizeof(trayIcon_);
    trayIcon_.hWnd = hwnd_;
    trayIcon_.uID = 1;
    trayIcon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    trayIcon_.uCallbackMessage = kTrayMessage;
    if (trayIconHandle_ == nullptr) trayIconHandle_ = util::CreateFrameSnapAppIcon(32);
    trayIcon_.hIcon = trayIconHandle_ != nullptr ? trayIconHandle_ : LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(trayIcon_.szTip, hotkeyRegistered_ ? L"FrameSnap - ready" : L"FrameSnap - open Settings to configure your hotkey");
    Shell_NotifyIconW(NIM_DELETE, &trayIcon_);
    trayVersion4_ = false;
    if (Shell_NotifyIconW(NIM_ADD, &trayIcon_)) {
        trayIcon_.uVersion = NOTIFYICON_VERSION_4;
        trayVersion4_ = Shell_NotifyIconW(NIM_SETVERSION, &trayIcon_) != FALSE;
    }
}

void FrameSnapApp::RemoveTrayIcon() {
    if (trayIcon_.hWnd != nullptr) {
        Shell_NotifyIconW(NIM_DELETE, &trayIcon_);
        trayIcon_.hWnd = nullptr;
    }
}

void FrameSnapApp::ShowTrayMenu(POINT point) {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;
    AppendMenuW(menu, MF_STRING, TrayCapture, L"Capture");
    AppendMenuW(menu, MF_STRING, TraySettings, L"Settings");
    AppendMenuW(menu, MF_STRING, TrayOpenFolder, L"Open save folder");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, TrayExit, L"Quit FrameSnap");
    if (point.x == -1 && point.y == -1) GetCursorPos(&point);
    SetForegroundWindow(hwnd_);
    const UINT command = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
        point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    PostMessageW(hwnd_, WM_NULL, 0, 0); // Dismiss correctly when clicking outside the menu.
    if (command != 0) PostMessageW(hwnd_, WM_COMMAND, command, 0);
}

bool FrameSnapApp::RegisterAppHotkey(const HotkeyBinding& binding) {
    UnregisterAppHotkey();
    const UINT modifiers = binding.modifiers & (MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN);
    // Do not install a global keyboard hook or steal ordinary typing on conflict.
    if (binding.virtualKey == 0 || binding.virtualKey > 255 || binding.virtualKey == VK_F12 ||
        (modifiers == 0 && binding.virtualKey != VK_SNAPSHOT &&
            (binding.virtualKey < VK_F1 || binding.virtualKey > VK_F24))) return false;
    hotkeyRegistered_ = RegisterHotKey(hwnd_, kHotkeyId, modifiers | MOD_NOREPEAT, binding.virtualKey) != FALSE;
    return hotkeyRegistered_;
}

void FrameSnapApp::UnregisterAppHotkey() {
    if (hwnd_ != nullptr && hotkeyRegistered_) UnregisterHotKey(hwnd_, kHotkeyId);
    hotkeyRegistered_ = false;
}

std::wstring FrameSnapApp::BuildHotkeyStatusText() const {
    if (mode_ != LaunchMode::Background) return L"On-demand mode: use the installed Windows shortcut or launch FrameSnap.";
    return hotkeyRegistered_ ? L"Ready: " + util::HotkeyLabel(settings_.hotkey)
        : L"Shortcut unavailable. Choose another combination; Ctrl+Alt+S is the default.";
}

std::wstring FrameSnapApp::BuildPrintScreenStatusText() const {
    return util::IsPrintScreenSnippingEnabled() ? L"Windows currently uses Print Screen for Snipping Tool."
        : L"Windows Print Screen snipping is disabled in Windows settings.";
}

void FrameSnapApp::ShowSettings() {
    if (shuttingDown_) return;
    if (mode_ == LaunchMode::Capture) mode_ = LaunchMode::Settings;
    if (settingsWindow_ == nullptr) settingsWindow_ = std::make_unique<SettingsWindow>(instance_, hwnd_);
    CreateTrayIcon();
    settingsWindow_->Show(settings_, BuildHotkeyStatusText(), BuildPrintScreenStatusText());
}

void FrameSnapApp::ReportError(const wchar_t* message) {
    exitCode_ = 1;
    AppendStartupLog(message);
    if (!shuttingDown_) MessageBoxW(hwnd_, message, kFrameSnapAppName, MB_OK | MB_ICONWARNING);
}

void FrameSnapApp::ExitApplication() {
    if (!shuttingDown_) {
        shuttingDown_ = true;
        UnregisterAppHotkey();
        RemoveTrayIcon();
        frozenFrame_.reset();
        if (overlay_ != nullptr) overlay_->Cancel();
        if (preview_ != nullptr) preview_->Hide();
        if (editor_ != nullptr) editor_->Close(false);
        if (settingsWindow_ != nullptr && IsWindow(settingsWindow_->Handle())) DestroyWindow(settingsWindow_->Handle());
        PlaySoundW(nullptr, nullptr, 0);
    }
    // Keep pumping messages until accepted saves complete, rather than blocking
    // the window thread on disk I/O or an infinite clipboard event wait.
    if (pendingSaves_ == 0) FinishExit();
}

void FrameSnapApp::FinishExit() {
    if (hwnd_ != nullptr && IsWindow(hwnd_)) DestroyWindow(hwnd_);
    else PostQuitMessage(exitCode_);
}

void FrameSnapApp::FinishSession() {
    // Ignore stale close notifications if a newer capture or editor is active.
    if ((overlay_ != nullptr && overlay_->IsActive()) ||
        (preview_ != nullptr && preview_->CurrentImage() != nullptr) ||
        (editor_ != nullptr && IsWindowVisible(editor_->Handle()))) return;
    frozenFrame_.reset();
    overlay_.reset();
    editor_.reset();
    if (mode_ == LaunchMode::Capture) ExitApplication();
}

void FrameSnapApp::BeginCapture() {
    if (shuttingDown_ || (overlay_ != nullptr && overlay_->IsActive())) return;
    if (overlay_ == nullptr) overlay_ = std::make_unique<OverlayWindow>(instance_, hwnd_);
    if (preview_ != nullptr) preview_->Hide();
    if (editor_ != nullptr) { editor_->Close(false); editor_.reset(); }
    if (settingsWindow_ != nullptr) settingsWindow_->Hide();
    const auto hotkeyStart = std::chrono::steady_clock::now();
    DwmFlush();
    if (settings_.soundEnabled) {
        static const auto sound = CreateCaptureSoundWav();
        PlaySoundW(reinterpret_cast<LPCWSTR>(sound.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    }
    frozenFrame_ = util::CaptureScreenSnapshotGdi(util::VirtualScreenBounds());
    if (frozenFrame_ == nullptr) {
        CaptureEngine fallback; // D3D resources exist only while attempting a capture.
        if (fallback.Initialize()) frozenFrame_ = fallback.Capture(util::VirtualScreenBounds());
    }
    if (frozenFrame_ == nullptr || !overlay_->BeginSession(settings_, hotkeyStart, frozenFrame_)) {
        overlay_->Cancel();
        ReportError(L"The desktop could not be captured. Unlock your desktop and try again.");
        FinishSession();
        return;
    }
    lastOverlayLatency_ = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - hotkeyStart);
}

void FrameSnapApp::HandleCaptureReady(std::unique_ptr<CaptureRequest> request) {
    if (shuttingDown_ || request == nullptr) return;
    auto result = util::CropImage(frozenFrame_, request->selection);
    frozenFrame_.reset();
    overlay_.reset();
    if (result == nullptr) { FinishSession(); return; }
    CaptureMetrics metrics{};
    metrics.hotkeyToOverlay = lastOverlayLatency_;
    const bool copied = ClipboardPublisher::Publish(hwnd_, *result);
    metrics.commitToClipboard = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - request->commitTime);
    if (!copied) ReportError(L"The image could not be copied. Another app may be holding the clipboard. Open the preview to copy or save it.");
    if (settings_.soundEnabled && copied) {
        static const auto sound = CreateCaptureCompleteSoundWav();
        PlaySoundW(reinterpret_cast<LPCWSTR>(sound.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    }
    if (settings_.autoSaveEnabled) {
        const auto savePath = (std::filesystem::path(settings_.saveFolder) / util::TimestampedFileName(*result)).wstring();
        const auto saveStart = std::chrono::steady_clock::now();
        metrics.saveDropped = !saveQueue_.Enqueue({result, savePath}, hwnd_);
        if (!metrics.saveDropped) ++pendingSaves_;
        else ReportError(L"The save queue is full. This capture was not saved to disk; use the preview to save it.");
        metrics.commitToSaveEnqueue = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - saveStart);
    }
    const auto previewStart = std::chrono::steady_clock::now();
    if (preview_ == nullptr) preview_ = std::make_unique<PreviewWindow>(instance_, hwnd_);
    if (!preview_->Show(result, settings_.previewTimeoutMs)) FinishSession();
    metrics.commitToPreview = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - previewStart);
    util::WriteMetricsLog(metrics, *result);
}

void FrameSnapApp::ApplySettings(const AppSettings& settings) {
    AppSettings merged = settings;
    merged.penColor = settings_.penColor;
    merged.highlighterColor = settings_.highlighterColor;
    merged.penWidth = settings_.penWidth;
    merged.highlighterWidth = settings_.highlighterWidth;
    if (mode_ == LaunchMode::Background && !RegisterAppHotkey(merged.hotkey)) {
        RegisterAppHotkey(settings_.hotkey);
        ReportError(L"That shortcut is already in use or unsupported. The previous shortcut has been kept.");
        return;
    }
    if (merged.runAtStartupEnabled != settings_.runAtStartupEnabled && !util::SetRunAtStartup(merged.runAtStartupEnabled)) {
        if (mode_ == LaunchMode::Background) RegisterAppHotkey(settings_.hotkey);
        ReportError(L"The sign-in setting could not be updated. Your previous settings have been kept.");
        return;
    }
    if (!settingsStore_.Save(merged)) {
        if (merged.runAtStartupEnabled != settings_.runAtStartupEnabled) util::SetRunAtStartup(settings_.runAtStartupEnabled);
        if (mode_ == LaunchMode::Background) RegisterAppHotkey(settings_.hotkey);
        ReportError(L"Settings could not be saved. Check that your local app data folder is writable.");
        return;
    }
    settings_ = merged;
    CreateTrayIcon();
    if (settingsWindow_ != nullptr) settingsWindow_->UpdateStatus(BuildHotkeyStatusText(), BuildPrintScreenStatusText());
}

LRESULT CALLBACK FrameSnapApp::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<FrameSnapApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<FrameSnapApp*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    return self != nullptr ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT FrameSnapApp::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (taskbarCreatedMessage_ != 0 && message == taskbarCreatedMessage_) { CreateTrayIcon(); return 0; }
    switch (message) {
    case WM_QUERYENDSESSION: return TRUE; // Never do disk I/O in the shutdown query.
    case WM_ENDSESSION:
        if (wParam != 0) ExitApplication();
        return 0;
    case WM_CLOSE:
    case WM_APP_EXIT_REQUESTED: ExitApplication(); return 0;
    case WM_HOTKEY:
        if (wParam == kHotkeyId && hotkeyRegistered_) BeginCapture();
        return 0;
    case WM_APP_BEGIN_CAPTURE: BeginCapture(); return 0;
    case WM_APP_START_BACKGROUND:
        if (!shuttingDown_ && mode_ != LaunchMode::Background) {
            mode_ = LaunchMode::Background;
            RegisterAppHotkey(settings_.hotkey);
            CreateTrayIcon();
        }
        return 0;
    case WM_APP_SHOW_SETTINGS: ShowSettings(); return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case TrayCapture: BeginCapture(); return 0;
        case TraySettings: ShowSettings(); return 0;
        case TrayOpenFolder: ShellExecuteW(hwnd_, L"open", settings_.saveFolder.c_str(), nullptr, nullptr, SW_SHOWNORMAL); return 0;
        case TrayExit: ExitApplication(); return 0;
        }
        break;
    case WM_DISPLAYCHANGE:
        if (overlay_ != nullptr && overlay_->IsActive()) { overlay_->Cancel(); FinishSession(); }
        return 0;
    case WM_APP_CAPTURE_READY:
        HandleCaptureReady(std::unique_ptr<CaptureRequest>(reinterpret_cast<CaptureRequest*>(lParam)));
        return 0;
    case WM_APP_CAPTURE_CANCELLED:
    case WM_APP_PREVIEW_CLOSED:
    case WM_APP_EDITOR_CLOSED:
        FinishSession();
        return 0;
    case WM_APP_PREVIEW_CLICKED:
        if (!shuttingDown_ && preview_ != nullptr) {
            auto image = preview_->CurrentImage();
            preview_->Hide();
            if (image != nullptr) {
                if (editor_ == nullptr) editor_ = std::make_unique<EditorWindow>(instance_, hwnd_);
                editor_->Show(image, settings_);
                if (editor_->Handle() == nullptr) FinishSession();
            }
        }
        return 0;
    case WM_APP_SAVE_COMPLETED:
        if (pendingSaves_ != 0) --pendingSaves_;
        if (wParam == 0) ReportError(L"The PNG could not be saved. Check the destination folder and available disk space.");
        if (shuttingDown_ && pendingSaves_ == 0) FinishExit();
        return 0;
    case WM_APP_SETTINGS_APPLIED: {
        std::unique_ptr<AppSettings> settings(reinterpret_cast<AppSettings*>(lParam));
        if (settings != nullptr && !shuttingDown_) ApplySettings(*settings);
        return 0;
    }
    case kTrayMessage: {
        const UINT event = trayVersion4_ ? LOWORD(lParam) : static_cast<UINT>(lParam);
        if (event == WM_CONTEXTMENU || (!trayVersion4_ && event == WM_RBUTTONUP)) {
            POINT point{-1, -1};
            if (trayVersion4_) point = {GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)};
            ShowTrayMenu(point);
        } else if (event == NIN_SELECT || event == NIN_KEYSELECT || (!trayVersion4_ && event == WM_LBUTTONUP)) {
            BeginCapture();
        }
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(exitCode_); return 0;
    case WM_NCDESTROY: {
        HWND destroyed = hwnd_;
        SetWindowLongPtrW(destroyed, GWLP_USERDATA, 0);
        hwnd_ = nullptr;
        return DefWindowProcW(destroyed, message, wParam, lParam);
    }
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}
