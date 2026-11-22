#pragma once

#include <cstdint>

#include "Cook.h"
#include "CookData.h"
#include "Search.h"

// What to do with free materials to get the most rupees.
//
// A cooked dish sells for the sum of its ingredients' prices times 1.5 to 2.8 (one to five items), rounded up to ten
// and capped at what they cost to buy. Five items is always the best multiplier, but monster parts only make a dish
// (an elixir) with a critter, so the critters set how many monster parts can be cooked. Ore, dragon parts and star
// fragments are worth 2 or 1 in a pot: they are sold as they are.
namespace Cozinha
{
    constexpr int MaxSalePots = 60;   // the pouch holds 60 dishes
    constexpr int MaxRawSales = 64;

    struct SalePot
    {
        uint32_t items[5] = {};
        int itemCount = 0;
        int32_t price = 0;    // the dish
        int32_t rawPrice = 0; // the same items sold as they are
    };

    struct RawSale
    {
        uint32_t item = 0;
        int32_t count = 0;
        int32_t price = 0;    // count x selling price
    };

    struct SalePlan
    {
        int potCount = 0;
        SalePot pots[MaxSalePots];
        int32_t cookedTotal = 0;
        int32_t cookedRawTotal = 0;
        int partsLeft = 0;          // free monster parts with no critter to cook them with
        int32_t partsLeftRaw = 0;
        int rawCount = 0;
        RawSale raw[MaxRawSales];   // free items best sold as they are, most valuable first
        int32_t rawTotal = 0;
    };

    // `free`: what is left after the reserve. `potLimit`: dishes the pouch can still take.
    void PlanSale(const CookData& data, const StockItem* free, int freeCount, int potLimit, SalePlan& out);
}
