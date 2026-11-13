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
    using Data::CompendiumCategory;
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

    // The shrine and side quests of the base game that are not in the log: 119, of which 34 wait for a flag.
    // All but one are taken in a known region.
    Progress::QuestsLeft left = Progress::QuestsToTake(-1, IsSet);
    assert(left.now == 85 && left.later == 34);
    int inRegions = 0;
    for (int region = 0; region < 15; ++region)
    {
        const Progress::QuestsLeft here = Progress::QuestsToTake(region, IsSet);
        inRegions += here.now + here.later;
    }
    assert(inRegions == 118);
    // The region of the Great Plateau has none.
    assert(Progress::QuestsToTake(6, IsSet).now == 0 && Progress::QuestsToTake(6, IsSet).later == 0);
    const Progress::QuestsLeft duelingPeaks = Progress::QuestsToTake(7, IsSet);
    assert(duelingPeaks.now + duelingPeaks.later == 13);

    // Find_Impa_Activated: the quest is taken. With Find_Impa_Finish it is done, and no longer open.
    setFlags.insert(528446742u);
    main = Progress::Quests(QuestKind::Main, false, IsSet);
    assert(main.done == 0 && main.open == 1);
    setFlags.insert(1144992850u);
    main = Progress::Quests(QuestKind::Main, false, IsSet);
    assert(main.done == 1 && main.open == 0);
    // Find_Impa_Finish is also what two side quests of that region wait for: they can be taken now.
    left = Progress::QuestsToTake(-1, IsSet);
    assert(left.now == 87 && left.later == 32);
    assert(Progress::QuestsToTake(7, IsSet).now == duelingPeaks.now + 2);
    // Kakariko_Cha_003_Activated: one of the two is in the log, and no longer to be taken; the same when
    // only its last flag is set (Kakariko_Cha_003_Finish).
    setFlags.insert(2535558755u);
    assert(Progress::QuestsToTake(-1, IsSet).now == 86 && Progress::QuestsToTake(7, IsSet).now == duelingPeaks.now + 1);
    setFlags.erase(2535558755u);
    setFlags.insert(2754433291u);
    assert(Progress::QuestsToTake(-1, IsSet).now == 86 && Progress::QuestsToTake(-1, IsSet).later == 32);
    // IsGet_Obj_Camera lets five more be taken, in five regions.
    setFlags.insert(4158471683u);
    assert(Progress::QuestsToTake(-1, IsSet).now == 91 && Progress::QuestsToTake(-1, IsSet).later == 27);

    // FirstTower_Finish without FirstTower_Activated still counts as done.
    setFlags.insert(3326169616u);
    main = Progress::Quests(QuestKind::Main, false, IsSet);
    assert(main.done == 2 && main.open == 0);
    assert(Progress::Quests(QuestKind::Side, false, IsSet).done == 1);

    // Clear_RemainsFire: one of the four Divine Beasts.
    assert(Progress::DivineBeasts(IsSet).total == 4 && Progress::DivineBeasts(IsSet).done == 0);
    setFlags.insert(1074258414u);
    assert(Progress::DivineBeasts(IsSet).done == 1);

    assert(Progress::DivineBeastChests(IsSet).total == 35 && Progress::FinalTrialChests(IsSet).total == 7);
    setFlags.insert(Data::DivineBeastChestFlags[3]);
    assert(Progress::DivineBeastChests(IsSet).done == 1 && Progress::FinalTrialChests(IsSet).done == 0);

    // The compendium: 385 entries, 389 with the Champions' Ballad, 5 more in Master Mode.
    Progress::Game base, pass, master;
    pass.expansionPass = true;
    master.expansionPass = true;
    master.masterMode = true;
    assert(Data::CompendiumEntriesCount == 394);
    assert(Progress::Compendium(IsSet, base).total == 385 && Progress::Compendium(IsSet, pass).total == 389);
    assert(Progress::Compendium(IsSet, master).total == 394 && Progress::Compendium(IsSet, pass).done == 0);
    const int withPass[] = {83, 81, 36, 185, 4};
    const int withoutPass[] = {83, 78, 36, 184, 4};
    int sum = 0;
    for (int category = 0; category < static_cast<int>(CompendiumCategory::Count); ++category)
    {
        assert(Progress::Compendium(static_cast<CompendiumCategory>(category), IsSet, pass).total == withPass[category]);
        assert(Progress::Compendium(static_cast<CompendiumCategory>(category), IsSet, base).total == withoutPass[category]);
        sum += withPass[category];
    }
    assert(sum == 389 && Progress::Compendium(CompendiumCategory::Monsters, IsSet, master).total == 86);
    // IsRegisteredPictureBook_Animal_Insect_F is a creature and ..._Obj_Mineral_C_01 a treasure.
    setFlags.insert(4116346506u);
    setFlags.insert(4093504545u);
    assert(Progress::Compendium(IsSet, base).done == 2);
    assert(Progress::Compendium(CompendiumCategory::Creatures, IsSet, base).done == 1);
    assert(Progress::Compendium(CompendiumCategory::Treasure, IsSet, base).done == 1);
    assert(Progress::Compendium(CompendiumCategory::Monsters, IsSet, base).done == 0);
    // ..._Weapon_Sword_502 comes with the Champions' Ballad and ..._Enemy_Lynel_Gold with Master Mode: a game
    // without them does not count their flags.
    setFlags.insert(279023443u);
    setFlags.insert(3707451460u);
    assert(Progress::Compendium(IsSet, base).done == 2 && Progress::Compendium(IsSet, pass).done == 3);
    assert(Progress::Compendium(IsSet, master).done == 4);
    assert(Progress::Compendium(CompendiumCategory::Equipment, IsSet, pass).done == 1);
    assert(Progress::Compendium(CompendiumCategory::Monsters, IsSet, master).done == 1);

    // Armor: 65 pieces, 84 with the Expansion Pass; those of amiibo are not in the table.
    assert(Data::ArmorPiecesCount == 84);
    assert(Progress::Armor(IsSet, base).total == 65 && Progress::Armor(IsSet, pass).total == 84);
    assert(Progress::Armor(IsSet, pass).done == 0);
    // IsGet_Armor_001_Head and IsGet_Armor_002_Head are two levels of the same piece.
    setFlags.insert(1181818774u);
    assert(Progress::Armor(IsSet, base).done == 1);
    setFlags.insert(3236261176u);
    assert(Progress::Armor(IsSet, base).done == 1);
    // IsGet_Armor_141_Lower and IsGet_Armor_140_Lower: the two actors of the same boots.
    setFlags.insert(2107574893u);
    setFlags.insert(3689415129u);
    assert(Progress::Armor(IsSet, base).done == 2);
    // IsGet_Armor_170_Upper is of the Expansion Pass; IsGet_Armor_200_Head only amiibo give.
    setFlags.insert(899850885u);
    setFlags.insert(3473460046u);
    assert(Progress::Armor(IsSet, base).done == 2 && Progress::Armor(IsSet, pass).done == 3);

    // Memories: nothing to count before the album has pictures; then as many as it has.
    assert(Data::MemoryFlagsCount == 13 && Data::MemoryPhotoFlagsCount == 14);
    assert(Progress::Memories(IsSet).total == 0 && Progress::Memories(IsSet).done == 0);
    for (int i = 0; i < 12; ++i)
        setFlags.insert(Data::MemoryPhotoFlags[i]);
    assert(Progress::Memories(IsSet).total == 12 && Progress::Memories(IsSet).done == 0);
    // IsPlayed_Demo126_0: the first memory spot.
    setFlags.insert(481298551u);
    assert(Progress::Memories(IsSet).total == 12 && Progress::Memories(IsSet).done == 1);
    // All thirteen recalled: one more than the pictures told of, and never more than thirteen.
    for (int i = 0; i < Data::MemoryFlagsCount; ++i)
        setFlags.insert(Data::MemoryFlags[i]);
    assert(Progress::Memories(IsSet).total == 13 && Progress::Memories(IsSet).done == 13);
    setFlags.insert(Data::MemoryPhotoFlags[12]);
    setFlags.insert(Data::MemoryPhotoFlags[13]);
    assert(Progress::Memories(IsSet).total == 13);

    // Every flag read above is in the list the save reader keeps, once and in order.
    const std::vector<uint32_t>& tracked = Progress::TrackedFlags();
    for (std::size_t i = 1; i < tracked.size(); ++i)
        assert(tracked[i - 1] < tracked[i]);
    const std::set<uint32_t> kept(tracked.begin(), tracked.end());
    for (uint32_t flag : setFlags)
        assert(kept.count(flag) == 1 || flag == 3473460046u); // all but the amiibo piece
    assert(kept.count(0u) == 0);
    return 0;
}
