#include <cassert>
#include <cstring>
#include <set>

#include "Chests.h"

namespace
{
    const Data::Chest* Find(const Data::Chest* chests, int count, uint32_t hash)
    {
        for (int i = 0; i < count; ++i)
            if (chests[i].hash == hash)
                return &chests[i];
        return nullptr;
    }

    const char* ItemOf(const Data::Chest& chest)
    {
        return Data::ChestItemNames[chest.item];
    }
}

int main()
{
    // Every chest has its own flag, a kind of content and an item with a name.
    std::set<uint32_t> hashes;
    int masterModeOnly = 0, dlc = 0;
    const auto check = [&](const Data::Chest* chests, int count, bool special)
    {
        for (int i = 0; i < count; ++i)
        {
            const Data::Chest& chest = chests[i];
            assert(chest.hash != 0 && hashes.insert(chest.hash).second);
            assert(i == 0 || chests[i - 1].hash < chest.hash);
            assert(chest.contents < static_cast<uint8_t>(Data::ChestContents::Count));
            assert(chest.item < Data::ChestItemNamesCount && ItemOf(chest)[0] != 0);
            // Inside the map: about -5000..5000 by -4000..4000
            assert(chest.x > -5100.0f && chest.x < 5100.0f && chest.y > -4100.0f && chest.y < 4100.0f);
            masterModeOnly += (chest.flags & Data::ChestMasterModeOnly) != 0;
            dlc += (chest.flags & Data::ChestDlc) != 0;

            const Data::ChestContents contents = static_cast<Data::ChestContents>(chest.contents);
            const bool alwaysSpecial = contents == Data::ChestContents::Armor || contents == Data::ChestContents::KeyItem ||
                                       contents == Data::ChestContents::HorseGear;
            const bool whenRare = contents == Data::ChestContents::Weapon || contents == Data::ChestContents::Bow ||
                                  contents == Data::ChestContents::Shield || contents == Data::ChestContents::Material;
            // Armor, key items and horse gear are always special; arrows, gems and rupees never are.
            assert(!alwaysSpecial || special);
            assert(!special || alwaysSpecial || whenRare);
        }
    };
    check(Data::Chests, Data::ChestsCount, false);
    check(Data::SpecialChests, Data::SpecialChestsCount, true);
    assert(Data::ChestsCount + Data::SpecialChestsCount == 1312);
    assert(masterModeOnly == 118 && dlc == 24);

    // Three chests by the name of their flag in the save (its CRC32).
    const Data::Chest* shield = Find(Data::SpecialChests, Data::SpecialChestsCount, 2175256561u); // MainField_TBox_Field_Iron_628080506
    assert(shield != nullptr && std::strcmp(ItemOf(*shield), "Hylian Shield") == 0 && shield->flags == 0);
    assert(static_cast<Data::ChestContents>(shield->contents) == Data::ChestContents::Shield);
    const Data::Chest* mask = Find(Data::SpecialChests, Data::SpecialChestsCount, 2124646826u);
    assert(mask != nullptr && std::strcmp(ItemOf(*mask), "Majora's Mask") == 0 && mask->flags == Data::ChestDlc);
    const Data::Chest* vest = Find(Data::SpecialChests, Data::SpecialChestsCount, 1013528320u);
    assert(vest != nullptr && std::strcmp(ItemOf(*vest), "Salvager Vest") == 0 && vest->flags == Data::ChestSideQuest);
    assert(Find(Data::Chests, Data::ChestsCount, 2175256561u) == nullptr);
    return 0;
}
