#include "Localization.h"

#include <array>

namespace
{
    const std::string english[] = {
        "Exit", // Exit
        "Choose another profile", // ChooseAnotherProfile
        "No save data found for that user", // NoSaveDataTitle
        "Make sure you chose the correct profile.", // NoSaveDataDescription
        "BotW is running and could not be read", // GameRunningTitle
        "Open this app once with BotW closed:", // GameRunningDescription
        "it keeps a copy of the save to show while you play.", // GameRunningDescription2
        "No", // No
        "Legend", // Legend
        "B - close", // Close
        "Koroks", // Koroks
        "Shrines", // Shrines
        "Hinoxes", // Hinoxes
        "Taluses", // Taluses
        "Moldugas", // Moldugas
        "Locations", // Locations
        "Y - mark as found", // MarkFound
        "Position", // Position
        "Found", // Found
        "Not found", // NotFound
        "Object info", // ObjectInfo
        "Nearest missing", // NextMissing
        "Korok", // Korok
        "Shrine", // Shrine
        "DLC Shrine", // DLCShrine
        "Location", // Location
        "Hinox", // Hinox
        "Stone Talus", // StoneTalus
        "Molduga", // Molduga
        "Map", // MapCompletion
        "Completed", // ShrineCompleted
        "of Link", // OfLink
        "of the last save", // OfLastSave
        "Open", // HintOpen
        "Nearest", // HintNearest
        "At aim", // HintAtAim
        "Link", // HintLink
        "Legend", // HintLegend
        "Sync", // HintSync
        "Exit", // HintExit
        "Back", // HintBack
        "Photo", // HintPhoto
        "Mark found", // HintMarkFound
        "Previous / next", // HintPreviousNext
        "Show / hide", // HintShowHide
        "Close", // HintClose
        "Select", // HintSelect
        "Master mode", // HintMasterMode
        "Normal mode", // HintNormalMode
        "Map completion", // MapCompletionLong
        "Discovered", // Discovered
        "Not discovered", // NotDiscovered
        "Not completed", // NotCompleted
        "All chests opened", // AllChestsOpened
        "Chests left", // ChestsLeft
        "Marked as found by you", // MarkedByYou
        "Defeated", // Defeated
        "Not defeated", // NotDefeated
        "Korok seed", // KorokSeed
        "Marked as found", // ToastMarked
        "Synced with the game", // ToastSynced
        "Synced with the game: nothing new", // ToastNoChange
        "Could not read the game", // ToastSyncFailed
        "Nothing under the aim", // ToastNothingHere
        "Nothing missing in the categories shown", // ToastNothingMissing
        "new", // NewSince
        "of", // Of
        "Chests", // Chests
        "Armor & key items", // ArmorChests
        "Rare weapons", // WeaponChests
        "Rare materials", // MaterialChests
        "Treasure chest", // TreasureChest
        "Special chest", // SpecialChest
        "Not opened", // NotOpened
        "Opened", // Opened
        "Unlocks when its camp is cleared", // ChestEnemyCamp
        "Shows up during a side quest", // ChestSideQuest
        "Breakable walls", // Walls
        "Breakable wall", // BreakableWall
        "Broken", // Broken
        "Not broken", // NotBroken
        "Towers", // Towers
        "Chests in shrines", // ShrineChests
        "Great Fairies", // GreatFairies
        "Divine Beasts", // DivineBeasts
        "Chests in Divine Beasts", // DivineBeastChests
        "Hyrule Compendium", // Compendium
        "Creatures", // Creatures
        "Monsters", // Monsters
        "Materials", // Materials
        "Equipment", // Equipment
        "Treasure", // Treasure
        "Memories", // Memories
        "quests", // QuestsToTake
        "Blood Moon", // BloodMoon
        "Hero's Path", // HeroPath
        "Quests", // Quests
        "Quest", // Quest
        "Side quest", // SideQuestTitle
        "Shrine quest", // ShrineQuestTitle
        "Not taken yet", // QuestNotTaken
        "Not available yet: something else first", // QuestNotYet
        "Main quest", // MainQuestTitle
        "In progress", // QuestInProgress
        "Where the game's marker for this step points", // QuestMarkerHere
        "No marker in the game for this step: where the quest was taken", // QuestNoMarker
        "Tower", // Tower
        "Great Fairy Fountain", // GreatFairy
        "Activated", // TowerActivated
        "Not activated", // TowerNotActivated
        "The fairy is awake", // FairyAwake
        "The fairy sleeps", // FairyAsleep
        "You already have it: not opened", // ChestItemHad
        "The horse god is awake", // HorseGodAwake
        "The horse god sleeps", // HorseGodAsleep
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
        Text::Hinox, Text::StoneTalus, Text::Molduga, Text::TreasureChest, Text::SpecialChest,
        Text::BreakableWall, Text::Quest, Text::Tower, Text::GreatFairy
    }};
    static_assert(names.size() == static_cast<size_t>(Data::ObjectType::Count), "Singular names must match ObjectType");
    const int index = static_cast<int>(objectType);
    if (index < 0 || index >= static_cast<int>(names.size()))
        return Get(Text::ObjectInfo);
    return Get(names[static_cast<size_t>(index)]);
}
