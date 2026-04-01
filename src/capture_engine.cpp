#include "capture_engine.h"

#include "util.h"

namespace {

constexpr std::array kPreferredFormats{
    DXGI_FORMAT_R16G16B16A16_FLOAT,
    DXGI_FORMAT_B8G8R8A8_UNORM,
};

bool CreateDeviceForAdapter(IDXGIAdapter1* adapter, ComPtr<ID3D11Device>& device, ComPtr<ID3D11DeviceContext>& context) {
    constexpr UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    static constexpr std::array levels{
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    D3D_FEATURE_LEVEL level{};
    return SUCCEEDED(D3D11CreateDevice(
        adapter,
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        flags,
        levels.data(),
        static_cast<UINT>(levels.size()),
        D3D11_SDK_VERSION,
        device.GetAddressOf(),
        &level,
        context.GetAddressOf()));
}

bool OutputIsHdr(IDXGIOutput* output) {
    ComPtr<IDXGIOutput6> output6;
    if (FAILED(output->QueryInterface(IID_PPV_ARGS(output6.GetAddressOf())))) {
        return false;
    }
    DXGI_OUTPUT_DESC1 desc{};
    if (FAILED(output6->GetDesc1(&desc))) {
        return false;
    }
    return desc.BitsPerColor > 8 ||
           desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
           desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
}

}  // namespace

bool CaptureEngine::Initialize() {
    return EnumerateOutputs();
}

void CaptureEngine::RefreshOutputs() {
    EnumerateOutputs();
}

bool CaptureEngine::EnumerateOutputs() {
    sessions_.clear();

    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())))) {
        return false;
    }

    for (UINT adapterIndex = 0;; ++adapterIndex) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(adapterIndex, adapter.GetAddressOf()) == DXGI_ERROR_NOT_FOUND) {
            break;
        }

        for (UINT outputIndex = 0;; ++outputIndex) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(outputIndex, output.GetAddressOf()) == DXGI_ERROR_NOT_FOUND) {
                break;
            }

            DXGI_OUTPUT_DESC desc{};
            if (FAILED(output->GetDesc(&desc)) || !desc.AttachedToDesktop) {
                continue;
            }

            MonitorSession session{};
            session.monitor = desc.Monitor;
            session.bounds = desc.DesktopCoordinates;
            session.hdrSource = OutputIsHdr(output.Get());
            session.adapter = adapter;
            output.As(&session.output1);
            output.As(&session.output5);

            if (!CreateDeviceForAdapter(adapter.Get(), session.device, session.context)) {
                continue;
            }
            CreateDuplication(session);
            sessions_.push_back(std::move(session));
        }
    }
    return !sessions_.empty();
}

bool CaptureEngine::CreateDuplication(MonitorSession& session) {
    session.duplication.Reset();
    session.staging.Reset();
    session.stagingFormat = DXGI_FORMAT_UNKNOWN;
    session.stagingWidth = 0;
    session.stagingHeight = 0;
    HRESULT hr = E_FAIL;
    if (session.output5 != nullptr) {
        hr = session.output5->DuplicateOutput1(
            session.device.Get(),
            0,
            static_cast<UINT>(kPreferredFormats.size()),
            kPreferredFormats.data(),
            session.duplication.GetAddressOf());
    }
    if (FAILED(hr) && session.output1 != nullptr) {
        hr = session.output1->DuplicateOutput(session.device.Get(), session.duplication.GetAddressOf());
    }
    return SUCCEEDED(hr);
}

float CaptureEngine::HalfToFloat(std::uint16_t value) {
    const std::uint32_t sign = static_cast<std::uint32_t>(value & 0x8000U) << 16U;
    std::uint32_t exponent = (value >> 10U) & 0x1FU;
    std::uint32_t mantissa = value & 0x03FFU;
    std::uint32_t output = 0;

    if (exponent == 0) {
        if (mantissa == 0) {
            output = sign;
        } else {
            exponent = 127 - 15 + 1;
            while ((mantissa & 0x0400U) == 0U) {
                mantissa <<= 1U;
                --exponent;
            }
            mantissa &= 0x03FFU;
            output = sign | (exponent << 23U) | (mantissa << 13U);
        }
    } else if (exponent == 31U) {
        output = sign | 0x7F800000U | (mantissa << 13U);
    } else {
        output = sign | ((exponent + (127 - 15)) << 23U) | (mantissa << 13U);
    }

    float result = 0.0f;
    memcpy(&result, &output, sizeof(result));
    return result;
}

std::uint8_t CaptureEngine::ToneMapToByte(float value) {
    value = std::max(0.0f, value);
    const float mapped = value / (1.0f + value);
    const float gamma = std::pow(mapped, 1.0f / 2.2f);
    return static_cast<std::uint8_t>(util::Clamp(std::lround(gamma * 255.0f), 0l, 255l));
}

