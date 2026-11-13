#pragma once

#include <cstdint>
#include <vector>

#include "ProgressData.h"

// The counters of the progress and collection panels that come straight from flags of the save: quests,
// Divine Beasts and their chests, the compendium, armor, memories. Each takes the test "is this flag set?",
// so that it can be checked without a save.
namespace Progress
{
    struct Counter
    {
        int done = 0;
        int total = 0;
        int open = 0; // quests taken and not finished
    };

    using IsSet = bool (*)(uint32_t flagHash);

    // What the game has changes what there is to count.
    struct Game
    {
        bool expansionPass = false;
        bool masterMode = false;
    };

    Counter Quests(Data::QuestKind kind, bool dlc, IsSet isSet);
    Counter DivineBeasts(IsSet isSet);
    Counter DivineBeastChests(IsSet isSet);
    Counter FinalTrialChests(IsSet isSet);

    // The Hyrule Compendium: 385 entries, 389 with the Champions' Ballad, 5 more in Master Mode.
    Counter Compendium(IsSet isSet, Game game);
    Counter Compendium(Data::CompendiumCategory category, IsSet isSet, Game game);

    // Armor pieces owned, at any level of upgrade. Those that only amiibo give are not counted.
    Counter Armor(IsSet isSet, Game game);

    // Memories recalled, out of the pictures the album has so far: the game tells of twelve and shows one
    // more at the end. Nothing to count (total 0) before the album has any.
    Counter Memories(IsSet isSet);

    // The shrine and side quests of the base game that are not in the log yet.
    struct QuestsLeft
    {
        int now = 0;   // can be taken
        int later = 0; // wait for something else to happen first
    };
    // region: one of Regions.h, or -1 for the whole map (which includes the quest with no known region).
    QuestsLeft QuestsToTake(int region, IsSet isSet);

    // Every flag the counters above read, sorted and without repeats: what the reader of the save keeps.
    const std::vector<uint32_t>& TrackedFlags();
}
