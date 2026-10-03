#include "clipboard_publisher.h"

bool ClipboardPublisher::Publish(HWND owner, const ImageData& image) {
    if (!IsWindow(owner) || !image.IsValid()) return false;

    const UINT privacyFormat = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
    if (privacyFormat == 0) return false;
    HGLOBAL privacy = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DWORD));
    HGLOBAL dib = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPV5HEADER) + image.pixels.size());
    if (privacy == nullptr || dib == nullptr) {
        if (privacy != nullptr) GlobalFree(privacy);
        if (dib != nullptr) GlobalFree(dib);
        return false;
    }
    auto* data = static_cast<BYTE*>(GlobalLock(dib));
    if (data == nullptr) {
        GlobalFree(privacy);
        GlobalFree(dib);
        return false;
    }
    auto* header = reinterpret_cast<BITMAPV5HEADER*>(data);
    *header = {};
    header->bV5Size = sizeof(*header);
    header->bV5Width = image.width;
    header->bV5Height = -image.height;
    header->bV5Planes = 1;
    header->bV5BitCount = 32;
    header->bV5Compression = BI_BITFIELDS;
    header->bV5SizeImage = static_cast<DWORD>(image.pixels.size());
    header->bV5RedMask = 0x00FF0000;
    header->bV5GreenMask = 0x0000FF00;
    header->bV5BlueMask = 0x000000FF;
    header->bV5AlphaMask = 0xFF000000;
    header->bV5CSType = LCS_sRGB;
    header->bV5Intent = LCS_GM_IMAGES;
    memcpy(data + sizeof(*header), image.pixels.data(), image.pixels.size());
    GlobalUnlock(dib);

    bool opened = false;
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (OpenClipboard(owner)) {
            opened = true;
            break;
        }
        Sleep(8);
    }
    bool copied = false;
    if (opened) {
        if (EmptyClipboard() && SetClipboardData(privacyFormat, privacy) != nullptr) {
            privacy = nullptr; // Windows owns successful transfers.
            copied = SetClipboardData(CF_DIBV5, dib) != nullptr;
            if (copied) dib = nullptr;
        }
        CloseClipboard();
    }
    if (privacy != nullptr) GlobalFree(privacy);
    if (dib != nullptr) GlobalFree(dib);
    return copied;
}
