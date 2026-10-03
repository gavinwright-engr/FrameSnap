#pragma once

#include <cstddef>
#include <optional>

// Bound allocations and the UINT byte counts accepted by WIC.
inline constexpr std::size_t kMaxImageBytes = 512ULL * 1024 * 1024;
inline constexpr std::optional<std::size_t> ImageByteSize(int width, int height) {
    if (width <= 0 || height <= 0) return std::nullopt;
    if (static_cast<std::size_t>(width) > kMaxImageBytes / 4) return std::nullopt;
    const auto rowBytes = static_cast<std::size_t>(width) * 4;
    if (rowBytes > kMaxImageBytes || static_cast<std::size_t>(height) > kMaxImageBytes / rowBytes) return std::nullopt;
    return rowBytes * static_cast<std::size_t>(height);
}
