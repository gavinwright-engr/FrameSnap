#pragma once

#include "color_picker.h"
#include "common.h"
#include "image_io.h"
#include "types.h"

class EditorWindow {
public:
    EditorWindow(HINSTANCE instance, HWND owner);
    ~EditorWindow();

    void Show(const std::shared_ptr<ImageData>& image, AppSettings& settings);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void ApplyWindowChrome() const;
    HFONT CreateUiFont(int height, int weight) const;
    bool EnsureWindow();
    void ApplyFonts() const;
    void LayoutControls();
    void Paint();
    void PaintButton(const DRAWITEMSTRUCT& drawItem) const;
    bool IsToolButton(UINT controlId) const;
    RECT CanvasRect() const;
    RECT SidebarRect() const;
    RECT ImageViewRect() const;
    std::optional<FloatPoint> ClientToImage(POINT point) const;
    Gdiplus::RectF ImageToViewRect() const;
    void InvalidateCanvas(bool erase = false) const;
    void InvalidateSidebar() const;
    void RefreshButtons() const;
    void BeginStroke(FloatPoint point);
    void BreakStroke();
    void ExtendStroke(FloatPoint point);
    void EndStroke();
    void EraseStrokeAt(FloatPoint point);
    void EraseBrushAt(FloatPoint point);
    void PickColorAt(FloatPoint point);
    void UpdateToolbarState();
    void UpdateWidthsFromSlider();
    void SaveImage();
    ImageData RenderDocument() const;

    HINSTANCE instance_{};
    HWND owner_{};
    HWND hwnd_{};
    HWND penButton_{};
    HWND highlighterButton_{};
    HWND eraseButton_{};
    HWND eraseBrushButton_{};
    HWND eyedropperButton_{};
    HWND undoButton_{};
    HWND redoButton_{};
    HWND saveButton_{};
    HWND widthSlider_{};
    std::unique_ptr<ColorPickerControl> colorPicker_;
    std::shared_ptr<ImageData> image_;
    AppSettings* settings_{};
    ActiveTool activeTool_{ActiveTool::Pen};
    ActiveTool inkTool_{ActiveTool::Pen};
    std::vector<Stroke> strokes_;
    std::vector<Stroke> redoStrokes_;
    bool drawing_{false};
    Stroke inFlightStroke_{};
    ImageIo imageIo_;
    HFONT uiFont_{};
    HFONT titleFont_{};
    HFONT hintFont_{};
};
