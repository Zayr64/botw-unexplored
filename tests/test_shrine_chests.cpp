#include <cassert>
#include <set>

#include "ShrineChests.h"

int main()
{
    // 205 chests in the 120 shrines and the 16 of the Champions' Ballad; every shrine has at least one.
    assert(Data::ShrineChestListsCount == 136);
    assert(Data::ShrineChestFlagsCount == 205);
    std::set<uint32_t> flags;
    int next = 0;
    for (int i = 0; i < Data::ShrineChestListsCount; ++i)
    {
        const Data::ShrineChestList& list = Data::ShrineChestLists[i];
        assert(i == 0 || Data::ShrineChestLists[i - 1].locationHash < list.locationHash);
        assert(list.first == next && list.count >= 1 && list.count <= 6);
        next += list.count;
        for (int chest = 0; chest < list.count; ++chest)
            assert(flags.insert(Data::ShrineChestFlags[list.first + chest]).second);
        assert(ShrineChests::Find(list.locationHash) == &list);
    }
    assert(next == Data::ShrineChestFlagsCount);

    // Values from the hash list of marcrobledo/savegame-editors (zelda-botw.hashes.csv).
    // Location_Dungeon003 has CDungeon_TBox_Dungeon_Stone_638977289, ..._Stone_2441275272 and ..._Iron_3284159718.
    const Data::ShrineChestList* three = ShrineChests::Find(107786799u);
    assert(three != nullptr && three->count == 3);
    assert(flags.count(474408737u) == 1 && flags.count(23363453u) == 1 && flags.count(3616764300u) == 1);
    const uint32_t none[] = {1u};
    const uint32_t two[] = {23363453u, 3196837937u, 3616764300u}; // sorted; the middle one is of another shrine
    const uint32_t all[] = {23363453u, 474408737u, 3616764300u};
    assert(ShrineChests::CountOpened(*three, none, 0) == 0);
    assert(ShrineChests::CountOpened(*three, none, 1) == 0);
    assert(ShrineChests::CountOpened(*three, two, 3) == 2);
    assert(ShrineChests::CountOpened(*three, all, 3) == 3);

    // Location_Dungeon038 has one chest, CDungeon_TBox_Dungeon_Iron_3375369818.
    const Data::ShrineChestList* one = ShrineChests::Find(3130210404u);
    assert(one != nullptr && one->count == 1 && Data::ShrineChestFlags[one->first] == 3196837937u);
    assert(ShrineChests::CountOpened(*one, two, 3) == 1);

    // Location_HyruleCastle is not a shrine, and the flag of a chest is not a shrine either.
    assert(ShrineChests::Find(3414442901u) == nullptr);
    assert(ShrineChests::Find(474408737u) == nullptr);
    return 0;
}
