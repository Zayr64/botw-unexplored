#include "Cook.h"

#include <cstdio>

// Follows uking::CookingMgr (zeldaret/botw, src/Game/Cooking/cookManager.cpp) function by function; the comments name
// the function each part comes from. Keep the order of the steps: it changes the results.

namespace Compendium
{
    namespace
    {
        constexpr int MaxIngredients = 5;

        struct Ingredient
        {
            uint32_t hash = 0;
            const Actor* actor = nullptr;
            int count = 0;
            bool usedInRecipe = false;
        };

        // CookItem, the dish being made.
        struct Item
        {
            uint32_t actor = 0;
            float life = 0.0f;
            int32_t time = 0;
            int32_t price = 0;
            Effect effect = EffectNone;
            float vitality = 0.0f;
            bool crit = false;
        };

        bool HasTag(const CookData& data, uint32_t actor, uint32_t tag)
        {
            const Actor* a = data.FindActor(actor);
            return a && a->HasTag(tag);
        }

        const EffectEntry& Entry(const CookData& data, Effect effect)
        {
            return data.effects[static_cast<int>(effect)];
        }

        // cookFail: Dubious Food keeps its hearts (at least FALR), Rock-Hard Food gets SFALR. Both sell for 2.
        void CookFail(const CookData& data, Item& item)
        {
            if (item.actor == 0)
                item.actor = data.failActor;
            float life;
            if (item.actor == data.failActor)
            {
                item.time = 0;
                const float minRecovery = static_cast<float>(data.failActorLifeRecover);
                life = minRecovery > item.life ? minRecovery : item.life;
            }
            else
            {
                item.time = 0;
                life = static_cast<float>(data.stoneFoodLifeRecover);
            }
            item.life = life;
            item.vitality = 0.0f;
            item.effect = EffectNone;
            item.price = 2;
        }

        // cookCalcIngredientsBoost
        void CalcIngredientsBoost(const CookData& data, const Ingredient* ingredients, int count, Item& item,
                                  int32_t& potency)
        {
            const bool isMedicine = HasTag(data, item.actor, Tags::CookEMedicine());
            const bool isNotFairyTonic = data.fairyTonic != item.actor;

            int32_t effectCounts[EffectSlots] = {};
            int32_t cureLevels[EffectSlots] = {};
            int32_t staminaBoost = 0;
            int32_t lifeBoost = 0;
            int32_t timeBoost = 0;
            int32_t totalCount = 0;
            int32_t lifeRecover = 0;

            for (int i = 0; i < count; i++)
            {
                const Actor& a = *ingredients[i].actor;
                const int32_t n = ingredients[i].count;
                totalCount += n;
                if (a.HasTag(Tags::CookEnemy()))
                {
                    if (a.spiceTime > 0)
                        timeBoost += a.spiceTime * n;
                    if (a.spiceMaxHeart > 0)
                        lifeBoost += a.spiceMaxHeart * n;
                    if (a.spiceStamina > 0)
                        staminaBoost += a.spiceStamina * n;
                }
                else
                {
                    if (a.hitPointRecover > 0)
                        lifeRecover += a.hitPointRecover * n;
                    if (a.effectLevel > 0 && a.effectType != EffectNone && a.effectType >= 0 &&
                        a.effectType < EffectSlots)
                    {
                        effectCounts[a.effectType] += n;
                        cureLevels[a.effectType] += a.effectLevel * n;
                    }
                }
            }

            bool effectFound = false;
            for (int i = 0; i < EffectSlots; i++)
            {
                const int32_t effectCount = effectCounts[i];
                if (effectCount <= 0)
                    continue;
                if (effectFound)
                {
                    // A second effect cancels both.
                    effectFound = false;
                    item.vitality = 0.0f;
                    item.effect = EffectNone;
                    item.time = 0;
                    potency = 0;
                    break;
                }
                const EffectEntry& entry = data.effects[i];
                item.vitality = static_cast<float>(cureLevels[i]) * entry.multiplier;
                item.effect = static_cast<Effect>(i);
                potency = cureLevels[i];
                if (entry.boostTime > 0)
                    item.time = timeBoost + 30 * totalCount + entry.boostTime * effectCount;
                if (item.effect == LifeMaxUp)
                    item.vitality += static_cast<float>(lifeBoost);
                else if (item.effect == GutsRecover || item.effect == ExGutsMaxUp)
                    item.vitality += static_cast<float>(staminaBoost);
                effectFound = true;
            }

            if (!isNotFairyTonic && effectFound)
            {
                effectFound = false;
                item.vitality = 0.0f;
                item.effect = EffectNone;
                item.time = 0;
            }

            if (isMedicine && !effectFound)
                item.actor = data.failActor;

            if (HasTag(data, item.actor, Tags::CookFailure()))
                item.life = static_cast<float>(lifeRecover) * data.failActorLifeRecoverMultiplier;
            else if (effectFound)
                item.life = static_cast<float>(lifeRecover) * data.lifeRecoverMultiplier;
            else
                item.life = static_cast<float>(lifeRecover) * Entry(data, LifeRecover).multiplier;

            if (item.effect != EffectNone)
            {
                const int32_t max = Entry(data, item.effect).max;
                if (item.vitality > static_cast<float>(max))
                    item.vitality = static_cast<float>(max);
            }
        }

