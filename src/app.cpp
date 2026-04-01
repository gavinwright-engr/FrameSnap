#include "app.h"

#include "util.h"

#include <mmsystem.h>

namespace {

constexpr wchar_t kMainClassName[] = L"ScreenshotterMainWindow";
constexpr wchar_t kAppName[] = L"OneShot";
constexpr UINT kTrayMessage = WM_APP + 20;
constexpr UINT kHotkeyId = 1;

enum TrayCommand {
    TrayCapture = 4001,
    TraySettings,
    TrayOpenFolder,
    TrayExit,
};

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
    constexpr int kSampleRate = 22050;
    constexpr double kDurationSeconds = 0.095;
    constexpr double kSplitSeconds = 0.045;
    constexpr double kAttackSeconds = 0.004;
    constexpr double kReleaseSeconds = 0.026;
    constexpr double kPi = 3.14159265358979323846;
    const int sampleCount = static_cast<int>(kSampleRate * kDurationSeconds);

    std::vector<std::int16_t> samples;
    samples.reserve(static_cast<size_t>(sampleCount));
    for (int i = 0; i < sampleCount; ++i) {
        const double t = static_cast<double>(i) / kSampleRate;
        double envelope = 1.0;
        if (t < kAttackSeconds) {
            envelope = t / kAttackSeconds;
        } else if (t > (kDurationSeconds - kReleaseSeconds)) {
            envelope = (kDurationSeconds - t) / kReleaseSeconds;
        }
        envelope = std::clamp(envelope, 0.0, 1.0);

        double sample = 0.0;
        if (t < kSplitSeconds) {
            sample += 0.75 * std::sin(2.0 * kPi * 1046.50 * t);
            sample += 0.20 * std::sin(2.0 * kPi * 1567.98 * t);
        } else {
            const double shifted = t - kSplitSeconds;
            sample += 0.62 * std::sin(2.0 * kPi * 1318.51 * shifted);
            sample += 0.16 * std::sin(2.0 * kPi * 1975.53 * shifted);
        }

        const auto pcm = static_cast<std::int16_t>(std::clamp(sample * envelope * 0.28, -1.0, 1.0) * 32767.0);
        samples.push_back(pcm);
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
    constexpr int kSampleRate = 22050;
    constexpr double kDurationSeconds = 0.084;
    constexpr double kSplitSeconds = 0.038;
    constexpr double kAttackSeconds = 0.003;
    constexpr double kReleaseSeconds = 0.024;
    constexpr double kPi = 3.14159265358979323846;
    const int sampleCount = static_cast<int>(kSampleRate * kDurationSeconds);

    std::vector<std::int16_t> samples;
    samples.reserve(static_cast<size_t>(sampleCount));
    for (int i = 0; i < sampleCount; ++i) {
        const double t = static_cast<double>(i) / kSampleRate;
        double envelope = 1.0;
        if (t < kAttackSeconds) {
            envelope = t / kAttackSeconds;
        } else if (t > (kDurationSeconds - kReleaseSeconds)) {
            envelope = (kDurationSeconds - t) / kReleaseSeconds;
        }
        envelope = std::clamp(envelope, 0.0, 1.0);

        double sample = 0.0;
        if (t < kSplitSeconds) {
            sample += 0.74 * std::sin(2.0 * kPi * 783.99 * t);
            sample += 0.18 * std::sin(2.0 * kPi * 1174.66 * t);
        } else {
            const double shifted = t - kSplitSeconds;
            sample += 0.60 * std::sin(2.0 * kPi * 987.77 * shifted);
            sample += 0.14 * std::sin(2.0 * kPi * 1479.98 * shifted);
        }

        const auto pcm = static_cast<std::int16_t>(std::clamp(sample * envelope * 0.22, -1.0, 1.0) * 32767.0);
        samples.push_back(pcm);
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

HICON CreateOneShotTrayIcon() {
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

ScreenshotterApp::ScreenshotterApp(HINSTANCE instance)
    : instance_(instance) {}

ScreenshotterApp::~ScreenshotterApp() {
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

bool ScreenshotterApp::Initialize() {
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
    util::SetRunAtStartup(true);
    AppendStartupLog(L"Initialize: startup registration attempted");

    if (!CreateMainWindow()) {
        AppendStartupLog(L"Initialize: main window creation failed");
        return false;
    }
    AppendStartupLog(L"Initialize: main window created");

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
        AppendStartupLog(L"Initialize: hotkey registered");
    }
    CreateTrayIcon();
    AppendStartupLog(L"Initialize: tray icon created");
    if (!hotkeyRegistered_) {
        settingsWindow_->Show(settings_);
        const std::wstring message = L"OneShot couldn't register " + util::HotkeyLabel(settings_.hotkey) +
                                     L" because Windows or another app is already using it.\n\nRecord a different shortcut in Settings.";
        MessageBoxW(hwnd_, message.c_str(), kAppName, MB_OK | MB_ICONINFORMATION);
    }
    return true;
}

int ScreenshotterApp::Run() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

bool ScreenshotterApp::CreateMainWindow() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = ScreenshotterApp::WndProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kMainClassName;
    if (RegisterClassW(&wc) == 0) {
        const DWORD error = GetLastError();
        AppendStartupLog(L"CreateMainWindow: RegisterClassW failed " + std::to_wstring(error));
        return false;
    }

    hwnd_ = CreateWindowExW(
        0,
        kMainClassName,
        kAppName,
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

void ScreenshotterApp::CreateTrayIcon() {
    trayIcon_.cbSize = sizeof(trayIcon_);
    trayIcon_.hWnd = hwnd_;
    trayIcon_.uID = 1;
    trayIcon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    trayIcon_.uCallbackMessage = kTrayMessage;
    if (trayIconHandle_ == nullptr) {
        trayIconHandle_ = CreateOneShotTrayIcon();
    }
    trayIcon_.hIcon = trayIconHandle_ != nullptr ? trayIconHandle_ : LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(trayIcon_.szTip, kAppName);
    Shell_NotifyIconW(NIM_ADD, &trayIcon_);
}

void ScreenshotterApp::RemoveTrayIcon() {
    if (trayIcon_.hWnd != nullptr) {
        Shell_NotifyIconW(NIM_DELETE, &trayIcon_);
        trayIcon_.hWnd = nullptr;
    }
}

void ScreenshotterApp::ShowTrayMenu() {
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

bool ScreenshotterApp::RegisterAppHotkey(const HotkeyBinding& binding) {
    UnregisterAppHotkey();
    return RegisterHotKey(hwnd_, kHotkeyId, binding.modifiers | MOD_NOREPEAT, binding.virtualKey) == TRUE;
}

void ScreenshotterApp::UnregisterAppHotkey() {
    UnregisterHotKey(hwnd_, kHotkeyId);
}

void ScreenshotterApp::BeginCapture() {
    if (overlay_ == nullptr || overlay_->IsActive()) {
        return;
    }
    const auto hotkeyStart = std::chrono::steady_clock::now();
    if (settings_.soundEnabled) {
        static const auto captureSound = CreateCaptureSoundWav();
        if (!captureSound.empty()) {
            PlaySoundW(reinterpret_cast<LPCWSTR>(captureSound.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT | SND_NOSTOP);
        }
    }
    frozenFrame_ = captureEngine_.Capture(util::VirtualScreenBounds());
    if (frozenFrame_ == nullptr) {
        captureEngine_.RefreshOutputs();
        frozenFrame_ = captureEngine_.Capture(util::VirtualScreenBounds());
    }
    overlay_->BeginSession(settings_, hotkeyStart, frozenFrame_);
    lastOverlayLatency_ = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - hotkeyStart);
}

void ScreenshotterApp::HandleCaptureReady(std::unique_ptr<CaptureRequest> request) {
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

void ScreenshotterApp::ApplySettings(const AppSettings& settings) {
    AppSettings merged = settings;
    merged.penColor = settings_.penColor;
    merged.highlighterColor = settings_.highlighterColor;
    merged.penWidth = settings_.penWidth;
    merged.highlighterWidth = settings_.highlighterWidth;
    if (!RegisterAppHotkey(merged.hotkey)) {
        hotkeyRegistered_ = RegisterAppHotkey(settings_.hotkey);
        const std::wstring message = L"OneShot couldn't register " + util::HotkeyLabel(merged.hotkey) +
                                     L". It is likely already in use or unsupported.";
        MessageBoxW(hwnd_, message.c_str(), kAppName, MB_OK | MB_ICONWARNING);
        return;
    }
    hotkeyRegistered_ = true;
    settings_ = merged;
    settingsStore_.Save(settings_);
}

LRESULT CALLBACK ScreenshotterApp::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<ScreenshotterApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<ScreenshotterApp*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }
    return self != nullptr ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT ScreenshotterApp::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_HOTKEY:
        BeginCapture();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case TrayCapture:
            BeginCapture();
            return 0;
        case TraySettings:
            settingsWindow_->Show(settings_);
            return 0;
        case TrayOpenFolder:
            ShellExecuteW(hwnd_, L"open", settings_.saveFolder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        case TrayExit:
            DestroyWindow(hwnd_);
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
    case kTrayMessage:
        if (lParam == WM_CONTEXTMENU || lParam == WM_RBUTTONUP) {
            ShowTrayMenu();
        } else if (lParam == WM_LBUTTONDBLCLK) {
            BeginCapture();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}
