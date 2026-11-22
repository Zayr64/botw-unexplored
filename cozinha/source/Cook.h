#pragma once

#include <cstddef>
#include <cstdint>

#include "CookData.h"

// The game's cooking, step by step as in the decomp (uking::CookingMgr::cook, zeldaret/botw), with the same float32
// arithmetic. Build with -ffp-contract=off: a fused multiply-add rounds differently from the game.
//
// The random parts (critical success, Monster Extract) are not rolled here: the result is the dish without them, plus
// the chance of a critical success and which bonuses it could give, so the caller can show or compute each one.
namespace Cozinha
{
    enum class CritBonus : uint8_t
    {
        None = 0,
        Life = 1,      // +3 hearts (SSA of LifeRecover)
        Vitality = 2,  // +1 level, +1 yellow heart, more stamina
        Time = 4,      // +5:00
    };

    struct CookResult
    {
        uint32_t actor = 0;        // the dish (an Item_Cook_* actor)
        int32_t life = 0;          // quarter hearts; for LifeMaxUp, the yellow quarter hearts (and full recovery)
        Effect effect = EffectNone;
        // LifeMaxUp: yellow quarter hearts. GutsRecover: stamina, 1000 = one wheel. ExGutsMaxUp: fifths of a wheel.
        // Timed effects: the level (1 to 3).
        int32_t vitality = 0;
        int32_t time = 0;          // seconds, only for timed effects
        int32_t price = 0;         // selling price in rupees
        int32_t potency = 0;       // sum of effectLevel x count of the effect's ingredients (the "potency" in guides)
        bool failure = false;      // Dubious Food or Rock-Hard Food
        bool monsterExtract = false; // the result is random; this is the dish before Monster Extract
        bool crit = false;         // a critical bonus was applied (only when one is asked for)
        int32_t critChance = 0;    // percent; 100 or more is certain
        uint8_t critBonuses = 0;   // CritBonus values a critical success can give (one is picked at random)
    };

    // `items`: actor name hashes in the order they go into the pot, 1 to 5, repeats allowed.
    // `bonus`: CritBonus::None for the normal dish; otherwise that critical bonus, if it is one the dish can get.
    CookResult Cook(const CookData& data, const uint32_t* items, int count, CritBonus bonus = CritBonus::None);

    // The name the game shows: the effect's word, if any, before the dish's name ("Chilly Elixir").
    void DishName(const CookData& data, const CookResult& result, char* out, size_t size);
}