        // The choice in cookHandleCrit, as the set of bonuses the random pick can land on.
        uint8_t CritOptions(const CookData& data, const Item& item)
        {
            const uint8_t life = static_cast<uint8_t>(CritBonus::Life);
            const uint8_t vitality = static_cast<uint8_t>(CritBonus::Vitality);
            const uint8_t time = static_cast<uint8_t>(CritBonus::Time);
            if (item.effect == EffectNone)
                return life;

            const int32_t vitalityBonus = item.vitality >= 1.0f ? static_cast<int32_t>(item.vitality) : 1;
            const bool lifeMaxed = item.life >= static_cast<float>(Entry(data, LifeRecover).max);
            const bool vitalityMaxed = vitalityBonus >= Entry(data, item.effect).max;
            switch (item.effect)
            {
            case LifeMaxUp:
                return vitality;
            case GutsRecover:
            case ExGutsMaxUp:
                if (vitalityMaxed)
                    return life;
                if (lifeMaxed)
                    return vitality;
                return life | vitality;
            default:
                if (vitalityMaxed)
                    return lifeMaxed ? time : static_cast<uint8_t>(time | life);
                return lifeMaxed ? static_cast<uint8_t>(time | vitality) : static_cast<uint8_t>(life | vitality | time);
            }
        }

        // cookHandleCrit, with the bonus already picked.
        void ApplyCrit(const CookData& data, Item& item, CritBonus bonus)
        {
            item.crit = true;
            switch (bonus)
            {
            case CritBonus::Vitality:
                if (item.effect != EffectNone)
                {
                    if (item.vitality > 0.0f && item.vitality < 1.0f)
                        item.vitality = 1.0f;
                    item.vitality = static_cast<float>(static_cast<int32_t>(item.vitality) + Entry(data, item.effect).ssa);
                }
                break;
            case CritBonus::Time:
                item.time += data.critEffectTime;
                break;
            case CritBonus::Life:
                item.life += static_cast<float>(Entry(data, LifeRecover).ssa);
                break;
            case CritBonus::None:
                break;
            }
        }

        // cookCalcCritBoost, without the roll: fills the chance and the options, applies `bonus` when asked.
        void CalcCritBoost(const CookData& data, const Ingredient* ingredients, int count, Item& item, CritBonus bonus,
                           CookResult& result)
        {
            for (int i = 0; i < count; i++)
            {
                if (ingredients[i].hash == data.monsterExtract)
                {
                    // cookHandleMonsterExtract: hearts, effect and time are rolled instead.
                    result.monsterExtract = true;
                    return;
                }
            }

            int32_t threshold = 0;
            for (int i = 0; i < count; i++)
                if (ingredients[i].actor->spiceSuccessRate > threshold)
                    threshold = ingredients[i].actor->spiceSuccessRate;
            if (count >= 1)
                threshold += data.ingredientNumSuccessRates[count - 1];
            result.critChance = threshold;
            result.critBonuses = CritOptions(data, item);

            if (bonus != CritBonus::None && (result.critBonuses & static_cast<uint8_t>(bonus)) != 0)
                ApplyCrit(data, item, bonus);
        }

