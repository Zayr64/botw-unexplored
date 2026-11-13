#pragma once

#include <array>
#include <iosfwd>

#include "LegendCategories.h"

namespace SettingsIO
{
    constexpr int CurrentVersion = 2;
    // One entry per legend category, in the place each release gave it. Entries 0-4 are the first five
    // categories. Entry 5 belonged to the names of the locations, which the app no longer draws, and entry 6 to
    // the display-mode button of older releases: both are still read and written, so that those files stay
    // valid, and ignored by the app, like showMode. The categories added later (chests, walls) are the entries
    // from 7 on. They go at the end of the file, past where a release without them stops reading: the version
    // is the same and each release reads the file of the other.
    constexpr size_t CategoryCount = 10;
    constexpr size_t FirstRetiredEntry = 5;
    constexpr size_t RetiredEntries = 2;
    constexpr size_t BaseCategoryCount = 7;
    static_assert(LegendCategories::ButtonCount + RetiredEntries == CategoryCount, "Every legend category needs a settings entry");

    // Where the legend category with this index is kept in Settings::visible.
    constexpr size_t CategoryIndex(size_t legendCategory)
    {
        return legendCategory < FirstRetiredEntry ? legendCategory : legendCategory + RetiredEntries;
    }

    struct Settings
    {
        float cameraX = 0.0f;
        float cameraY = 0.0f;
        float zoom = 0.25f;
        bool legendOpen = true;
        std::array<bool, CategoryCount> visible = {{true, true, false, false, false, false, false, false, false, false}};
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
