#include <cassert>
#include <set>

#include "LocationState.h"

int main()
{
    using LocationState::Kind;

    // Twenty locations, each with its own state flag.
    const LocationState::Entry* table = LocationState::Table();
    std::set<uint32_t> locations, flags;
    int towers = 0;
    for (int i = 0; i < LocationState::Count; ++i)
    {
        assert(locations.insert(table[i].locationHash).second && flags.insert(table[i].flagHash).second);
        assert(locations.count(table[i].flagHash) == 0);
        towers += table[i].kind == Kind::Tower;
    }
    assert(towers == 15);

    // Values from the hash list of marcrobledo/savegame-editors (zelda-botw.hashes.csv).
    // Location_MapTower07 (the Great Plateau Tower) and MapTower_07.
    const LocationState::Entry* plateau = LocationState::ForLocation(2845242333u);
    assert(plateau != nullptr && plateau->kind == Kind::Tower && plateau->flagHash == 2459426701u);
    assert(LocationState::ForFlag(2459426701u) == plateau);
    // Location_MapTower15 and MapTower_15.
    const LocationState::Entry* last = LocationState::ForLocation(1585685424u);
    assert(last != nullptr && last->kind == Kind::Tower && last->flagHash == 1703055328u);

    // Location_WeaponCureSpring and RevivalGreatFairy1; Location_WeaponCureSpring_04 and RevivalGreatFairy4.
    const LocationState::Entry* fountain = LocationState::ForLocation(3218941954u);
    assert(fountain != nullptr && fountain->kind == Kind::GreatFairy && fountain->flagHash == 4193769829u);
    const LocationState::Entry* fourth = LocationState::ForLocation(3236472523u);
    assert(fourth != nullptr && fourth->kind == Kind::GreatFairy && fourth->flagHash == 2308779498u);
    // Location_MaronSpring and RevivalGreatFairy5.
    const LocationState::Entry* spring = LocationState::ForFlag(4271512956u);
    assert(spring != nullptr && spring->kind == Kind::GreatFairy && spring->locationHash == 2500911877u);

    // Location_HyruleCastle is only visited; the flag of a location is not a state flag, nor the other way.
    assert(LocationState::ForLocation(3414442901u) == nullptr);
    assert(LocationState::ForFlag(2845242333u) == nullptr);
    assert(LocationState::ForLocation(2459426701u) == nullptr);
    return 0;
}