        // cookCalcSpiceBoost. The two inner loops change `i` like the game's code does (a bug that skips the next
        // ingredient); they only run for spices with these values above zero.
        void CalcSpiceBoost(const Ingredient* ingredients, int count, Item& item)
        {
            for (int i = 0; i < MaxIngredients; i++)
            {
                if (i >= count)
                    continue;
                const Actor& a = *ingredients[i].actor;
                if (a.HasTag(Tags::CookEnemy()) || !a.HasTag(Tags::CookSpice()))
                    continue;
                if (a.spiceHitPoint > 0)
                    item.life += static_cast<float>(a.spiceHitPoint);
                if (a.spiceTime > 0)
                    item.time += a.spiceTime;
                if (a.spiceMaxHeart > 0)
                {
                    for (; i < 1; i++)
                        if (item.effect == LifeMaxUp)
                            item.vitality += static_cast<float>(a.spiceMaxHeart);
                }
                if (a.spiceStamina > 0)
                {
                    for (; i < 1; i++)
                        if (item.effect == GutsRecover || item.effect == ExGutsMaxUp)
                            item.vitality += static_cast<float>(a.spiceStamina);
                }
            }
        }

        // cookCalcRecipeBoost
        void CalcRecipeBoost(bool hasHitPointBonus, int32_t hitPointBonus, bool hasTimeBonus, int32_t timeBonus,
                             Item& item)
        {
            if (hasHitPointBonus)
                item.life += static_cast<float>(hitPointBonus);
            if (hasTimeBonus && item.time > 0)
                item.time += timeBonus;
        }

        // cookAdjustItem
        void AdjustItem(const CookData& data, Item& item)
        {
            item.life = static_cast<float>(static_cast<int32_t>(item.life));
            const float lifeMax = static_cast<float>(Entry(data, LifeRecover).max);
            if (item.life > lifeMax)
                item.life = lifeMax;
            if (item.life < 0.0f)
                item.life = 0.0f;

            if (item.effect == EffectNone)
            {
                if (item.life == 0.0f)
                    item.life = 1.0f;
            }
            else
            {
                if (item.vitality > 0.0f && item.vitality < 1.0f)
                    item.vitality = 1.0f;
                int32_t vitality = static_cast<int32_t>(item.vitality);
                const int32_t vitalityMax = Entry(data, item.effect).max;
                if (vitality > vitalityMax)
                    vitality = vitalityMax;
                if (item.effect == GutsRecover)
                    vitality = vitality * 200;
                float vitalityF = item.vitality = static_cast<float>(vitality);
                if (item.effect == LifeMaxUp)
                {
                    if (static_cast<int32_t>(vitalityF) % 4 != 0)
                    {
                        vitalityF = static_cast<float>((static_cast<int32_t>(vitalityF) + 4) & ~3);
                        item.vitality = vitalityF;
                    }
                    if (vitalityF < 4.0f)
                    {
                        vitalityF = 4.0f;
                        item.vitality = 4.0f;
                    }
                    item.life = vitalityF;
                }
            }

            if (item.time < 0)
                item.time = 0;
            if (item.time > 1800)
                item.time = 1800;
        }

