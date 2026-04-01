#pragma once

#include "common.h"

class ColorPickerControl {
public:
    ColorPickerControl(HINSTANCE instance, HWND parent, int controlId);

    bool Create(int x, int y, int width, int height);
    void Move(int x, int y, int width, int height) const;
    HWND Handle() const;
    void SetColor(COLORREF color);
    COLORREF GetColor() const;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    static COLORREF HsvToColor(float hue, float saturation, float value);
    static void ColorToHsv(COLORREF color, float& hue, float& saturation, float& value);
    void Paint();
    void UpdateFromPoint(POINT point);
    RECT WheelRect() const;
    RECT ValueRect() const;
    void NotifyParent() const;

    HINSTANCE instance_{};
    HWND parent_{};
    HWND hwnd_{};
    int controlId_{};
    bool draggingWheel_{false};
    bool draggingValue_{false};
    float hue_{0.0f};
    float saturation_{1.0f};
    float value_{1.0f};
};
