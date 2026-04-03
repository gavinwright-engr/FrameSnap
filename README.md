# FrameSnap

Prototype exploring low-latency screenshot pipeline on Windows (DXGI, HDR-aware, async)

- global hotkey via `RegisterHotKey`
- dimming overlay with drag or click-click rectangle selection
- DXGI desktop duplication capture with HDR-aware float fallback tone mapping to SDR
- immediate clipboard publish
- RAM-first async PNG save queue
- bottom-right preview popup
- lightweight editor with pen, highlighter, eyedropper, width control, color wheel, undo/redo, and whole-stroke erase

## Build

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1
```

Built binary:

- `out\Debug\FrameSnap.exe`

## Notes

- Settings are stored under `%LOCALAPPDATA%\FrameSnap\settings.ini`.
- Metrics are logged under `%LOCALAPPDATA%\FrameSnap\metrics.log`.
- This is a native v1 implementation, not a full Snagit/ShareX replacement. The core hot path is implemented; broader workflows and richer markup tools are intentionally deferred.
