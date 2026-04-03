#include "app.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
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

    FrameSnapApp app(instance, launchBackground);
    const bool initialized = app.Initialize();
    const int result = initialized ? app.Run() : -1;
    CoUninitialize();
    return result;
}
