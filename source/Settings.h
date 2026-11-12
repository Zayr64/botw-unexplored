#pragma once

#include <array>
#include <iosfwd>

#include "LegendCategories.h"

namespace SettingsIO
{
    constexpr int CurrentVersion = 2;
    // One entry per legend category. Entries 0-5 are the six categories every release had; entry 6 belonged
    // to the display-mode button of older releases (still read and written so that those files stay valid,
    // ignored by the app, like showMode). The categories added later (the chests) are the entries from 7 on.
    // They go at the end of the file, past where a release without them stops reading: the version is the
    // same and each release reads the file of the other.
    constexpr size_t CategoryCount = 9;
    constexpr size_t LegacyEntry = 6;
    constexpr size_t BaseCategoryCount = 7;
    static_assert(LegendCategories::ButtonCount + 1 == CategoryCount, "Every legend category needs a settings entry");

    // Where the legend category with this index is kept in Settings::visible.
    constexpr size_t CategoryIndex(size_t legendCategory)
    {
        return legendCategory < LegacyEntry ? legendCategory : legendCategory + 1;
    }

    struct Settings
    {
        float cameraX = 0.0f;
        float cameraY = 0.0f;
        float zoom = 0.25f;
        bool legendOpen = true;
        std::array<bool, CategoryCount> visible = {{true, true, false, false, false, false, false, false, false}};
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
