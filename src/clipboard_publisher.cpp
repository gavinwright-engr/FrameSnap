#include "clipboard_publisher.h"

namespace {

bool OpenClipboardWithRetry() {
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (OpenClipboard(nullptr)) {
            return true;
        }
        Sleep(8);
    }
    return false;
}

}  // namespace

ClipboardPublisher::ClipboardPublisher() = default;

ClipboardPublisher::~ClipboardPublisher() {
    Stop();
}

bool ClipboardPublisher::Start() {
    if (running_.exchange(true)) {
        return true;
    }
    thread_ = std::thread([this] { ThreadMain(); });
    return true;
}

void ClipboardPublisher::Stop() {
    if (!running_.exchange(false)) {
        return;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool ClipboardPublisher::PublishBlocking(const std::shared_ptr<ImageData>& image) {
    if (image == nullptr) {
        return false;
    }
    if (!running_) {
        Start();
    }
    auto request = std::make_shared<Request>();
    request->image = image;
    request->doneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (request->doneEvent == nullptr) {
        return false;
    }
    {
        std::scoped_lock lock(mutex_);
        queue_.push(request);
    }
    cv_.notify_one();
    WaitForSingleObject(request->doneEvent, INFINITE);
    CloseHandle(request->doneEvent);
    return request->success;
}

void ClipboardPublisher::ThreadMain() {
    const HRESULT coInitializeResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool coInitialized = SUCCEEDED(coInitializeResult);
    for (;;) {
        std::shared_ptr<Request> request;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return !running_ || !queue_.empty(); });
            if (!running_ && queue_.empty()) {
                break;
            }
            request = queue_.front();
            queue_.pop();
        }
        request->success = PublishNow(request->image);
        SetEvent(request->doneEvent);
    }
    if (coInitialized) {
        CoUninitialize();
    }
}

HGLOBAL ClipboardPublisher::CreateDibv5(const ImageData& image) const {
    const SIZE_T headerSize = sizeof(BITMAPV5HEADER);
    const SIZE_T pixelSize = image.pixels.size();
    const HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, headerSize + pixelSize);
    if (handle == nullptr) {
        return nullptr;
    }
    auto* data = static_cast<BYTE*>(GlobalLock(handle));
    if (data == nullptr) {
        GlobalFree(handle);
        return nullptr;
    }

    auto* header = reinterpret_cast<BITMAPV5HEADER*>(data);
    ZeroMemory(header, sizeof(*header));
    header->bV5Size = sizeof(BITMAPV5HEADER);
    header->bV5Width = image.width;
    header->bV5Height = -image.height;
    header->bV5Planes = 1;
    header->bV5BitCount = 32;
    header->bV5Compression = BI_BITFIELDS;
    header->bV5RedMask = 0x00FF0000;
    header->bV5GreenMask = 0x0000FF00;
    header->bV5BlueMask = 0x000000FF;
    header->bV5AlphaMask = 0xFF000000;
    memcpy(data + headerSize, image.pixels.data(), pixelSize);
    GlobalUnlock(handle);
    return handle;
}

bool ClipboardPublisher::PublishNow(const std::shared_ptr<ImageData>& image) {
    HGLOBAL dib = CreateDibv5(*image);
    if (dib == nullptr) {
        return false;
    }

    const UINT pngFormat = RegisterClipboardFormatW(L"PNG");
    const auto pngBytes = imageIo_.EncodePng(*image);
    HGLOBAL pngHandle = nullptr;
    if (!pngBytes.empty()) {
        pngHandle = GlobalAlloc(GMEM_MOVEABLE, pngBytes.size());
        if (pngHandle != nullptr) {
            if (void* data = GlobalLock(pngHandle)) {
                memcpy(data, pngBytes.data(), pngBytes.size());
                GlobalUnlock(pngHandle);
            } else {
                GlobalFree(pngHandle);
                pngHandle = nullptr;
            }
        }
    }

    if (!OpenClipboardWithRetry()) {
        GlobalFree(dib);
        if (pngHandle != nullptr) {
            GlobalFree(pngHandle);
        }
        return false;
    }

    EmptyClipboard();
    const bool dibOk = SetClipboardData(CF_DIBV5, dib) != nullptr;
    const bool pngOk = pngHandle == nullptr || SetClipboardData(pngFormat, pngHandle) != nullptr;
    CloseClipboard();

    if (!dibOk) {
        GlobalFree(dib);
    }
    if (pngHandle != nullptr && !pngOk) {
        GlobalFree(pngHandle);
    }
    return dibOk;
}
