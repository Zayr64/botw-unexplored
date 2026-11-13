#pragma once

#include <array>
#include <iosfwd>

#include "LegendCategories.h"

namespace SettingsIO
{
    constexpr int CurrentVersion = 2;
    // One entry per legend category, in the place each release gave it:
    //    0-4  Koroks, shrines, Hinoxes, Taluses, Moldugas
    //    5    the names of the locations, which the app no longer draws
    //    6    the display-mode button of the first releases
    //    7    the ordinary chests, which the app no longer draws
    //    8    the special chests; since they were split in three, those with armor and key items
    //    9    the breakable walls
    //   10    the chests with rare weapons
    //   11    the chests with rare materials
    // Entries 5 to 7 are still read and written, so that those files stay valid, and ignored by the app, like
    // showMode. The entries from 7 on go at the end of the file, past where a release without them stops
    // reading: the version is the same and each release reads the file of the other.
    constexpr size_t CategoryCount = 12;
    constexpr size_t RetiredEntries = 3;
    constexpr size_t BaseCategoryCount = 7;
    constexpr size_t SpecialChestsEntry = 8;
    constexpr size_t FirstSplitEntry = 10; // a file that ends before it gives the two new groups the value of entry 8
    static_assert(LegendCategories::ButtonCount + RetiredEntries == CategoryCount, "Every legend category needs a settings entry");

    // Where the legend category with this index is kept in Settings::visible.
    constexpr size_t CategoryIndex(size_t legendCategory)
    {
        constexpr size_t entries[LegendCategories::ButtonCount] = {0, 1, 2, 3, 4, 8, 10, 11, 9};
        return entries[legendCategory];
    }

    struct Settings
    {
        float cameraX = 0.0f;
        float cameraY = 0.0f;
        float zoom = 0.25f;
        bool legendOpen = true;
        std::array<bool, CategoryCount> visible = {{true, true, false, false, false, false, false, false, false, false,
                                                    false, false}};
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
