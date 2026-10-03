#include "clipboard_publisher.h"
#include "image_io.h"
#include "save_queue.h"
#include "settings_store.h"
#include "util.h"
#include <iostream>
#include <psapi.h>
#include <set>
#include <stdexcept>

namespace {
int checks = 0;
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(std::string(#condition) + " (Win32=" + std::to_string(GetLastError()) + ")"); ++checks; } while (false)

void Pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

template <class Predicate> bool Until(Predicate predicate, DWORD timeout = 5000) {
    const auto start = GetTickCount64();
    while (GetTickCount64() - start < timeout) {
        Pump();
        if (predicate()) return true;
        Sleep(10);
    }
    return false;
}

struct TestWindow {
    HWND handle = CreateWindowExW(0, L"STATIC", L"FrameSnap test surface", WS_POPUP | SS_WHITERECT,
        200, 200, 240, 180, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ~TestWindow() { if (IsWindow(handle)) DestroyWindow(handle); }
};

struct TempDirectory {
    std::filesystem::path path;
    TempDirectory() {
        GUID id{};
        wchar_t text[40]{};
        CHECK(SUCCEEDED(CoCreateGuid(&id)));
        CHECK(StringFromGUID2(id, text, 40) != 0);
        path = std::filesystem::temp_directory_path() / (std::wstring(L"FrameSnap-tests-") + text);
        std::filesystem::create_directory(path);
    }
    ~TempDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
};

ImageData Sample() {
    ImageData image;
    image.width = 2;
    image.height = 2;
    image.sourceRect = {-2, -1, 0, 1};
    image.pixels = {0, 0, 255, 255, 0, 255, 0, 255, 255, 0, 0, 255, 255, 255, 255, 255};
    return image;
}

void Components() {
    TestWindow window;
    CHECK(window.handle != nullptr);
    TempDirectory directory;
    auto image = Sample();
    CHECK(ClipboardPublisher::Publish(window.handle, image));
    CHECK(IsClipboardFormatAvailable(CF_DIBV5));
    CHECK(IsClipboardFormatAvailable(RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing")));
    CHECK(OpenClipboard(window.handle));
    auto handle = GetClipboardData(CF_DIBV5);
    CHECK(handle != nullptr);
    auto* header = static_cast<BITMAPV5HEADER*>(GlobalLock(handle));
    CHECK(header != nullptr && header->bV5Width == 2 && header->bV5Height == -2);
    CHECK(memcmp(header + 1, image.pixels.data(), image.pixels.size()) == 0);
    GlobalUnlock(handle);
    CloseClipboard();
    const auto sequence = GetClipboardSequenceNumber();
    auto invalid = image;
    invalid.pixels.pop_back();
    CHECK(!ClipboardPublisher::Publish(window.handle, invalid));
    CHECK(GetClipboardSequenceNumber() == sequence);
    CHECK(!ClipboardPublisher::Publish(nullptr, image));

    ImageIo io;
    const auto png = io.EncodePng(image);
    const std::array<std::uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    CHECK(png.size() > signature.size() && std::equal(signature.begin(), signature.end(), png.begin()));
    ComPtr<IWICImagingFactory> factory;
    CHECK(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()))));
    ComPtr<IWICStream> stream;
    CHECK(SUCCEEDED(factory->CreateStream(stream.GetAddressOf())));
    CHECK(SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE*>(png.data()), static_cast<DWORD>(png.size()))));
    ComPtr<IWICBitmapDecoder> decoder;
    CHECK(SUCCEEDED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf())));
    ComPtr<IWICBitmapFrameDecode> frame;
    CHECK(SUCCEEDED(decoder->GetFrame(0, frame.GetAddressOf())));
    ComPtr<IWICFormatConverter> converted;
    CHECK(SUCCEEDED(factory->CreateFormatConverter(converted.GetAddressOf())));
    CHECK(SUCCEEDED(converted->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)));
    std::vector<BYTE> decoded(image.pixels.size());
    CHECK(SUCCEEDED(converted->CopyPixels(nullptr, image.width * 4, static_cast<UINT>(decoded.size()), decoded.data())));
    CHECK(decoded == image.pixels);
    const auto filename = (directory.path / L"capture.png").wstring();
    CHECK(io.SavePng(image, filename));
    CHECK(!io.SavePng(image, filename)); // Automatic saves never overwrite.
    CHECK(io.SavePng(image, filename, true)); // Explicit editor save can replace its own file.
    CHECK(!io.SavePng(invalid, (directory.path / L"invalid.png").wstring()));
    CHECK(!std::filesystem::exists(directory.path / L"invalid.png"));
    CHECK(std::distance(std::filesystem::directory_iterator(directory.path), {}) == 1);

    auto cropped = util::CropImage(std::make_shared<ImageData>(image), RECT{-1, -1, 4, 0});
    CHECK(cropped != nullptr && cropped->width == 1 && cropped->height == 1);
    CHECK(cropped->pixels == std::vector<std::uint8_t>({0, 255, 0, 255}));
    CHECK(!util::CropImage(std::make_shared<ImageData>(invalid), image.sourceRect));
    CHECK(!util::CropImage(std::make_shared<ImageData>(image), RECT{20, 20, 21, 21}));
    std::set<std::wstring> names;
    for (int i = 0; i < 500; ++i) names.insert(util::TimestampedFileName(image));
    CHECK(names.size() == 500);

    SaveQueue queue;
    for (int i = 0; i < 3; ++i) {
        CHECK(queue.Enqueue({std::make_shared<ImageData>(image), (directory.path / (std::to_wstring(i) + L".png")).wstring()}, window.handle));
        // Test that an idle worker can finish and restart for a later save.
        CHECK(Until([&] { return std::filesystem::exists(directory.path / (std::to_wstring(i) + L".png")); }));
    }
    CHECK(queue.Enqueue({std::make_shared<ImageData>(image), (directory.path / L"drained.png").wstring()}, window.handle));
    queue.Stop();
    CHECK(std::filesystem::exists(directory.path / L"drained.png"));
    CHECK(!queue.Enqueue({std::make_shared<ImageData>(image), filename}, window.handle));

    ShowWindow(window.handle, SW_SHOWNORMAL);
    UpdateWindow(window.handle);
    DwmFlush();
    auto snapshot = util::CaptureScreenSnapshotGdi(RECT{220, 220, 240, 240});
    CHECK(snapshot && snapshot->width == 20 && snapshot->height == 20);
    for (std::size_t i = 3; i < snapshot->pixels.size(); i += 4) CHECK(snapshot->pixels[i] == 255);
    std::cout << "clipboard ownership/privacy, lossless PNG round-trip, atomic saves, crop bounds, unique names, queue drain/restart, opaque GDI capture passed\n";
}

