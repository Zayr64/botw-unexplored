#include "Localization.h"

#include <array>

namespace
{
    const std::string english[] = {
        "Exit", "Choose another profile", "No save data found for that user", "Make sure you chose the correct profile.",
        "BotW is running and could not be read", "Open this app once with BotW closed:", "it keeps a copy of the save to show while you play.",
        "No", "Yes", "Master mode save file detected", "Would you like to load it?", "Legend", "B - close", "B - back", "Koroks",
        "Shrines", "Hinoxes", "Taluses", "Moldugas", "Locations", "Y - mark as found",
        "Press X: legend", "BotW is running.", "Loaded older save", "Press Y: master mode",
        "Press Y: normal mode", "ZL/ZR: nearest  R: center  L: Link  (+): exit",
        "Position", "Found", "Not found", "Object info", "Nearest missing", "Korok", "Shrine", "DLC Shrine", "Location", "Hinox", "Stone Talus", "Molduga", "Map", "Completed", "All chests", "Distance from Link",
        "of Link", "of the last save", "Open", "Nearest", "At aim", "Link",
        "Legend", "Sync", "Exit", "Back", "Photo", "Mark found",
        "Previous / next", "Show / hide", "Close", "Select", "Master mode", "Normal mode",
        "Zoom", "Map completion", "Live from the game", "Save backup", "Save", "slot",
        "Discovered", "Not discovered", "Not completed", "All chests opened", "Chests left", "Marked as found by you",
        "Visited", "Not visited", "Defeated", "Not defeated", "Korok seed", "Link is inside",
        "Marked as found", "Synced with the game", "Synced with the game: nothing new", "Could not read the game", "Nothing under the aim", "Nothing missing in the categories shown",
        "new", "of"
    };

    static_assert(sizeof(english) / sizeof(english[0]) == static_cast<size_t>(Localization::Text::Count), "English strings are incomplete");
}

const std::string& Localization::Get(Text text)
{
    return english[static_cast<unsigned int>(text)];
}

const std::string& Localization::GetObjectTypeName(Data::ObjectType objectType)
{
    const std::array<Text, static_cast<size_t>(Data::ObjectType::Count)> names = {{
        Text::Korok, Text::Shrine, Text::DLCShrine, Text::Location,
        Text::Hinox, Text::StoneTalus, Text::Molduga
    }};
    static_assert(names.size() == static_cast<size_t>(Data::ObjectType::Count), "Singular names must match ObjectType");
    const int index = static_cast<int>(objectType);
    if (index < 0 || index >= static_cast<int>(names.size()))
        return Get(Text::ObjectInfo);
    return Get(names[static_cast<size_t>(index)]);
}
