#pragma once

#include <cstdint>
#include <vector>

#include "ProgressData.h"

// The counters of the progress panel that come straight from flags of the save: quests, Divine Beasts and
// their chests, the compendium. Each takes the test "is this flag set?", so that it can be checked without
// a save.
namespace Progress
{
    struct Counter
    {
        int done = 0;
        int total = 0;
        int open = 0; // quests taken and not finished
    };

    using IsSet = bool (*)(uint32_t flagHash);

    // The Hyrule Compendium has 385 entries; the save has a flag for a few actors more, which stay unset.
    constexpr int CompendiumEntries = 385;

    Counter Quests(Data::QuestKind kind, bool dlc, IsSet isSet);
    Counter DivineBeasts(IsSet isSet);
    Counter DivineBeastChests(IsSet isSet);
    Counter FinalTrialChests(IsSet isSet);
    Counter Compendium(IsSet isSet);

    // Every flag the counters above read, sorted and without repeats: what the reader of the save keeps.
    const std::vector<uint32_t>& TrackedFlags();
}
