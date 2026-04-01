#pragma once

#include "common.h"
#include "types.h"

class SettingsStore {
public:
    SettingsStore();

    AppSettings Load() const;
    bool Save(const AppSettings& settings) const;

private:
    std::filesystem::path path_;
};
