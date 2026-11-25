#pragma once

#include <cstdint>

#include "Cook.h"
#include "CookData.h"

// The best dishes that can be cooked from what the player has (or from what is free after the reserve).
//
// For one effect, only three kinds of ingredient can help: those with that effect (another effect cancels both),
// those with no effect that add time, hearts or a sure critical (spices, monster parts for elixirs, plain food), and
// nothing else. So the search takes every ingredient with the effect plus the few best helpers, and cooks every
// combination of up to five of them with the game's own rules (Cook). Tens of thousands of combinations per effect.
namespace Compendium
{
    struct StockItem
    {
        uint32_t hash = 0;
        int32_t count = 0;
    };

    struct FoundDish
    {
        uint32_t items[5] = {};
        int itemCount = 0;
        CookResult result;
        int32_t cost = 0; // selling price of the ingredients used: what the dish costs in rupees not made
    };

    struct SearchResult
    {
        bool found = false;
        FoundDish best;     // strongest effect, then longest, then the cheapest (or the most hearts: Ranking)
        FoundDish cheapest; // the cheapest dish as strong as `best` (same level, yellow hearts or stamina)
        int evaluated = 0;  // combinations cooked
    };

    struct TargetResult
    {
        bool found = false;
        bool reached = false; // false: nothing reaches the target and `dish` is the strongest there is
        FoundDish dish;
        int evaluated = 0;
    };

    // How the best dish of an effect is chosen among those as strong and as long.
    enum class Ranking
    {
        PriceThenHearts, // the cheaper first, hearts only between equals: no costly food just for hearts (0.1.9 on)
        HeartsThenPrice, // more hearts first, the price only between equals (0.1.0 to 0.1.8)
    };

    // Dishes with `effect` (LifeMaxUp for yellow hearts, GutsRecover, ExGutsMaxUp, or a timed effect).
    SearchResult FindBestForEffect(const CookData& data, const StockItem* stock, int stockCount, Effect effect,
                                   Ranking ranking = Ranking::PriceThenHearts);

    // The dish that restores the most hearts (not counting yellow-heart dishes, which restore all).
    SearchResult FindMostHearts(const CookData& data, const StockItem* stock, int stockCount);

    // The cheapest dish that is at least `minimum` strong, so nothing is wasted: EffectNone for hearts (in quarters,
    // yellow-heart dishes left out), GutsRecover for stamina (1000 = one wheel), LifeMaxUp for yellow hearts (in
    // quarters; any of them also restores every heart). Among the cheapest, the one going least over the target.
    TargetResult FindCheapestReaching(const CookData& data, const StockItem* stock, int stockCount, Effect goal,
                                      int32_t minimum);
}