struct Process {
    PROCESS_INFORMATION info{};
    std::wstring arguments_;
    Process(const std::wstring& executable, const wchar_t* arguments) {
        arguments_ = arguments;
        std::wstring command = L"\"" + executable + L"\" " + arguments;
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        CHECK(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &info));
        CloseHandle(info.hThread);
    }
    ~Process() {
        if (info.hProcess != nullptr) {
            if (WaitForSingleObject(info.hProcess, 0) == WAIT_TIMEOUT) {
                if (HWND hwnd = Main()) PostMessageW(hwnd, WM_CLOSE, 0, 0);
                if (WaitForSingleObject(info.hProcess, 2000) == WAIT_TIMEOUT) TerminateProcess(info.hProcess, 99); // Only this test's child.
            }
            CloseHandle(info.hProcess);
        }
    }
    HWND Window(const wchar_t* className, bool visible = false) const {
        HWND hwnd = nullptr;
        while ((hwnd = FindWindowExW(nullptr, hwnd, className, nullptr)) != nullptr) {
            DWORD processId = 0;
            GetWindowThreadProcessId(hwnd, &processId);
            if (processId == info.dwProcessId && (!visible || IsWindowVisible(hwnd))) return hwnd;
        }
        return nullptr;
    }
    HWND Main() const { return Window(kFrameSnapMainWindowClassName); }
    HWND Await(const wchar_t* className, bool visible = false) const {
        HWND hwnd = nullptr;
        CHECK(Until([&] { hwnd = Window(className, visible); return hwnd != nullptr; }));
        return hwnd;
    }
    void Exited() const {
        CHECK(Until([&] { return WaitForSingleObject(info.hProcess, 0) == WAIT_OBJECT_0; }, 10000));
        DWORD code = 99;
        CHECK(GetExitCodeProcess(info.hProcess, &code));
        if (code != 0) std::wcerr << L"Process " << info.dwProcessId << L" (" << arguments_ << L") exit=" << code << L"\n";
        CHECK(code == 0);
    }
};

