#pragma once

#include "types.h"

class ClipboardPublisher {
public:
    // Fully rendered data remains available after FrameSnap exits. No worker,
    // delayed rendering, PNG compression, or indefinitely blocking event wait.
    static bool Publish(HWND owner, const ImageData& image);
};
