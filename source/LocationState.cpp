#include "LocationState.h"

#include <array>

#include "ShrineFlags.h"

namespace
{
    struct Names
    {
        const char* location;
        const char* flag;
        LocationState::Kind kind;
    };

    using LocationState::Kind;

    const Names names[LocationState::Count] = {
        {"Location_MapTower01", "MapTower_01", Kind::Tower},
        {"Location_MapTower02", "MapTower_02", Kind::Tower},
        {"Location_MapTower03", "MapTower_03", Kind::Tower},
        {"Location_MapTower04", "MapTower_04", Kind::Tower},
        {"Location_MapTower05", "MapTower_05", Kind::Tower},
        {"Location_MapTower06", "MapTower_06", Kind::Tower},
        {"Location_MapTower07", "MapTower_07", Kind::Tower},
        {"Location_MapTower08", "MapTower_08", Kind::Tower},
        {"Location_MapTower09", "MapTower_09", Kind::Tower},
        {"Location_MapTower10", "MapTower_10", Kind::Tower},
        {"Location_MapTower11", "MapTower_11", Kind::Tower},
        {"Location_MapTower12", "MapTower_12", Kind::Tower},
        {"Location_MapTower13", "MapTower_13", Kind::Tower},
        {"Location_MapTower14", "MapTower_14", Kind::Tower},
        {"Location_MapTower15", "MapTower_15", Kind::Tower},
        {"Location_WeaponCureSpring", "RevivalGreatFairy1", Kind::GreatFairy},
        {"Location_WeaponCureSpring_02", "RevivalGreatFairy2", Kind::GreatFairy},
        {"Location_WeaponCureSpring_03", "RevivalGreatFairy3", Kind::GreatFairy},
        {"Location_WeaponCureSpring_04", "RevivalGreatFairy4", Kind::GreatFairy},
        {"Location_MaronSpring", "RevivalGreatFairy5", Kind::GreatFairy},
    };
}

const LocationState::Entry* LocationState::Table()
{
    static const std::array<Entry, Count> table = []()
    {
        std::array<Entry, Count> result;
        for (int i = 0; i < Count; ++i)
            result[i] = {ShrineFlags::Crc32(names[i].location), ShrineFlags::Crc32(names[i].flag), names[i].kind};
        return result;
    }();
    return table.data();
}

const LocationState::Entry* LocationState::ForLocation(uint32_t locationHash)
{
    const Entry* table = Table();
    for (int i = 0; i < Count; ++i)
        if (table[i].locationHash == locationHash)
            return &table[i];
    return nullptr;
}

const LocationState::Entry* LocationState::ForFlag(uint32_t flagHash)
{
    const Entry* table = Table();
    for (int i = 0; i < Count; ++i)
        if (table[i].flagHash == flagHash)
            return &table[i];
    return nullptr;
}
