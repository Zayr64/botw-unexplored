#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// The quests on the map: where each side and shrine quest is taken (where its giver stands), and, for a quest in
// progress (main ones too), where its current step leads, as the game's own marker does. Names, places, descriptions
// and the quest log's text of each step come along. They come from sdmc:/switch/botw-unexplored/quests.txt, which
// tools/gen_quests.py makes from the game's own files, so it is not in this repository; without the file the map has
// no quests.
namespace QuestMap
{
    constexpr int MaxMarkers = 4; // the most markers a step has in the game (four relics, for one)

    struct Marker
    {
        float x = 0.0f;
        float z = 0.0f;
        uint32_t offHash = 0; // the marker goes away once this flag is set; 0 for none
    };

    struct Step
    {
        uint32_t flagHash = 0; // set when the quest reaches this step
        bool ends = false;     // reaching it ends the quest (a "Finish" before the last step)
        std::vector<Marker> markers; // where the game's marker points; none for many steps
        std::string text;            // the quest log's text for it
    };

    struct Entry
    {
        bool shrine = false; // a shrine quest
        bool main = false;   // a main quest: on the map only while in progress
        bool dlc = false;    // comes with the Expansion Pass
        uint32_t startHash = 0;    // <name>_Activated: the quest was taken
        uint32_t finishHash = 0;   // the flag of its last step: the quest is done
        uint32_t requiresHash = 0; // has to be set before the quest can be taken; 0 for none
        float x = 0.0f;
        float z = 0.0f; // where it is taken, in the game's coordinates like the other objects
        std::string name;
        std::string location; // the place the game gives the quest ("Hateno Village"); may be empty
        std::string description;
        std::vector<Step> steps; // every step but the last (version 2 files); empty in version 1
    };

    // The text of a quests.txt: a first line "# BOTW_UNEXPLORED_QUESTS 1" or "2", then one quest per line, tab
    // separated: kind (side, shrine or, in version 2, main), dlc (0/1), the three flag hashes, x, z, name, location,
    // description. Version 2 follows each quest with its steps: "step", the hash of its flag, 1 if it ends the quest,
    // its markers ("x,z,<off flag hash>" separated by ";", or empty) and its text. False, with no entries, when it is
    // not such a file; lines that do not read are skipped (and the steps after a skipped quest).
    bool Parse(const std::string& text, std::vector<Entry>& entries);

    // Reads the file into Entries(); false when it is missing or not a quests file.
    bool Load(const std::string& path);
    const std::vector<Entry>& Entries();
    // Every flag the loaded quests use (start, end, dependency, steps, markers), sorted: the save and the running game
    // keep them for IsProgressFlagSet (SavefileIO), which only keeps the flags something asks for.
    const std::vector<uint32_t>& Flags();

    // The step a quest is at while in progress: the last one whose flag is set, once the quest is taken; -1 when it
    // is not taken, is done (its last flag, or a step that ends it) or the file has no steps.
    int CurrentStep(const Entry& entry, const std::function<bool(uint32_t)>& isSet);

    // Where a quest in progress leads now: the markers of its current step that are still on; none when the step has
    // no marker in the game (the map then shows where the quest was taken).
    std::vector<Marker> ActiveMarkers(const Entry& entry, int step, const std::function<bool(uint32_t)>& isSet);

    // Whether a quest is done: its last flag, or a step that ends it (version 1 files: the last flag only).
    bool IsDone(const Entry& entry, const std::function<bool(uint32_t)>& isSet);

    // The region (Regions.h) where the quest is taken, from the quest tables of the app; -1 when it has none.
    int RegionOf(uint32_t startHash);
}

namespace Data
{
    // The quests draw with three icons: those that can be taken now, those that wait for something first, and where
    // a quest in progress leads.
    struct QuestIcon {};
    struct QuestLaterIcon {};
    struct QuestActiveIcon {};
}
