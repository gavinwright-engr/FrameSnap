#include "settings_store.h"

#include "util.h"

namespace {

std::wstring ReadIniString(const std::filesystem::path& path, const wchar_t* section, const wchar_t* key, const std::wstring& fallback) {
    std::array<wchar_t, 1024> buffer{};
    GetPrivateProfileStringW(section, key, fallback.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()), path.c_str());
    return buffer.data();
}

int ReadIniInt(const std::filesystem::path& path, const wchar_t* section, const wchar_t* key, int fallback) {
    return static_cast<int>(GetPrivateProfileIntW(section, key, fallback, path.c_str()));
}

bool WriteIniInt(const std::filesystem::path& path, const wchar_t* section, const wchar_t* key, int value) {
    return WritePrivateProfileStringW(section, key, std::to_wstring(value).c_str(), path.c_str()) == TRUE;
}

bool WriteIniString(const std::filesystem::path& path, const wchar_t* section, const wchar_t* key, const std::wstring& value) {
    return WritePrivateProfileStringW(section, key, value.c_str(), path.c_str()) == TRUE;
}

}  // namespace

SettingsStore::SettingsStore()
    : path_(util::EnsureAppDirectory() / L"settings.ini") {}

AppSettings SettingsStore::Load() const {
    AppSettings settings{};
    settings.saveFolder = util::DefaultSaveFolder().wstring();
    settings.runAtStartupEnabled = ReadIniInt(path_, L"app", L"run_at_startup", 1) != 0;
    settings.printScreenOverrideEnabled = ReadIniInt(path_, L"app", L"print_screen_override", util::IsPrintScreenSnippingEnabled() ? 0 : 1) != 0;
    settings.autoSaveEnabled = ReadIniInt(path_, L"capture", L"auto_save", 1) != 0;
    settings.clickModeEnabled = ReadIniInt(path_, L"capture", L"click_mode", 1) != 0;
    settings.soundEnabled = ReadIniInt(path_, L"capture", L"sound", 1) != 0;
    settings.previewTimeoutMs = static_cast<UINT>(ReadIniInt(path_, L"capture", L"preview_timeout_ms", 3000));
    settings.dragThresholdPx = static_cast<UINT>(ReadIniInt(path_, L"capture", L"drag_threshold_px", 4));
    settings.hotkey.modifiers = static_cast<UINT>(ReadIniInt(path_, L"hotkey", L"modifiers", MOD_WIN | MOD_SHIFT));
    settings.hotkey.virtualKey = static_cast<UINT>(ReadIniInt(path_, L"hotkey", L"virtual_key", 'S'));
    settings.penWidth = static_cast<float>(ReadIniInt(path_, L"markup", L"pen_width", 3));
    settings.highlighterWidth = static_cast<float>(ReadIniInt(path_, L"markup", L"highlighter_width", 14));
    settings.penColor = static_cast<COLORREF>(ReadIniInt(path_, L"markup", L"pen_color", RGB(255, 72, 72)));
    settings.highlighterColor = static_cast<COLORREF>(ReadIniInt(path_, L"markup", L"highlighter_color", RGB(255, 230, 0)));

    const auto folder = ReadIniString(path_, L"capture", L"save_folder", settings.saveFolder);
    settings.saveFolder = folder.empty() ? util::DefaultSaveFolder().wstring() : folder;
    std::error_code error;
    std::filesystem::create_directories(settings.saveFolder, error);
    return settings;
}

bool SettingsStore::Save(const AppSettings& settings) const {
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(settings.saveFolder), error);
    return WriteIniInt(path_, L"app", L"run_at_startup", settings.runAtStartupEnabled ? 1 : 0) &&
           WriteIniInt(path_, L"app", L"print_screen_override", settings.printScreenOverrideEnabled ? 1 : 0) &&
           WriteIniInt(path_, L"capture", L"auto_save", settings.autoSaveEnabled ? 1 : 0) &&
           WriteIniInt(path_, L"capture", L"click_mode", settings.clickModeEnabled ? 1 : 0) &&
           WriteIniInt(path_, L"capture", L"sound", settings.soundEnabled ? 1 : 0) &&
           WriteIniInt(path_, L"capture", L"preview_timeout_ms", static_cast<int>(settings.previewTimeoutMs)) &&
           WriteIniInt(path_, L"capture", L"drag_threshold_px", static_cast<int>(settings.dragThresholdPx)) &&
           WriteIniString(path_, L"capture", L"save_folder", settings.saveFolder) &&
           WriteIniInt(path_, L"hotkey", L"modifiers", static_cast<int>(settings.hotkey.modifiers)) &&
           WriteIniInt(path_, L"hotkey", L"virtual_key", static_cast<int>(settings.hotkey.virtualKey)) &&
           WriteIniInt(path_, L"markup", L"pen_width", static_cast<int>(std::round(settings.penWidth))) &&
           WriteIniInt(path_, L"markup", L"highlighter_width", static_cast<int>(std::round(settings.highlighterWidth))) &&
           WriteIniInt(path_, L"markup", L"pen_color", static_cast<int>(settings.penColor)) &&
           WriteIniInt(path_, L"markup", L"highlighter_color", static_cast<int>(settings.highlighterColor));
}
