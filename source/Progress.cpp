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

Progress::Counter Progress::Compendium(IsSet isSet)
{
    Counter counter = CountSet(Data::CompendiumFlags, Data::CompendiumFlagsCount, isSet);
    counter.total = CompendiumEntries;
    counter.done = std::min(counter.done, counter.total);
    return counter;
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
        }
        for (int i = 0; i < DivineBeastCount; ++i)
            result.push_back(ShrineFlags::Crc32(divineBeastFlags[i]));
        result.insert(result.end(), Data::DivineBeastChestFlags, Data::DivineBeastChestFlags + Data::DivineBeastChestFlagsCount);
        result.insert(result.end(), Data::FinalTrialChestFlags, Data::FinalTrialChestFlags + Data::FinalTrialChestFlagsCount);
        result.insert(result.end(), Data::CompendiumFlags, Data::CompendiumFlags + Data::CompendiumFlagsCount);
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }();
    return flags;
}
