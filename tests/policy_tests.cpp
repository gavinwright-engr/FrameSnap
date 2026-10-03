#include "launch_options.h"
#include "image_layout.h"
#include <array>
#include <climits>
#include <iostream>
#include <stdexcept>

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(#condition); ++checks; } while (false)
int main() {
    int checks = 0;
    try {
        CHECK(ParseLaunchMode({}) == LaunchMode::Capture);
        for (auto [argument, expected] : std::array<std::pair<std::wstring_view, LaunchMode>, 5>{{
            {L"--capture", LaunchMode::Capture}, {L"--settings", LaunchMode::Settings},
            {L"--background", LaunchMode::Background}, {L"--quit", LaunchMode::Quit},
            {L"--help", LaunchMode::Help}}}) {
            std::array<std::wstring_view, 1> args{argument};
            CHECK(ParseLaunchMode(args) == expected);
        }
        std::array<std::wstring_view, 2> conflict{L"--capture", L"--background"};
        CHECK(!ParseLaunchMode(conflict));
        std::array<std::wstring_view, 1> unknown{L"--backgroud"};
        CHECK(!ParseLaunchMode(unknown));
        CHECK(ImageByteSize(1, 1) == 4);
        CHECK(ImageByteSize(3840, 2160) == 33177600);
        CHECK(ImageByteSize(15360, 4320) == 265420800);
        CHECK(!ImageByteSize(0, 100));
        CHECK(!ImageByteSize(-1, 100));
        CHECK(!ImageByteSize(100, -1));
        CHECK(!ImageByteSize(INT_MAX, INT_MAX));
        CHECK(!ImageByteSize(INT_MAX, 1));
        CHECK(!ImageByteSize(INT_MAX / 2 + 1, 1));
        CHECK(!ImageByteSize(1, INT_MAX));
        CHECK(ImageByteSize(8192, 16384) == kMaxImageBytes);
        CHECK(!ImageByteSize(8192, 16385));
        std::cout << checks << " policy checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
