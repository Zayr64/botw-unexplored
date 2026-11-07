#include "Localization.h"

#include <array>

namespace
{
    const std::string english[] = {
        "Exit", "Choose another profile", "No save data found for that user", "Make sure you chose the correct profile.",
        "BotW is running - can't load save", "Please run this app at least once without BotW running", "After that you can use it while playing.",
        "No", "Yes", "Master mode save file detected", "Would you like to load it?", "Legend", "X - close", "B/X - back", "Koroks",
        "Shrines", "Hinoxes", "Taluses", "Moldugas", "Locations", "Show Completed", "B - mark as found",
        "Press X: legend", "BotW is running.", "Loaded older save", "Press Y: master mode",
        "Press Y: normal mode", "L/R: zoom  (-): user  (+): exit", "Missing", "Completed", "All",
        "Position", "Found", "Not found", "Object info", "Next missing", "Korok", "Shrine", "DLC Shrine", "Location", "Hinox", "Stone Talus", "Molduga", "Map", "Completed", "All chests", "Distance from Link"
    };

    static_assert(sizeof(english) / sizeof(english[0]) == static_cast<size_t>(Localization::Text::Count), "English strings are incomplete");
}

const std::string& Localization::Get(Text text)
{
    return english[static_cast<unsigned int>(text)];
}

const std::string& Localization::GetShowModeName(int showMode)
{
    const int clamped = showMode < 0 || showMode > 2 ? 0 : showMode;
    return Get(static_cast<Text>(static_cast<int>(Text::Missing) + clamped));
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