std::shared_ptr<ImageData> CaptureEngine::Capture(const RECT& selection) {
    const RECT normalized = util::NormalizeRect(selection);
    if (util::IsRectEmptySafe(normalized) || sessions_.empty()) {
        return nullptr;
    }

    auto image = std::make_shared<ImageData>();
    image->sourceRect = normalized;
    image->width = normalized.right - normalized.left;
    image->height = normalized.bottom - normalized.top;
    image->pixels.resize(static_cast<std::size_t>(image->width) * static_cast<std::size_t>(image->height) * 4U);

    bool any = false;
    for (auto& session : sessions_) {
        const RECT intersection = util::IntersectRectSafe(normalized, session.bounds);
        if (util::IsRectEmptySafe(intersection)) {
            continue;
        }
        any = CaptureSessionRegion(session, intersection, *image) || any;
    }
    return any ? image : nullptr;
}

bool CaptureEngine::CaptureSessionRegion(MonitorSession& session, const RECT& intersection, ImageData& output) {
    if (session.duplication == nullptr && !CreateDuplication(session)) {
        return false;
    }

    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    ComPtr<IDXGIResource> resource;
    HRESULT hr = session.duplication->AcquireNextFrame(33, &frameInfo, resource.GetAddressOf());
    if (hr == DXGI_ERROR_ACCESS_LOST) {
        if (!CreateDuplication(session)) {
            return false;
        }
        hr = session.duplication->AcquireNextFrame(33, &frameInfo, resource.GetAddressOf());
    }
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        hr = session.duplication->AcquireNextFrame(120, &frameInfo, resource.GetAddressOf());
    }
    if (FAILED(hr)) {
        return false;
    }

    auto guard = std::unique_ptr<void, std::function<void(void*)>>(
        resource.Get(),
        [&session](void*) {
            session.duplication->ReleaseFrame();
        });

    ComPtr<ID3D11Texture2D> frameTexture;
    if (FAILED(resource.As(&frameTexture))) {
        return false;
    }

    D3D11_TEXTURE2D_DESC frameDesc{};
    frameTexture->GetDesc(&frameDesc);

    const LONG localLeft = intersection.left - session.bounds.left;
    const LONG localTop = intersection.top - session.bounds.top;
    const UINT copyWidth = static_cast<UINT>(intersection.right - intersection.left);
    const UINT copyHeight = static_cast<UINT>(intersection.bottom - intersection.top);

    D3D11_TEXTURE2D_DESC stagingDesc{};
    stagingDesc.Width = copyWidth;
    stagingDesc.Height = copyHeight;
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.Format = frameDesc.Format;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    if (session.staging == nullptr || session.stagingFormat != frameDesc.Format || session.stagingWidth != copyWidth || session.stagingHeight != copyHeight) {
        session.staging.Reset();
        if (FAILED(session.device->CreateTexture2D(&stagingDesc, nullptr, session.staging.GetAddressOf()))) {
            return false;
        }
        session.stagingFormat = frameDesc.Format;
        session.stagingWidth = copyWidth;
        session.stagingHeight = copyHeight;
    }

    D3D11_BOX sourceBox{
        static_cast<UINT>(localLeft),
        static_cast<UINT>(localTop),
        0,
        static_cast<UINT>(localLeft) + copyWidth,
        static_cast<UINT>(localTop) + copyHeight,
        1,
    };
    session.context->CopySubresourceRegion(session.staging.Get(), 0, 0, 0, 0, frameTexture.Get(), 0, &sourceBox);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(session.context->Map(session.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return false;
    }
    auto unmapGuard = std::unique_ptr<void, std::function<void(void*)>>(
        session.staging.Get(),
        [&session](void* resourcePtr) {
            session.context->Unmap(static_cast<ID3D11Texture2D*>(resourcePtr), 0);
        });

    const bool hdrFloat = frameDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
    output.hdrSource = output.hdrSource || session.hdrSource || hdrFloat;

    for (UINT y = 0; y < copyHeight; ++y) {
        const auto* srcRow = static_cast<const std::uint8_t*>(mapped.pData) + mapped.RowPitch * y;
        const LONG dstY = (intersection.top - output.sourceRect.top) + static_cast<LONG>(y);
        auto* dstRow = output.pixels.data() + (static_cast<std::size_t>(dstY) * static_cast<std::size_t>(output.width) + static_cast<std::size_t>(intersection.left - output.sourceRect.left)) * 4U;

        if (!hdrFloat) {
            memcpy(dstRow, srcRow, static_cast<std::size_t>(copyWidth) * 4U);
            continue;
        }

        const auto* srcPixels = reinterpret_cast<const std::uint16_t*>(srcRow);
        for (UINT x = 0; x < copyWidth; ++x) {
            const float r = HalfToFloat(srcPixels[x * 4 + 0]);
            const float g = HalfToFloat(srcPixels[x * 4 + 1]);
            const float b = HalfToFloat(srcPixels[x * 4 + 2]);
            dstRow[x * 4 + 0] = ToneMapToByte(b);
            dstRow[x * 4 + 1] = ToneMapToByte(g);
            dstRow[x * 4 + 2] = ToneMapToByte(r);
            dstRow[x * 4 + 3] = 255;
        }
    }
    return true;
}