        // cookCalcItemPrice
        void CalcItemPrice(const CookData& data, const Ingredient* ingredients, int count, Item& item)
        {
            item.price = 0;
            if (data.fairyTonic == item.actor)
            {
                item.price = 2;
                return;
            }

            int32_t maxPrice = 0;
            int32_t multiplierIndex = 0;
            for (int i = 0; i < count; i++)
            {
                const Actor& a = *ingredients[i].actor;
                const int32_t n = ingredients[i].count;
                if (a.HasTag(Tags::CookLowPrice()))
                {
                    multiplierIndex += n;
                    item.price += n;
                    maxPrice += n;
                }
                else
                {
                    if (a.hasSellingPrice)
                    {
                        multiplierIndex += n;
                        item.price += a.sellingPrice * n;
                    }
                    if (a.hasBuyingPrice)
                        maxPrice += a.buyingPrice * n;
                }
            }

            if (multiplierIndex >= 1 && multiplierIndex <= 5)
                item.price = static_cast<int32_t>(data.ingredientNumMultipliers[multiplierIndex - 1] *
                                                  static_cast<float>(item.price));
            if (item.price >= 1 && item.price % 10 != 0)
                item.price = item.price + 10 - item.price % 10;
            if (maxPrice < item.price)
                item.price = maxPrice;
            if (item.price < 2)
                item.price = 2;
        }

        bool FindByName(Ingredient* ingredients, int count, uint32_t hash)
        {
            for (int i = 0; i < count; i++)
            {
                if (!ingredients[i].usedInRecipe && ingredients[i].hash == hash)
                {
                    ingredients[i].usedInRecipe = true;
                    return true;
                }
            }
            return false;
        }

        bool FindByTag(Ingredient* ingredients, int count, uint32_t tag)
        {
            for (int i = 0; i < count; i++)
            {
                if (!ingredients[i].usedInRecipe && ingredients[i].actor->HasTag(tag))
                {
                    ingredients[i].usedInRecipe = true;
                    return true;
                }
            }
            return false;
        }

        bool MatchesRecipe(const Recipe& recipe, Ingredient* ingredients, int count)
        {
            if (recipe.actorSlotCount + recipe.tagSlotCount > count ||
                (recipe.actorSlotCount == 0 && recipe.tagSlotCount == 0))
                return false;
            for (int i = 0; i < count; i++)
                ingredients[i].usedInRecipe = false;

            for (int s = 0; s < recipe.actorSlotCount; s++)
            {
                const Slot& slot = recipe.actorSlots[s];
                if (slot.count < 1)
                    continue;
                bool found = false;
                for (int h = 0; h < slot.count && !found; h++)
                    found = FindByName(ingredients, count, slot.hashes[h]);
                if (!found)
                    return false;
            }
            for (int s = 0; s < recipe.tagSlotCount; s++)
            {
                const Slot& slot = recipe.tagSlots[s];
                bool found = false;
                for (int h = 0; h < slot.count && !found; h++)
                    found = FindByTag(ingredients, count, slot.hashes[h]);
                if (!found)
                    return false;
            }
            return true;
        }

        // The decomp also skips a single recipe whose actor and tag lists are longer than the number of ingredients
        // (its cook() is marked NON_MATCHING). The game does not: one Acorn makes Sauteed Nuts, whose list has two
        // nuts, and one snail makes Seafood Skewer (list of five), as seen in game (savage13/cooking's test dishes)
        // and in Pistonite/botw-recipe, checked against the console. So any one of the listed actors or tags matches.
        bool MatchesSingleRecipe(const SingleRecipe& recipe, const Ingredient& single, int count)
        {
            if (count < recipe.num)
                return false;
            if (recipe.actors.count == 0 && recipe.tags.count == 0)
                return false;
            if (recipe.actors.count > 0)
            {
                bool found = false;
                for (int h = 0; h < recipe.actors.count && !found; h++)
                    found = single.hash == recipe.actors.hashes[h];
                if (!found)
                    return false;
            }
            if (recipe.tags.count > 0)
            {
                bool found = false;
                for (int h = 0; h < recipe.tags.count && !found; h++)
                    found = single.actor->HasTag(recipe.tags.hashes[h]);
                if (!found)
                    return false;
            }
            return true;
        }

        CookResult Finish(const Item& item, int32_t potency, CookResult result)
        {
            result.actor = item.actor;
            result.life = static_cast<int32_t>(item.life);
            result.effect = item.effect;
            result.vitality = static_cast<int32_t>(item.vitality);
            result.time = item.effect != EffectNone ? item.time : 0;
            result.price = item.price;
            result.potency = item.effect != EffectNone ? potency : 0;
            result.crit = item.crit;
            return result;
        }
    }

