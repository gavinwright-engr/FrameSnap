#include "app.h"

namespace {

bool SignalExistingFrameSnap() {
    for (int attempt = 0; attempt < 20; ++attempt) {
        HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, kFrameSnapShowSettingsEventName);
        if (event != nullptr) {
            SetEvent(event);
            CloseHandle(event);
            return true;
        }
        if (HWND hwnd = FindWindowW(kFrameSnapMainWindowClassName, kFrameSnapAppName)) {
            PostMessageW(hwnd, WM_APP_SHOW_SETTINGS, 0, 0);
            return true;
        }
        if (HWND hwnd = FindWindowW(kFrameSnapSettingsWindowClassName, kFrameSnapAppName)) {
            PostMessageW(hwnd, WM_APP_SHOW_SETTINGS, 0, 0);
            return true;
        }
        Sleep(50);
    }
    return false;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT coInitializeResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool coInitialized = SUCCEEDED(coInitializeResult);
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool launchBackground = false;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] != nullptr && _wcsicmp(argv[i], L"--background") == 0) {
            launchBackground = true;
            break;
        }
    }
    if (argv != nullptr) {
        LocalFree(argv);
    }

    HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, kFrameSnapSingleInstanceMutexName);
    const DWORD mutexError = instanceMutex != nullptr ? GetLastError() : ERROR_SUCCESS;
    const bool ownsMutex = instanceMutex != nullptr && mutexError != ERROR_ALREADY_EXISTS;
    if (mutexError == ERROR_ALREADY_EXISTS) {
        if (!launchBackground) {
            SignalExistingFrameSnap();
        }
        if (instanceMutex != nullptr) {
            CloseHandle(instanceMutex);
        }
        if (coInitialized) {
            CoUninitialize();
        }
        return 0;
    }

    int result = -1;
    {
        FrameSnapApp app(instance, launchBackground);
        const bool initialized = app.Initialize();
        result = initialized ? app.Run() : -1;
    }
    if (ownsMutex) {
        ReleaseMutex(instanceMutex);
    }
    if (instanceMutex != nullptr) {
        CloseHandle(instanceMutex);
    }
    if (coInitialized) {
        CoUninitialize();
    }
    return result;
}
