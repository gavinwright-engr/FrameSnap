# FrameSnap review

This review focuses on predictable lifecycle, low idle cost, capture privacy, and a small install. Research uses Microsoft's published Windows documentation, read from its MicrosoftDocs source repositories.

## Findings addressed

| Finding | Resulting behavior |
| --- | --- |
| Tray events negotiated `NOTIFYICON_VERSION_4` but compared the entire `lParam` to old-style message IDs. | Decode the low word, support keyboard activation, recreate the icon after Explorer restarts, and dismiss context menus correctly. This explains why tray actions such as Quit could appear unresponsive. |
| Every launch opened Settings; startup conflicts showed a dialog. | Default launch captures once. `--settings` is explicit; `--background` stays quiet even on conflict. A second launch forwards its requested action. |
| Three workers/listeners and DXGI devices were initialized before capture. | IPC uses the existing window; copying needs no worker; saving has an on-demand worker that exits when idle. D3D fallback is scoped to one capture. |
| Full-desktop GDI and preview/editor images could remain after use. | Capture buffers and hidden preview/editor documents are released. One-shot mode terminates after completion/cancellation. Allocation and queued-save memory are bounded. |
| Clipboard code called `OpenClipboard(nullptr)` and ignored copy failure. | A real window owns fully rendered DIBV5 data; retry waits are bounded and failures are visible. The shared editor/capture path applies clipboard privacy metadata and avoids PNG compression before copying. |
| GDI's undefined alpha could become transparent PNG or clipboard content. | Normalize screenshot alpha to opaque; test actual pixel values and PNG decoding. |
| Saving could overwrite captures from the same second and expose partially written files. | Unique names, a temporary file in the destination directory, and atomic promotion; automatic saves never replace an existing file. Explicit editor saves may replace their own prior export. |
| Autosave failures were discarded, and the save budget excluded the active job. | Report failures, account for active pixels, and drain accepted work before exit. |
| Launch/settings code changed Windows' Print Screen preference and removed unrelated legacy startup names. | Windows keyboard settings are read-only; opening the Windows settings screen is explicit. Only FrameSnap's own Run value changes, on request. |
| The default hotkey conflicted with Windows' Win+Shift+S. | Use Ctrl+Alt+S for fresh settings; retain explicitly saved preferences and report conflicts without installing a fallback global keyboard hook. |
| Owner-drawn checkbox style was combined with `BS_AUTOCHECKBOX`. | Use native checkboxes, tab stops, and dialog-style keyboard navigation; keep recording hooks limited to active recording. |
| Shutdown queries performed disk writes, `GetMessage` errors were not handled, and HWNDs were not set during construction callbacks. | Respond immediately to session queries, handle errors explicitly, initialize handles and preserve default title initialization in `WM_NCCREATE`, use class-based single-instance lookup, and destroy UI resources before GDI+ shuts down. |
| Build script assumed `C:\BuildTools`, ignored its configuration switch, and compiled in the caller's directory. | CMake owns compiler configuration, flags, correct libraries, tests, and ZIP packaging. Release uses a static C++ runtime; MSVC enables CFG, ASLR, DEP, high-entropy ASLR, and CET compatibility. |
| No installation/uninstallation path or regression coverage. | Per-user install with a shell hotkey and Installed apps entry; migrate recognized portable startup entries to the installed executable with quiet launch; preserve user data on removal; component, lifecycle, portable-policy, and Windows installer tests. |

## What the hotkey can and cannot do

`RegisterHotKey` delivers a message to a live process. An exited application cannot receive that message. A Windows shell shortcut provides an OS-managed launch hotkey, so FrameSnap itself can be absent until needed. That gives zero FrameSnap idle CPU/memory, at the cost of process-launch and Explorer shortcut latency. Optional resident mode keeps a message loop and tray icon for faster activation and custom registered hotkeys. There is no timer-based capture or periodic desktop polling while idle.

## Product boundaries and next work

