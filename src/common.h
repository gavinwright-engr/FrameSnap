#pragma once

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_6.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <queue>
#include <span>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

inline constexpr wchar_t kFrameSnapAppName[] = L"FrameSnap";
inline constexpr wchar_t kFrameSnapMainWindowClassName[] = L"FrameSnapMainWindow";
inline constexpr wchar_t kFrameSnapSettingsWindowClassName[] = L"FrameSnapSettingsWindow";
inline constexpr wchar_t kFrameSnapSingleInstanceMutexName[] = L"Local\\FrameSnap.SingleInstance";
inline constexpr wchar_t kFrameSnapShowSettingsEventName[] = L"Local\\FrameSnap.ShowSettings";

inline constexpr UINT WM_APP_CAPTURE_READY = WM_APP + 1;
inline constexpr UINT WM_APP_PREVIEW_CLICKED = WM_APP + 2;
inline constexpr UINT WM_APP_SETTINGS_APPLIED = WM_APP + 3;
inline constexpr UINT WM_APP_COLOR_CHANGED = WM_APP + 4;
inline constexpr UINT WM_APP_CAPTURE_CANCELLED = WM_APP + 5;
inline constexpr UINT WM_APP_EXIT_REQUESTED = WM_APP + 6;
inline constexpr UINT WM_APP_SHOW_SETTINGS = WM_APP + 7;