    void DishName(const CookData& data, const CookResult& result, char* out, size_t size)
    {
        const Actor* dish = data.FindActor(result.actor);
        const char* base = dish && dish->display[0] ? dish->display : (dish ? dish->name : "?");
        const char* prefix = "";
        if (!result.failure && result.effect != EffectNone && result.effect >= 0 && result.effect < EffectSlots)
            prefix = data.effectNames[result.effect];
        snprintf(out, size, "%s%s%s", prefix, prefix[0] ? " " : "", base);
    }

    CookResult Cook(const CookData& data, const uint32_t* items, int itemCount, CritBonus bonus)
    {
        // prepareCookArg: repeats become one ingredient with a count, in the order they first appear. cook: only
        // actors the game knows count.
        Ingredient ingredients[MaxIngredients];
        int count = 0;
        for (int i = 0; i < itemCount && i < MaxIngredients; i++)
        {
            bool merged = false;
            for (int j = 0; j < count; j++)
            {
                if (ingredients[j].hash == items[i])
                {
                    ingredients[j].count++;
                    merged = true;
                    break;
                }
            }
            if (merged)
                continue;
            const Actor* actor = data.FindActor(items[i]);
            if (!actor)
                continue;
            ingredients[count].hash = items[i];
            ingredients[count].actor = actor;
            ingredients[count].count = 1;
            count++;
        }

        CookResult result;
        Item item;
        int32_t potency = 0;
        if (count == 0)
        {
            // cookFailForMissingConfig
            item.actor = data.failActor;
            item.life = static_cast<float>(data.failActorLifeRecover);
            item.price = 1;
            result.failure = true;
            return Finish(item, 0, result);
        }

        auto cookWith = [&](uint32_t dish, bool hasHitPointBonus, int32_t hitPointBonus, bool hasTimeBonus,
                            int32_t timeBonus) {
            item.actor = dish;
            CalcIngredientsBoost(data, ingredients, count, item, potency);
            if (HasTag(data, item.actor, Tags::CookFailure()))
            {
                CookFail(data, item);
                result.failure = true;
                return Finish(item, 0, result);
            }
            CalcCritBoost(data, ingredients, count, item, bonus, result);
            CalcSpiceBoost(ingredients, count, item);
            CalcRecipeBoost(hasHitPointBonus, hitPointBonus, hasTimeBonus, timeBonus, item);
            AdjustItem(data, item);
            CalcItemPrice(data, ingredients, count, item);
            return Finish(item, potency, result);
        };

        const Ingredient* single = nullptr;
        bool multipleNonSpice = false;
        if (count > 1)
        {
            for (int r = 0; r < data.recipeCount; r++)
            {
                const Recipe& recipe = data.recipes[r];
                if (!MatchesRecipe(recipe, ingredients, count) || !data.FindActor(recipe.result))
                    continue;
                return cookWith(recipe.result, recipe.hasHitPointBonus, recipe.hitPointBonus, recipe.hasTimeBonus,
                                recipe.timeBonus);
            }
            for (int i = 0; i < count; i++)
            {
                if (ingredients[i].actor->HasTag(Tags::CookSpice()))
                    continue;
                if (single)
                {
                    multipleNonSpice = true;
                    break;
                }
                single = &ingredients[i];
            }
        }
        else
        {
            single = &ingredients[0];
        }

        if (single && !multipleNonSpice)
        {
            for (int r = 0; r < data.singleRecipeCount; r++)
            {
                const SingleRecipe& recipe = data.singleRecipes[r];
                if (!MatchesSingleRecipe(recipe, *single, count) || !data.FindActor(recipe.result))
                    continue;
                return cookWith(recipe.result, recipe.hasHitPointBonus, recipe.hitPointBonus, recipe.hasTimeBonus,
                                recipe.timeBonus);
            }
        }

        item.actor = data.failActor;
        CalcIngredientsBoost(data, ingredients, count, item, potency);
        CookFail(data, item);
        result.failure = true;
        return Finish(item, 0, result);
    }
}
