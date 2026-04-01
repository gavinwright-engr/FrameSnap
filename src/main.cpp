#include "app.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ScreenshotterApp app(instance);
    const bool initialized = app.Initialize();
    const int result = initialized ? app.Run() : -1;
    CoUninitialize();
    return result;
}
