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
    using Data::ChestContents;
    using Data::ChestGroup;

    // Every chest has its own flag, a kind of content and an item with a name.
    std::set<uint32_t> hashes;
    int masterModeOnly = 0, dlc = 0;
    // group: that of the table, or Count for the table of the ordinary chests
    const auto check = [&](const Data::Chest* chests, int count, ChestGroup group)
    {
        int inNormalMode = 0;
        for (int i = 0; i < count; ++i)
        {
            const Data::Chest& chest = chests[i];
            assert(chest.hash != 0 && hashes.insert(chest.hash).second);
            assert(i == 0 || chests[i - 1].hash < chest.hash);
            assert(chest.contents < static_cast<uint8_t>(ChestContents::Count));
            assert(chest.item < Data::ChestItemNamesCount && ItemOf(chest)[0] != 0);
            // Inside the map: about -5000..5000 by -4000..4000
            assert(chest.x > -5100.0f && chest.x < 5100.0f && chest.y > -4100.0f && chest.y < 4100.0f);
            masterModeOnly += (chest.flags & Data::ChestMasterModeOnly) != 0;
            dlc += (chest.flags & Data::ChestDlc) != 0;
            inNormalMode += (chest.flags & Data::ChestMasterModeOnly) == 0;

            const ChestContents contents = static_cast<ChestContents>(chest.contents);
            const bool kept = contents == ChestContents::Armor || contents == ChestContents::KeyItem ||
                              contents == ChestContents::HorseGear;
            const bool gear = contents == ChestContents::Weapon || contents == ChestContents::Bow ||
                              contents == ChestContents::Shield;
            if (group == ChestGroup::Count)
                // Armor, key items and horse gear are never in an ordinary chest.
                assert(!kept);
            else
            {
                // Each group has its kinds of content, and the kind tells the group; arrows, gems and rupees
                // are in none.
                assert(Data::GroupOf(chest.contents) == group);
                assert(group == ChestGroup::Armor ? kept
                       : (group == ChestGroup::Weapon ? gear : contents == ChestContents::Material));
            }
        }
        return inNormalMode;
    };
    assert(check(Data::Chests, Data::ChestsCount, ChestGroup::Count) == 1104);
    assert(check(Data::ArmorChests, Data::ArmorChestsCount, ChestGroup::Armor) == 36);
    assert(check(Data::WeaponChests, Data::WeaponChestsCount, ChestGroup::Weapon) == 44);
    assert(check(Data::MaterialChests, Data::MaterialChestsCount, ChestGroup::Material) == 10);
    assert(Data::ChestsCount + Data::ArmorChestsCount + Data::WeaponChestsCount + Data::MaterialChestsCount == 1312);
    assert(masterModeOnly == 118 && dlc == 24);

    // Each group has an image of its own.
    assert(std::strcmp(Data::IconOf(ChestGroup::Armor), "romfs:/chestarmor.png") == 0);
    assert(std::strcmp(Data::IconOf(ChestGroup::Weapon), "romfs:/chestweapon.png") == 0);
    assert(std::strcmp(Data::IconOf(ChestGroup::Material), "romfs:/chestmaterial.png") == 0);

    // Three chests by the name of their flag in the save (its CRC32).
    const Data::Chest* shield = Find(Data::WeaponChests, Data::WeaponChestsCount, 2175256561u); // MainField_TBox_Field_Iron_628080506
    assert(shield != nullptr && std::strcmp(ItemOf(*shield), "Hylian Shield") == 0 && shield->flags == 0);
    assert(static_cast<ChestContents>(shield->contents) == ChestContents::Shield);
    const Data::Chest* mask = Find(Data::ArmorChests, Data::ArmorChestsCount, 2124646826u);
    assert(mask != nullptr && std::strcmp(ItemOf(*mask), "Majora's Mask") == 0 && mask->flags == Data::ChestDlc);
    const Data::Chest* vest = Find(Data::ArmorChests, Data::ArmorChestsCount, 1013528320u);
    assert(vest != nullptr && std::strcmp(ItemOf(*vest), "Salvager Vest") == 0 && vest->flags == Data::ChestSideQuest);
    assert(Find(Data::Chests, Data::ChestsCount, 2175256561u) == nullptr);
    assert(Find(Data::ArmorChests, Data::ArmorChestsCount, 2175256561u) == nullptr);
    return 0;
}
