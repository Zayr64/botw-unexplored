#pragma once

#include <cstddef>

namespace LegendCategories
{
    enum ButtonTypes
    {
        Koroks = 0,
        Shrines,
        Hinoxes,
        Taluses,
        Moldugas,
        Locations,      // the named places not visited yet, the third part of the map completion
        ArmorChests,    // the special chests, in three groups (Chests.h)
        WeaponChests,
        MaterialChests,
        Walls,
        Quests,   // where the side and shrine quests not taken yet are given (QuestMap.h)
        HeroPath, // not objects: the line of where Link has walked (HeroPath.h)
        Count
    };

    constexpr size_t ButtonCount = static_cast<size_t>(ButtonTypes::Count);
}