void SelectRegion(const Process& app) {
    const HWND overlay = app.Await(L"FrameSnapOverlayWindow", true);
    // Coordinates in the virtual-screen overlay, including negative-origin desktops.
    const int x = 220 - GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int y = 220 - GetSystemMetrics(SM_YVIRTUALSCREEN);
    SendMessageW(overlay, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
    SendMessageW(overlay, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x + 100, y + 70));
    SendMessageW(overlay, WM_LBUTTONUP, 0, MAKELPARAM(x + 100, y + 70));
}

void Lifecycle(const std::wstring& executable) {
    // Run on an isolated test desktop/profile; this suite intentionally uses the clipboard.
    CHECK(FindWindowW(kFrameSnapMainWindowClassName, nullptr) == nullptr);
    TestWindow surface;
    CHECK(surface.handle != nullptr);
    ShowWindow(surface.handle, SW_SHOWNORMAL);
    UpdateWindow(surface.handle);
    {
        Process absent(executable, L"--quit");
        absent.Exited();
    }
    {
        CHECK(RegisterHotKey(surface.handle, 91, MOD_CONTROL | MOD_ALT, 'S'));
        Process background(executable, L"--background");
        const HWND main = background.Await(kFrameSnapMainWindowClassName);
        Sleep(200);
        CHECK(!IsWindowVisible(main));
        wchar_t title[64]{};
        CHECK(GetWindowTextW(main, title, 64) > 0);
        CHECK(std::wstring(title) == kFrameSnapAppName);
        CHECK(background.Window(kFrameSnapSettingsWindowClassName, true) == nullptr);
        CHECK(background.Window(L"#32770", true) == nullptr); // No sign-in conflict popup.
        Process quit(executable, L"--quit");
        quit.Exited();
        background.Exited();
        UnregisterHotKey(surface.handle, 91);
    }
    std::cout << "silent startup with a conflicting hotkey and explicit quit passed\n";
    {
        Process settings(executable, L"--settings");
        const HWND window = settings.Await(kFrameSnapSettingsWindowClassName, true);
        const HWND checkbox = FindWindowExW(window, nullptr, L"BUTTON", L"Auto-save every capture");
        CHECK(checkbox != nullptr);
        const LRESULT before = SendMessageW(checkbox, BM_GETCHECK, 0, 0);
        SendMessageW(checkbox, BM_CLICK, 0, 0);
        CHECK(SendMessageW(checkbox, BM_GETCHECK, 0, 0) != before);
        PostMessageW(window, WM_CLOSE, 0, 0);
        settings.Exited();
    }
    {
        Process capture(executable, L"--capture");
        const HWND overlay = capture.Await(L"FrameSnapOverlayWindow", true);
        PostMessageW(overlay, WM_KEYDOWN, VK_ESCAPE, 0);
        capture.Exited();
    }
    std::cout << "settings close and capture cancellation exit passed\n";
    {
        Process capture(executable, L"");
        SelectRegion(capture);
        capture.Await(L"FrameSnapPreviewWindow", true);
        capture.Exited(); // Preview timeout closes the one-shot process.
        CHECK(IsClipboardFormatAvailable(CF_DIBV5));
        CHECK(OpenClipboard(surface.handle));
        auto data = GetClipboardData(CF_DIBV5);
        CHECK(data != nullptr);
        auto* header = static_cast<BITMAPV5HEADER*>(GlobalLock(data));
        CHECK(header && header->bV5Width == 100 && header->bV5Height == -70);
        const auto* pixels = reinterpret_cast<const BYTE*>(header + 1);
        CHECK(pixels[3] == 255);
        GlobalUnlock(data);
        CloseClipboard();
    }
    std::cout << "one-shot capture and clipboard persistence after process exit passed\n";
    {
        Process capture(executable, L"--capture");
        SelectRegion(capture);
        const HWND preview = capture.Await(L"FrameSnapPreviewWindow", true);
        PostMessageW(preview, WM_LBUTTONUP, 0, 0);
        const HWND editor = capture.Await(L"FrameSnapEditorWindow", true);
        CHECK(WaitForSingleObject(capture.info.hProcess, 0) == WAIT_TIMEOUT);
        PostMessageW(editor, WM_CLOSE, 0, 0);
        capture.Exited();
    }
    std::cout << "editor lifetime and close-to-exit passed\n";
    {
        Process background(executable, L"--background");
        const HWND main = background.Await(kFrameSnapMainWindowClassName);
        // Reproduce NOTIFYICON_VERSION_4: icon ID in the high word used to break this handler.
        PostMessageW(main, WM_APP + 20, MAKELPARAM(100, 100), MAKELPARAM(NIN_KEYSELECT, 1));
        const HWND overlay = background.Await(L"FrameSnapOverlayWindow", true);
        PostMessageW(overlay, WM_KEYDOWN, VK_ESCAPE, 0);
        CHECK(Until([&] { return !IsWindowVisible(overlay); }));
        CHECK(WaitForSingleObject(background.info.hProcess, 0) == WAIT_TIMEOUT);
        DWORD_PTR accepted = 0;
        CHECK(SendMessageTimeoutW(main, WM_QUERYENDSESSION, 0, 0, SMTO_ABORTIFHUNG, 500, &accepted));
        CHECK(accepted == TRUE);
        PostMessageW(main, WM_ENDSESSION, TRUE, 0);
        background.Exited();
    }
    std::cout << "packed tray activation, resident cancellation, and Windows shutdown passed\n";
    {
        Process background(executable, L"--background");
        const HWND main = background.Await(kFrameSnapMainWindowClassName);
        Process forward(executable, L"--capture");
        forward.Exited();
        const HWND overlay = background.Await(L"FrameSnapOverlayWindow", true);
        PostMessageW(overlay, WM_KEYDOWN, VK_ESCAPE, 0);
        CHECK(Until([&] { return !background.Window(L"FrameSnapOverlayWindow", true); }));
        FILETIME created{}, exited{}, kernelBefore{}, userBefore{}, kernelAfter{}, userAfter{};
        CHECK(GetProcessTimes(background.info.hProcess, &created, &exited, &kernelBefore, &userBefore));
        Sleep(1500);
        CHECK(GetProcessTimes(background.info.hProcess, &created, &exited, &kernelAfter, &userAfter));
        CHECK(WaitForSingleObject(background.info.hProcess, 0) == WAIT_TIMEOUT);
        CHECK(IsWindow(main));
        const auto ticks = [](FILETIME time) { return (static_cast<unsigned long long>(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
        PROCESS_MEMORY_COUNTERS_EX memory{};
        CHECK(GetProcessMemoryInfo(background.info.hProcess, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)));
        std::cout << "Background idle sample: " << memory.PrivateUsage / 1024 << " KiB private memory; "
            << (ticks(kernelAfter) + ticks(userAfter) - ticks(kernelBefore) - ticks(userBefore)) / 10000.0
            << " ms CPU over 1500 ms\n";
        PostMessageW(main, WM_APP + 20, MAKELPARAM(100, 100), MAKELPARAM(WM_CONTEXTMENU, 1));
        CHECK(Until([&] { return background.Window(L"#32768", true) != nullptr; }));
        PostMessageW(main, WM_KEYDOWN, VK_END, 0);
        PostMessageW(main, WM_KEYDOWN, VK_RETURN, 0);
        background.Exited();
    }
    std::cout << "second-instance capture routing and packed tray menu Quit passed\n";
}
} // namespace

int main(int argc, char**) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 1;
    int result = 0;
    try {
        if (argc > 1) {
            int count = 0;
            auto** args = CommandLineToArgvW(GetCommandLineW(), &count);
            CHECK(args != nullptr && count > 1);
            std::wstring executable(args[1]);
            LocalFree(args);
            Lifecycle(executable);
        } else {
            Components();
        }
        std::cout << checks << " checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        result = 1;
    }
    CoUninitialize();
    return result;
}
