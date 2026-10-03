#pragma once

#include <optional>
#include <span>
#include <string_view>

enum class LaunchMode { Capture, Background, Settings, Quit, Help };

// No arguments means one capture, with no resident process afterward.
inline std::optional<LaunchMode> ParseLaunchMode(std::span<const std::wstring_view> arguments) {
    if (arguments.empty()) return LaunchMode::Capture;
    if (arguments.size() != 1) return std::nullopt;
    const auto argument = arguments.front();
    if (argument == L"--capture") return LaunchMode::Capture;
    if (argument == L"--background") return LaunchMode::Background;
    if (argument == L"--settings") return LaunchMode::Settings;
    if (argument == L"--quit") return LaunchMode::Quit;
    if (argument == L"--help" || argument == L"-h" || argument == L"/?") return LaunchMode::Help;
    return std::nullopt;
}
