#include "image_io.h"

namespace {

bool WriteBitmapToFrame(IWICBitmapFrameEncode* frame, const ImageData& image) {
    if (FAILED(frame->SetSize(static_cast<UINT>(image.width), static_cast<UINT>(image.height)))) {
        return false;
    }
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&format)) || !IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)) {
        return false;
    }
    return SUCCEEDED(frame->WritePixels(
        static_cast<UINT>(image.height),
        static_cast<UINT>(image.width * 4),
        static_cast<UINT>(image.pixels.size()),
        const_cast<BYTE*>(image.pixels.data())));
}

}  // namespace

ImageIo::ImageIo() = default;

bool ImageIo::EnsureFactory() {
    if (factory_ != nullptr) {
        return true;
    }
    return SUCCEEDED(CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(factory_.ReleaseAndGetAddressOf())));
}

bool ImageIo::SavePng(const ImageData& image, const std::wstring& path, bool replaceExisting) {
    if (!image.IsValid() || path.empty()) return false;
    const auto bytes = EncodePng(image);
    if (bytes.empty()) return false;
    const std::filesystem::path destination(path);
    std::error_code error;
    if (!destination.parent_path().empty()) std::filesystem::create_directories(destination.parent_path(), error);
    if (error) return false;
    GUID id{};
    wchar_t suffix[40]{};
    if (FAILED(CoCreateGuid(&id)) || StringFromGUID2(id, suffix, static_cast<int>(std::size(suffix))) == 0) return false;
    const std::wstring temporary = path + L"." + suffix + L".partial";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool wrote = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != FALSE && written == bytes.size();
    const bool closed = CloseHandle(file) != FALSE;
    const DWORD flags = MOVEFILE_WRITE_THROUGH | (replaceExisting ? MOVEFILE_REPLACE_EXISTING : 0);
    const bool saved = wrote && closed && MoveFileExW(temporary.c_str(), path.c_str(), flags) != FALSE;
    if (!saved) DeleteFileW(temporary.c_str());
    return saved;
}

std::vector<std::uint8_t> ImageIo::EncodePng(const ImageData& image) {
    std::vector<std::uint8_t> bytes;
    if (!image.IsValid() || !EnsureFactory()) {
        return bytes;
    }

    ComPtr<IStream> memoryStream;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, memoryStream.GetAddressOf()))) {
        return bytes;
    }
    ComPtr<IWICStream> stream;
    if (FAILED(factory_->CreateStream(&stream))) {
        return bytes;
    }
    if (FAILED(stream->InitializeFromIStream(memoryStream.Get()))) {
        return bytes;
    }

    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(factory_->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))) {
        return bytes;
    }
    if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) {
        return bytes;
    }

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> bag;
    if (FAILED(encoder->CreateNewFrame(&frame, &bag))) {
        return bytes;
    }
    if (FAILED(frame->Initialize(bag.Get()))) {
        return bytes;
    }
    if (!WriteBitmapToFrame(frame.Get(), image)) {
        return bytes;
    }
    if (FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
        return bytes;
    }

    STATSTG stats{};
    if (FAILED(memoryStream->Stat(&stats, STATFLAG_NONAME))) {
        return bytes;
    }
    if (stats.cbSize.QuadPart > MAXDWORD) return bytes;
    const auto size = static_cast<ULONG>(stats.cbSize.QuadPart);
    if (size == 0) {
        return bytes;
    }
    bytes.resize(size);
    LARGE_INTEGER origin{};
    if (FAILED(memoryStream->Seek(origin, STREAM_SEEK_SET, nullptr))) return {};
    ULONG read = 0;
    if (FAILED(memoryStream->Read(bytes.data(), size, &read)) || read != size) {
        bytes.clear();
        return bytes;
    }
    bytes.resize(read);
    return bytes;
}
