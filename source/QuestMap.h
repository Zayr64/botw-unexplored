#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The side and shrine quests on the map: where each one is taken (where its giver stands), its name, place and
// description. They come from sdmc:/switch/botw-unexplored/quests.txt, which tools/gen_quests.py makes from the
// game's own files, so it is not in this repository; without the file the map has no quests.
namespace QuestMap
{
    struct Entry
    {
        bool shrine = false; // a shrine quest; else a side quest
        bool dlc = false;    // comes with the Expansion Pass
        uint32_t startHash = 0;    // <name>_Activated: the quest was taken
        uint32_t finishHash = 0;   // the flag of its last step: the quest is done
        uint32_t requiresHash = 0; // has to be set before the quest can be taken; 0 for none
        float x = 0.0f;
        float z = 0.0f; // the game's coordinates, like the other objects
        std::string name;
        std::string location; // the place the game gives the quest ("Hateno Village"); may be empty
        std::string description;
    };

    // The text of a quests.txt: a first line "# BOTW_UNEXPLORED_QUESTS 1", then one quest per line, tab separated:
    // kind (side or shrine), dlc (0/1), the three flag hashes, x, z, name, location, description. False, with no
    // entries, when it is not such a file; lines that do not read are skipped.
    bool Parse(const std::string& text, std::vector<Entry>& entries);

    // Reads the file into Entries(); false when it is missing or not a quests file.
    bool Load(const std::string& path);
    const std::vector<Entry>& Entries();

    // The region (Regions.h) where the quest is taken, from the quest tables of the app; -1 when it has none.
    int RegionOf(uint32_t startHash);
}

namespace Data
{
    // The quests draw with two icons: those that can be taken now and those that wait for something first.
    struct QuestIcon {};
    struct QuestLaterIcon {};
}
