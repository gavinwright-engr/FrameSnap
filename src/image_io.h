#pragma once

#include "common.h"
#include "types.h"

class ImageIo {
public:
    ImageIo();

    bool SavePng(const ImageData& image, const std::wstring& path);
    std::vector<std::uint8_t> EncodePng(const ImageData& image);

private:
    ComPtr<IWICImagingFactory> factory_;
    bool EnsureFactory();
};
