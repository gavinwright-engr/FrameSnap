#pragma once

#include "common.h"
#include "types.h"

class CaptureEngine {
public:
    bool Initialize();
    std::shared_ptr<ImageData> Capture(const RECT& selection);
    void RefreshOutputs();

private:
    struct MonitorSession {
        HMONITOR monitor{};
        RECT bounds{};
        bool hdrSource{};
        ComPtr<IDXGIAdapter1> adapter;
        ComPtr<IDXGIOutput1> output1;
        ComPtr<IDXGIOutput5> output5;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<IDXGIOutputDuplication> duplication;
        ComPtr<ID3D11Texture2D> staging;
        DXGI_FORMAT stagingFormat{DXGI_FORMAT_UNKNOWN};
        UINT stagingWidth{};
        UINT stagingHeight{};
    };

    bool EnumerateOutputs();
    bool CreateDuplication(MonitorSession& session);
    bool CaptureSessionRegion(MonitorSession& session, const RECT& intersection, ImageData& output);
    static float HalfToFloat(std::uint16_t value);
    static std::uint8_t ToneMapToByte(float value);

    std::vector<MonitorSession> sessions_;
};
