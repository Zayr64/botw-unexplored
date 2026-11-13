#include "Progress.h"

#include <algorithm>

#include "ShrineFlags.h"

namespace
{
    // Set when the Divine Beast is freed.
    const char* const divineBeastFlags[] = {
        "Clear_RemainsWind", "Clear_RemainsElectric", "Clear_RemainsFire", "Clear_RemainsWater"
    };
    constexpr int DivineBeastCount = sizeof(divineBeastFlags) / sizeof(divineBeastFlags[0]);

    Progress::Counter CountSet(const uint32_t* flags, int count, Progress::IsSet isSet)
    {
        Progress::Counter counter;
        counter.total = count;
        for (int i = 0; i < count; ++i)
            counter.done += isSet(flags[i]) ? 1 : 0;
        return counter;
    }

    // Whether this game has the entry at all.
    bool Exists(const Data::CompendiumEntry& entry, Progress::Game game)
    {
        if ((entry.flags & Data::CompendiumDlc) && !game.expansionPass)
            return false;
        return !(entry.flags & Data::CompendiumMasterMode) || game.masterMode;
    }

    Progress::Counter CountCompendium(int category, Progress::IsSet isSet, Progress::Game game)
    {
        Progress::Counter counter;
        for (int i = 0; i < Data::CompendiumEntriesCount; ++i)
        {
            const Data::CompendiumEntry& entry = Data::CompendiumEntries[i];
            if ((category >= 0 && entry.category != category) || !Exists(entry, game))
                continue;
            ++counter.total;
            counter.done += isSet(entry.hash) ? 1 : 0;
        }
        return counter;
    }
}

Progress::Counter Progress::Quests(Data::QuestKind kind, bool dlc, IsSet isSet)
{
    Counter counter;
    for (int i = 0; i < Data::QuestsCount; ++i)
    {
        const Data::Quest& quest = Data::Quests[i];
        if (quest.kind != static_cast<uint8_t>(kind) || (quest.dlc != 0) != dlc)
            continue;
        ++counter.total;
        if (isSet(quest.finishHash))
            ++counter.done;
        else if (isSet(quest.startHash))
            ++counter.open;
    }
    return counter;
}

Progress::Counter Progress::DivineBeasts(IsSet isSet)
{
    Counter counter;
    counter.total = DivineBeastCount;
    for (int i = 0; i < DivineBeastCount; ++i)
        counter.done += isSet(ShrineFlags::Crc32(divineBeastFlags[i])) ? 1 : 0;
    return counter;
}

Progress::Counter Progress::DivineBeastChests(IsSet isSet)
{
    return CountSet(Data::DivineBeastChestFlags, Data::DivineBeastChestFlagsCount, isSet);
}

Progress::Counter Progress::FinalTrialChests(IsSet isSet)
{
    return CountSet(Data::FinalTrialChestFlags, Data::FinalTrialChestFlagsCount, isSet);
}

Progress::Counter Progress::Compendium(IsSet isSet, Game game)
{
    return CountCompendium(-1, isSet, game);
}

Progress::Counter Progress::Compendium(Data::CompendiumCategory category, IsSet isSet, Game game)
{
    return CountCompendium(static_cast<int>(category), isSet, game);
}

Progress::Counter Progress::Armor(IsSet isSet, Game game)
{
    Counter counter;
    // The flags of a piece are together: count each piece once, owned when any of its flags is set.
    for (int i = 0; i < Data::ArmorFlagsCount;)
    {
        const uint8_t piece = Data::ArmorFlags[i].piece;
        const bool exists = Data::ArmorFlags[i].dlc == 0 || game.expansionPass;
        bool owned = false;
        for (; i < Data::ArmorFlagsCount && Data::ArmorFlags[i].piece == piece; ++i)
            owned = owned || isSet(Data::ArmorFlags[i].hash);
        if (!exists)
            continue;
        ++counter.total;
        counter.done += owned ? 1 : 0;
    }
    return counter;
}

Progress::Counter Progress::Memories(IsSet isSet)
{
    Counter counter = CountSet(Data::MemoryFlags, Data::MemoryFlagsCount, isSet);
    const int pictures = CountSet(Data::MemoryPhotoFlags, Data::MemoryPhotoFlagsCount, isSet).done;
    counter.total = std::min(Data::MemoryFlagsCount, std::max(pictures, counter.done));
    return counter;
}

Progress::QuestsLeft Progress::QuestsToTake(int region, IsSet isSet)
{
    QuestsLeft left;
    for (int i = 0; i < Data::QuestsCount; ++i)
    {
        const Data::Quest& quest = Data::Quests[i];
        if (quest.dlc != 0 || quest.kind == static_cast<uint8_t>(Data::QuestKind::Main))
            continue;
        if (region >= 0 && quest.region != region)
            continue;
        if (isSet(quest.startHash) || isSet(quest.finishHash))
            continue;
        if (quest.requiresHash == 0 || isSet(quest.requiresHash))
            ++left.now;
        else
            ++left.later;
    }
    return left;
}

const std::vector<uint32_t>& Progress::TrackedFlags()
{
    static const std::vector<uint32_t> flags = []()
    {
        std::vector<uint32_t> result;
        for (int i = 0; i < Data::QuestsCount; ++i)
        {
            result.push_back(Data::Quests[i].startHash);
            result.push_back(Data::Quests[i].finishHash);
            if (Data::Quests[i].requiresHash != 0)
                result.push_back(Data::Quests[i].requiresHash);
        }
        for (int i = 0; i < DivineBeastCount; ++i)
            result.push_back(ShrineFlags::Crc32(divineBeastFlags[i]));
        result.insert(result.end(), Data::DivineBeastChestFlags, Data::DivineBeastChestFlags + Data::DivineBeastChestFlagsCount);
        result.insert(result.end(), Data::FinalTrialChestFlags, Data::FinalTrialChestFlags + Data::FinalTrialChestFlagsCount);
        for (int i = 0; i < Data::CompendiumEntriesCount; ++i)
            result.push_back(Data::CompendiumEntries[i].hash);
        for (int i = 0; i < Data::ArmorFlagsCount; ++i)
            result.push_back(Data::ArmorFlags[i].hash);
        result.insert(result.end(), Data::MemoryFlags, Data::MemoryFlags + Data::MemoryFlagsCount);
        result.insert(result.end(), Data::MemoryPhotoFlags, Data::MemoryPhotoFlags + Data::MemoryPhotoFlagsCount);
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }();
    return flags;
}
