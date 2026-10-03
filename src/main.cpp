#include "app.h"

namespace {

UINT LaunchMessage(LaunchMode mode) {
    switch (mode) {
    case LaunchMode::Quit: return WM_APP_EXIT_REQUESTED;
    case LaunchMode::Settings: return WM_APP_SHOW_SETTINGS;
    case LaunchMode::Background: return WM_APP_START_BACKGROUND;
    default: return WM_APP_BEGIN_CAPTURE;
    }
}

int RunInstance(HINSTANCE instance, LaunchMode mode) {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, kFrameSnapSingleInstanceMutexName);
    if (mutex == nullptr) return 1;
    bool acquired = false;
    int result = 1;
    // Bounded startup/shutdown race handling. No always-running IPC thread.
    for (int attempt = 0; attempt < 100; ++attempt) {
        const DWORD state = WaitForSingleObject(mutex, 0);
        if (state == WAIT_OBJECT_0 || state == WAIT_ABANDONED) {
            acquired = true;
            break;
        }
        if (state != WAIT_TIMEOUT) break;
        if (HWND existing = FindWindowW(kFrameSnapMainWindowClassName, nullptr)) {
            DWORD processId = 0;
            GetWindowThreadProcessId(existing, &processId);
            HANDLE process = mode == LaunchMode::Quit ? OpenProcess(SYNCHRONIZE, FALSE, processId) : nullptr;
            if (mode == LaunchMode::Capture || mode == LaunchMode::Settings) AllowSetForegroundWindow(processId);
            if (PostMessageW(existing, LaunchMessage(mode), 0, 0)) {
                result = 0;
                if (mode == LaunchMode::Quit) {
                    const DWORD waited = process == nullptr ? WAIT_FAILED : WaitForSingleObject(process, 10000);
                    if (waited != WAIT_OBJECT_0) {
                        const auto diagnostic = L"FrameSnap: quit wait failed, status=" + std::to_wstring(waited) +
                            L" error=" + std::to_wstring(GetLastError()) + L"\n";
                        OutputDebugStringW(diagnostic.c_str());
                        result = 1; // Never force-kill a process with pending saves.
                    }
                }
                if (process != nullptr) CloseHandle(process);
                break;
            }
            if (process != nullptr) CloseHandle(process);
        }
        Sleep(20);
    }
    if (acquired) {
        if (mode == LaunchMode::Quit) {
            result = 0; // Idempotent: quitting an app that is not running succeeds.
        } else {
            const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            if (SUCCEEDED(initialized)) {
                try {
                    FrameSnapApp app(instance, mode);
                    result = app.Initialize() ? app.Run() : 1;
                } catch (const std::exception&) {
                    if (mode != LaunchMode::Background) {
                        MessageBoxW(nullptr, L"FrameSnap could not complete this operation. Try a smaller capture or free some memory.",
                            kFrameSnapAppName, MB_OK | MB_ICONERROR);
                    }
                    result = 1;
                }
                CoUninitialize();
            }
        }
        ReleaseMutex(mutex);
    }
    CloseHandle(mutex);
    return result;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // Remove the working directory and PATH from implicit DLL searches.
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) return 1;
    std::vector<std::wstring_view> arguments;
    for (int i = 1; i < argc; ++i) arguments.emplace_back(argv[i]);
    const auto mode = ParseLaunchMode(arguments);
    LocalFree(argv);
    if (!mode.has_value() || *mode == LaunchMode::Help) {
        MessageBoxW(nullptr,
            L"FrameSnap\n\nNo arguments / --capture: capture once, then exit.\n"
            L"--background: stay in the tray and listen for the configured hotkey.\n"
            L"--settings: open settings. Closing settings quits FrameSnap.\n"
            L"--quit: finish pending saves and quit the running instance.\n\n"
            L"The installer can create a Windows shortcut hotkey that launches a capture without a background process.",
            kFrameSnapAppName, MB_OK | (mode.has_value() ? MB_ICONINFORMATION : MB_ICONERROR));
        return mode.has_value() ? 0 : 2;
    }
    return RunInstance(instance, *mode);
}
