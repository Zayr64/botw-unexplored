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
        ArmorChests,    // the special chests, in three groups (Chests.h)
        WeaponChests,
        MaterialChests,
        Walls,
        Count
    };

    constexpr size_t ButtonCount = static_cast<size_t>(ButtonTypes::Count);
}
