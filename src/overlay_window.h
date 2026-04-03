#pragma once

#include "common.h"
#include "types.h"

class OverlayWindow {
public:
    OverlayWindow(HINSTANCE instance, HWND owner);

    bool BeginSession(const AppSettings& settings, std::chrono::steady_clock::time_point hotkeyStart, const std::shared_ptr<ImageData>& frozenFrame);
    void Cancel();
    bool IsActive() const;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK FreezeWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleFreezeMessage(UINT message, WPARAM wParam, LPARAM lParam);
    bool EnsureWindow();
    bool EnsureFreezeWindow();
    HCURSOR CreateCaptureCursor() const;
    void Paint();
    void PaintFrozenFrame();
    void FinishSelection(const RECT& rect, bool clickModeCompletion);
    POINT ScreenFromClientPoint(POINT point) const;
    RECT CurrentSelectionRect() const;
    RECT CurrentVisualBounds() const;
    void InvalidateVisualDelta();
    void UpdateWindowRegion();
    void PresentNow();
    void ConfigurePresentTimer(POINT screenPoint);

    HINSTANCE instance_{};
    HWND owner_{};
    HCURSOR captureCursor_{};
    HWND freezeHwnd_{};
    HWND hwnd_{};
    AppSettings settings_{};
    bool active_{false};
    bool mouseDown_{false};
    bool dragging_{false};
    bool anchorSet_{false};
    POINT dragStart_{};
    POINT dragCurrent_{};
    POINT anchorPoint_{};
    std::chrono::steady_clock::time_point hotkeyStart_{};
    RECT virtualBounds_{};
    RECT lastVisualBounds_{};
    bool hasLastVisualBounds_{false};
    std::shared_ptr<ImageData> frozenFrame_;
    UINT presentIntervalMs_{16};
    bool pendingPresent_{false};
    bool highResTimerEnabled_{false};
};