- **HDR:** the fast path is GDI/SDR; existing fallback tone mapping is not evidence of accurate HDR output. Investigate Windows Graphics Capture with a float pipeline and documented color conversion before claiming HDR fidelity. Do not advertise color-accurate HDR capture without hardware comparisons.
- **Redaction:** highlighters are translucent and erasing removes annotations. A future redaction tool needs opaque pixel replacement and flattened exports, with original/autosave behavior made explicit.
- **Clipboard privacy:** Windows exclusion metadata controls built-in history/sync, not arbitrary clipboard readers. Do not market it as clipboard encryption or isolation.
- **Saves:** normal Quit drains accepted writes. A disconnected or stalled filesystem can delay completion; the installer reports a timeout instead of force-killing an app that may still be saving. Crash recovery of temporary partial files is not implemented.
- **Distribution:** release signing and publisher reputation remain deployment work. This change creates local ZIPs and CI artifacts; it does not publish a release, sign binaries, or bypass platform reputation checks.
- **Accessibility and displays:** native checkbox semantics and keyboard navigation improve the existing UI, but screen-reader, high-contrast, per-monitor scaling, 1366×768 layouts, and keyboard-only editor review still need native testing. The current settings/editor layout is designed for larger desktops.

## Windows release validation

### Checks completed in the cloud workspace

- MinGW-w64 x64 Release build and ZIP packaging completed. The executable imports only Windows system DLLs; its PE flags enable ASLR, high-entropy ASLR, and DEP. The MSVC-specific mitigation flags still require a native build.
- Native Linux portable tests passed 20 checks for launch options and allocation limits.
- Wine on an isolated virtual desktop passed 444 component checks and 80 lifecycle checks, including actual capture, clipboard use after process exit, Settings Close, capture cancellation, second-instance forwarding, packed tray activation/menu Quit, conflicting hotkeys, and session shutdown.
- PowerShell parsed the build, install, uninstall, and installer-test scripts successfully. ZIP contents and the packaged executable were checked; a SHA-256 file accompanies the archive.
- No native Windows/MSVC build or installer execution has been performed here. The added CI workflow is configured to run those checks but has not been executed in this workspace. Wine's reported private-memory counter was zero and is unsuitable for a memory claim; native performance measurements remain necessary.

### Native release checks

Use a disposable Windows 10/11 desktop and run `scripts/build.ps1 -Configuration Release -Test -Package`, then `tests/install-tests.ps1 -ExecutablePath out\Release\FrameSnap.exe`.

Before shipping, verify:

1. Fresh per-user installation, a path containing spaces, reinstall, and uninstall; no elevation and no sign-in registration unless enabled. Preserve unrelated Run values, user files, settings, and captures.
2. Ctrl+Alt+S via the installed Explorer shortcut with no FrameSnap process; capture/cancel/preview/editor completion leaves no process. Clipboard pastes into Paint, Office, browsers, and the intended chat apps after exit.
3. Optional background mode: no startup window on sign-in or hotkey conflict; tray mouse and keyboard menus, Quit, Settings Close, `--quit`, Explorer restart, sign-out/restart, and rapid second launches.
4. Repeated large captures and saves: memory returns near its resting level; idle CPU remains near zero; a full queue or unwritable/offline folder reports failure; accepted saves are not silently discarded.
5. Multiple monitors, negative coordinates, mixed DPI, display hot-plug, remote sessions, lock/unlock, and cancellation when focus changes. HDR and protected content require separate platform validation.
6. Installer and executable Authenticode signatures, release checksums, and delivery through the intended trusted channel.

## Research references

- [NOTIFYICONDATAW: version 4 event layout](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-notifyicondataw)
- [RegisterHotKey: reserved combinations, conflicts, and MOD_NOREPEAT](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey)
- [IShellLinkW::SetHotkey](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishelllinkw-sethotkey)
- [OpenClipboard: a null owner followed by EmptyClipboard can make SetClipboardData fail](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-openclipboard)
- [SetClipboardData: ownership transfer and delayed rendering](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setclipboarddata)
- [Clipboard formats: history and cloud exclusion](https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-formats#cloud-clipboard-and-clipboard-history-formats)
- [WM_QUERYENDSESSION: return promptly, defer cleanup](https://learn.microsoft.com/en-us/windows/win32/shutdown/wm-queryendsession)
- [Run and RunOnce registry keys](https://learn.microsoft.com/en-us/windows/win32/setupapi/run-and-runonce-registry-keys)
- [Dynamic-link library security](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-security)
- [Desktop Duplication API: capture, rotation, and desktop-image handling](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api)
