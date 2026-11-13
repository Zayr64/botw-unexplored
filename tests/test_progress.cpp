#include <cassert>
#include <cstddef>
#include <set>

#include "Progress.h"

namespace
{
    std::set<uint32_t> setFlags;

    bool IsSet(uint32_t flagHash)
    {
        return setFlags.count(flagHash) != 0;
    }
}

int main()
{
    using Data::QuestKind;

    // The quests of the game's log: 15 main, 42 shrine and 77 side quests, and those of the Expansion Pass.
    assert(Data::QuestsCount == 152);
    assert(Progress::Quests(QuestKind::Main, false, IsSet).total == 15);
    assert(Progress::Quests(QuestKind::Shrine, false, IsSet).total == 42);
    assert(Progress::Quests(QuestKind::Side, false, IsSet).total == 77);
    assert(Progress::Quests(QuestKind::Main, true, IsSet).total == 5);
    assert(Progress::Quests(QuestKind::Side, true, IsSet).total == 13);
    assert(Progress::Quests(QuestKind::Shrine, true, IsSet).total == 0);

    // Values from the hash list of marcrobledo/savegame-editors (zelda-botw.hashes.csv).
    // Nothing set: nothing done, nothing open.
    Progress::Counter main = Progress::Quests(QuestKind::Main, false, IsSet);
    assert(main.done == 0 && main.open == 0);
    // Find_Impa_Activated: the quest is taken. With Find_Impa_Finish it is done, and no longer open.
    setFlags.insert(528446742u);
    main = Progress::Quests(QuestKind::Main, false, IsSet);
    assert(main.done == 0 && main.open == 1);
    setFlags.insert(1144992850u);
    main = Progress::Quests(QuestKind::Main, false, IsSet);
    assert(main.done == 1 && main.open == 0);
    // FirstTower_Finish without FirstTower_Activated still counts as done.
    setFlags.insert(3326169616u);
    main = Progress::Quests(QuestKind::Main, false, IsSet);
    assert(main.done == 2 && main.open == 0);
    assert(Progress::Quests(QuestKind::Side, false, IsSet).done == 0);

    // Clear_RemainsFire: one of the four Divine Beasts.
    assert(Progress::DivineBeasts(IsSet).total == 4 && Progress::DivineBeasts(IsSet).done == 0);
    setFlags.insert(1074258414u);
    assert(Progress::DivineBeasts(IsSet).done == 1);

    assert(Progress::DivineBeastChests(IsSet).total == 35 && Progress::FinalTrialChests(IsSet).total == 7);
    setFlags.insert(Data::DivineBeastChestFlags[3]);
    assert(Progress::DivineBeastChests(IsSet).done == 1 && Progress::FinalTrialChests(IsSet).done == 0);

    // The compendium has 385 entries, whatever the number of flags.
    assert(Data::CompendiumFlagsCount >= Progress::CompendiumEntries);
    assert(Progress::Compendium(IsSet).total == 385 && Progress::Compendium(IsSet).done == 0);
    setFlags.insert(Data::CompendiumFlags[0]);
    setFlags.insert(Data::CompendiumFlags[Data::CompendiumFlagsCount - 1]);
    assert(Progress::Compendium(IsSet).done == 2);

    // Every flag read above is in the list the save reader keeps, once and in order.
    const std::vector<uint32_t>& tracked = Progress::TrackedFlags();
    assert(tracked.size() == 2u * 152u + 4u + 35u + 7u + static_cast<std::size_t>(Data::CompendiumFlagsCount));
    for (std::size_t i = 1; i < tracked.size(); ++i)
        assert(tracked[i - 1] < tracked[i]);
    for (uint32_t flag : setFlags)
        assert(std::set<uint32_t>(tracked.begin(), tracked.end()).count(flag) == 1);
    return 0;
}
