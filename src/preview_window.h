#pragma once

#include "common.h"
#include "types.h"

class PreviewWindow {
public:
    PreviewWindow(HINSTANCE instance, HWND owner);
    ~PreviewWindow();

    void Show(const std::shared_ptr<ImageData>& image, UINT timeoutMs);
    void Hide();
    std::shared_ptr<ImageData> CurrentImage() const;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    bool EnsureWindow();
    void Paint();

    HINSTANCE instance_{};
    HWND owner_{};
    HWND hwnd_{};
    std::shared_ptr<ImageData> image_;
    HFONT titleFont_{};
    HFONT bodyFont_{};
};
