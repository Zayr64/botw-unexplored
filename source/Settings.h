#pragma once

#include <array>
#include <iosfwd>

#include "LegendCategories.h"

namespace SettingsIO
{
    constexpr int CurrentVersion = 2;
    // One entry per legend category, plus a last one for the display-mode button older releases had.
    // That entry and showMode are still read and written so older files stay valid, but the app ignores them.
    constexpr size_t CategoryCount = 7;
    static_assert(LegendCategories::ButtonCount < CategoryCount, "Every legend category needs a settings entry");

    struct Settings
    {
        float cameraX = 0.0f;
        float cameraY = 0.0f;
        float zoom = 0.25f;
        bool legendOpen = true;
        std::array<bool, CategoryCount> visible = {{true, true, false, false, false, false, false}};
        int showMode = 0;  // Kept in the file format for older releases; the app shows only what is missing.
        int language = -1; // Kept in the file format for older releases; the app is English only.
    };

    enum class LoadResult
    {
        Missing,
        Current,
        Legacy,
        Invalid,
        FutureVersion
    };

    LoadResult Load(std::istream& input, Settings& settings);
    bool Save(std::ostream& output, const Settings& settings);
}
