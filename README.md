# FrameSnap

A small, local Windows screenshot tool: select a region, copy it, and get back to work. Native C++20; no account, network service, telemetry, administrator rights, or separate Visual C++ runtime installation.

## Install and capture

Use a Windows x64 ZIP built from this repository. Extract it and double-click **Install.cmd**. Installation is per user, under `%LOCALAPPDATA%\Programs\FrameSnap`; **Ctrl+Alt+S** launches a capture through a Windows Start-menu shortcut. The installer does not start FrameSnap or enable sign-in startup by default. Windows Installed apps includes an uninstaller. Upgrades preserve existing preferences and the installed startup choice.

For portable use, run **FrameSnap.exe** directly. It captures once and exits after the preview times out or you close the editor. Escape or right-click cancels capture and exits. The clipboard image remains available after the process exits.

- Drag a rectangle, or click two opposite corners.
- Click the preview to annotate; right-click it to dismiss.
- In the editor, Ctrl+C copies the edited image and closes; Escape or the window's Close button dismisses it.
- Open **Start > FrameSnap > Settings** to opt into saving PNGs, sounds, or sign-in startup. New installs copy to the clipboard without automatically retaining a file.
- Closing Settings quits FrameSnap. **Start > FrameSnap > Quit**, the tray menu, and `FrameSnap.exe --quit` also quit. Accepted saves finish before exit.

## Choose the background behavior

| Command | Behavior |
| --- | --- |
| `FrameSnap.exe` / `--capture` | Capture once, then exit. No idle FrameSnap process. |
| `FrameSnap.exe --background` | Stay in the tray; wait for the configured hotkey. No idle capture, polling, graphics-device sessions, or screenshot buffers. |
| `FrameSnap.exe --settings` | Open Settings. Closing it quits. |
| `FrameSnap.exe --quit` | Ask the running instance to finish saves and exit; succeeds if already stopped. |

A capture request sent while a background instance is running uses that instance and leaves it resident. Sign-in startup is always quiet, including when a hotkey is unavailable. Startup registration changes only when requested; launching the app never recreates or rewrites it.

To upgrade an older portable copy and migrate its existing startup entry, run `Install.ps1 -ExecutablePath 'C:\path\to\FrameSnap.exe'` with the replacement executable at that path. The installer recognizes startup commands for the source or installed executable and migrates them to the installed copy with `--background`; explicitly pass `-StartWithWindows:$false` to remove that startup entry instead.

The default background shortcut is also Ctrl+Alt+S. Background mode uses `RegisterHotKey`, with no continuously installed keyboard hook. It will not take over another application's shortcut. If the installed Windows shortcut already owns the combination, it can still launch/forward a capture; choose a different combination in Settings for an independent background shortcut. Win+Shift+S belongs to Windows Snipping Tool. FrameSnap does not change Windows' Print Screen preference. Hotkey recording uses a temporary hook only while recording.

For an installation that opts into background startup and leaves the shortcut to the resident listener:

```powershell
.\Install.ps1 -StartWithWindows
```

To switch an installed copy back to the on-demand default, rerun `Install.ps1 -StartWithWindows:$false`. `-ShortcutHotkey 'CTRL+ALT+S'` configures the Windows shortcut for on-demand use; its hotkey can also be changed in the shortcut's Properties. Explorer must be running for a shell shortcut hotkey to work. Hotkey availability and launch latency depend on Windows and other installed apps.

## Files and privacy

Settings: `%LOCALAPPDATA%\FrameSnap\settings.ini`. PNGs, when enabled, go to the chosen folder (default: Pictures\FrameSnap). Startup and timing logs contain operational metadata, not captured pixels or window titles; each rotates at 256 KiB with one previous file. Uninstall preserves settings and captures and removes only FrameSnap's own installation, shortcuts, and matching startup entry.

Copying marks the image for exclusion from Windows clipboard history and cloud clipboard synchronization. Other applications and third-party clipboard managers can still read the current clipboard. A save folder managed by OneDrive or another sync service can sync saved PNGs. Highlighter strokes and annotation erasing are **not redaction**; do not use them to hide sensitive information. Autosave retains the unedited capture when enabled; saving edits creates a separate image.

Captures use an SDR GDI snapshot with a DXGI fallback. Color-accurate HDR export, protected/secure desktops, mixed-DPI displays, Remote Desktop, and clipboard compatibility with individual applications need real Windows testing. FrameSnap does not bypass protected capture restrictions.

## Build and test

Use Windows 10/11 x64, Visual Studio 2022 or later with **Desktop development with C++**, the Windows SDK, and CMake 3.25+. From the checkout:

```powershell
.\scripts\build.ps1 -Configuration Release -Package
```

The build script discovers CMake, uses CMake's Visual Studio generator, and writes `out\Release\FrameSnap.exe`. ZIP packages and SHA-256 files go in `out\packages`. Debug and RelWithDebInfo builds are also supported. The packaged installer needs no developer tools or network access.

Run `build.ps1 -Test` on an isolated test desktop/profile: the Windows tests launch and close the app and intentionally replace the clipboard. CTest covers clipboard ownership and persistence, PNG encode/decode and atomic saves, queue draining/restart, settings close, quiet startup with a conflicting hotkey, tray protocol events, capture cancellation, one-shot capture, editor lifetime, and shutdown. The CI workflow also exercises install, upgrade, and uninstall on a disposable Windows runner.

On Linux, native CMake/CTest runs the portable command-line and allocation-boundary tests. To cross-compile the app with MinGW-w64:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-x64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
```

Cross-builds and Wine tests do not establish native Windows, Explorer, GPU, or HDR correctness. See [the review and Windows validation checklist](docs/product-review.md).

Release ZIPs from this change are not code-signed or published. A SHA-256 file checks download integrity; it is not proof of publisher identity. Before public distribution, sign release executables and installers using a trusted signing identity, verify the native checklist, and distribute through a trusted release channel. Do not ask users to disable SmartScreen or antivirus.
