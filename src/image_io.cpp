#include "image_io.h"

namespace {

bool WriteBitmapToFrame(IWICBitmapFrameEncode* frame, const ImageData& image) {
    if (FAILED(frame->SetSize(static_cast<UINT>(image.width), static_cast<UINT>(image.height)))) {
        return false;
    }
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&format))) {
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

bool ImageIo::SavePng(const ImageData& image, const std::wstring& path) {
    if (!EnsureFactory()) {
        return false;
    }

    ComPtr<IWICStream> stream;
    if (FAILED(factory_->CreateStream(&stream))) {
        return false;
    }
    if (FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) {
        return false;
    }

    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(factory_->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))) {
        return false;
    }
    if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) {
        return false;
    }

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> bag;
    if (FAILED(encoder->CreateNewFrame(&frame, &bag))) {
        return false;
    }
    if (FAILED(frame->Initialize(bag.Get()))) {
        return false;
    }
    if (!WriteBitmapToFrame(frame.Get(), image)) {
        return false;
    }
    return SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

std::vector<std::uint8_t> ImageIo::EncodePng(const ImageData& image) {
    std::vector<std::uint8_t> bytes;
    if (!EnsureFactory()) {
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
    const auto size = static_cast<ULONG>(stats.cbSize.QuadPart);
    if (size == 0) {
        return bytes;
    }
    bytes.resize(size);
    LARGE_INTEGER origin{};
    memoryStream->Seek(origin, STREAM_SEEK_SET, nullptr);
    ULONG read = 0;
    if (FAILED(memoryStream->Read(bytes.data(), size, &read))) {
        bytes.clear();
        return bytes;
    }
    bytes.resize(read);
    return bytes;
}
