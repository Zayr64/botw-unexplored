#include "CookData.h"

#include "TextLine.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Cozinha
{
    uint32_t Crc32(const char* text)
    {
        uint32_t crc = 0xFFFFFFFFu;
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; p++)
        {
            crc ^= *p;
            for (int k = 0; k < 8; k++)
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
        return ~crc;
    }

    namespace Tags
    {
        uint32_t CookEnemy() { static const uint32_t h = Crc32("CookEnemy"); return h; }
        uint32_t CookSpice() { static const uint32_t h = Crc32("CookSpice"); return h; }
        uint32_t CookLowPrice() { static const uint32_t h = Crc32("CookLowPrice"); return h; }
        uint32_t CookFailure() { static const uint32_t h = Crc32("CookFailure"); return h; }
        uint32_t CookEMedicine() { static const uint32_t h = Crc32("CookEMedicine"); return h; }
        uint32_t CookInsect() { static const uint32_t h = Crc32("CookInsect"); return h; }
        uint32_t CookOre() { static const uint32_t h = Crc32("CookOre"); return h; }
    }

    bool Actor::HasTag(uint32_t tag) const
    {
        for (int i = 0; i < tagCount; i++)
            if (tags[i] == tag)
                return true;
        return false;
    }

    namespace
    {
        using Line = TextLine;

        bool ReadSlot(Line& line, Slot& slot)
        {
            int32_t n = 0;
            if (!line.Int(n) || n < 0 || n > MaxSlotHashes)
                return false;
            slot.count = n;
            for (int i = 0; i < n; i++)
                if (!line.Hex(slot.hashes[i]))
                    return false;
            return true;
        }

        void SortActors(Actor* actors, int count)
        {
            // Insertion sort: a few hundred entries, once.
            for (int i = 1; i < count; i++)
            {
                Actor tmp = actors[i];
                int j = i - 1;
                while (j >= 0 && actors[j].hash > tmp.hash)
                {
                    actors[j + 1] = actors[j];
                    j--;
                }
                actors[j + 1] = tmp;
            }
        }
    }

    bool CookData::Load(const char* text, size_t size, char* error, size_t errorSize)
    {
        actorCount = 0;
        recipeCount = 0;
        singleRecipeCount = 0;
        for (int i = 0; i < EffectSlots; i++)
        {
            effects[i] = EffectEntry();
            effectNames[i][0] = '\0';
        }

        bool header = false;
        bool system = false;
        int lineNumber = 0;
        const char* p = text;
        const char* end = text + size;
        // Names come after their actor; kept here until the actors are sorted.
        while (p < end)
        {
            const char* eol = static_cast<const char*>(memchr(p, '\n', static_cast<size_t>(end - p)));
            if (!eol)
                eol = end;
            lineNumber++;
            Line line(p, eol);
            p = eol + 1;

            char kind[24];
            if (!line.Word(kind, sizeof(kind)) || kind[0] == '#')
                continue;

            bool ok = true;
            if (!header)
            {
                char version[8];
                ok = strcmp(kind, "BOTW_COZINHA_DATA") == 0 && line.Word(version, sizeof(version)) &&
                     strcmp(version, "1") == 0;
                if (!ok)
                {
                    snprintf(error, errorSize, "not a cook-data.txt (version 1)");
                    return false;
                }
                header = true;
                continue;
            }

            if (strcmp(kind, "SYS") == 0)
            {
                ok = line.Hex(failActor) && line.Hex(fairyTonic) && line.Hex(monsterExtract) &&
                     line.Float(lifeRecoverMultiplier) && line.Float(failActorLifeRecoverMultiplier) &&
                     line.Int(failActorLifeRecover) && line.Int(stoneFoodLifeRecover) && line.Int(critEffectTime);
                for (int i = 0; ok && i < 5; i++)
                    ok = line.Float(ingredientNumMultipliers[i]);
                for (int i = 0; ok && i < 5; i++)
                    ok = line.Int(ingredientNumSuccessRates[i]);
                system = ok;
            }
            else if (strcmp(kind, "EFFECT") == 0)
            {
                int32_t id = 0;
                EffectEntry e;
                ok = line.Int(id) && id >= 0 && id < EffectSlots && line.Int(e.boostTime) && line.Int(e.max) &&
                     line.Int(e.min) && line.Float(e.multiplier) && line.Int(e.ssa);
                if (ok)
                    effects[id] = e;
            }
            else if (strcmp(kind, "EFFECTNAME") == 0)
            {
                int32_t id = 0;
                ok = line.Int(id) && id >= 0 && id < EffectSlots;
                if (ok)
                    line.Rest(effectNames[id], sizeof(effectNames[id]));
            }
            else if (strcmp(kind, "ACTOR") == 0)
            {
                if (actorCount >= MaxActors)
                {
                    snprintf(error, errorSize, "line %d: more than %d actors", lineNumber, MaxActors);
                    return false;
                }
                Actor& a = actors[actorCount];
                a = Actor();
                int32_t effect = -1;
                int32_t tagCount = 0;
                ok = line.Hex(a.hash) && line.Word(a.name, sizeof(a.name)) &&
                     line.OptionalInt(a.sellingPrice, a.hasSellingPrice) &&
                     line.OptionalInt(a.buyingPrice, a.hasBuyingPrice) && line.OptionalInt(a.hitPointRecover) &&
                     line.OptionalInt(a.effectLevel) && line.Int(effect) && line.OptionalInt(a.spiceTime) &&
                     line.OptionalInt(a.spiceHitPoint) && line.OptionalInt(a.spiceMaxHeart) &&
                     line.OptionalInt(a.spiceStamina) && line.OptionalInt(a.spiceSuccessRate) && line.Int(tagCount) &&
                     tagCount >= 0 && tagCount <= MaxActorTags;
                a.effectType = static_cast<Effect>(effect);
                a.tagCount = ok ? tagCount : 0;
                for (int i = 0; ok && i < tagCount; i++)
                    ok = line.Hex(a.tags[i]);
                if (ok)
                    actorCount++;
            }
            else if (strcmp(kind, "NAME") == 0)
            {
                uint32_t hash = 0;
                ok = line.Hex(hash) && actorCount > 0 && actors[actorCount - 1].hash == hash;
                if (ok)
                    line.Rest(actors[actorCount - 1].display, sizeof(actors[actorCount - 1].display));
            }
            else if (strcmp(kind, "RECIPE") == 0)
            {
                if (recipeCount >= MaxRecipes)
                {
                    snprintf(error, errorSize, "line %d: more than %d recipes", lineNumber, MaxRecipes);
                    return false;
                }
                Recipe& r = recipes[recipeCount];
                r = Recipe();
                int32_t n = 0;
                ok = line.Hex(r.result) && line.OptionalInt(r.hitPointBonus, r.hasHitPointBonus) &&
                     line.OptionalInt(r.timeBonus, r.hasTimeBonus) && line.Int(n) && n >= 0 && n <= MaxSlots;
                r.actorSlotCount = ok ? n : 0;
                for (int i = 0; ok && i < n; i++)
                    ok = ReadSlot(line, r.actorSlots[i]);
                ok = ok && line.Int(n) && n >= 0 && n <= MaxSlots;
                r.tagSlotCount = ok ? n : 0;
                for (int i = 0; ok && i < n; i++)
                    ok = ReadSlot(line, r.tagSlots[i]);
                if (ok)
                    recipeCount++;
            }
            else if (strcmp(kind, "SINGLE") == 0)
            {
                if (singleRecipeCount >= MaxSingleRecipes)
                {
                    snprintf(error, errorSize, "line %d: more than %d single recipes", lineNumber, MaxSingleRecipes);
                    return false;
                }
                SingleRecipe& r = singleRecipes[singleRecipeCount];
                r = SingleRecipe();
                ok = line.Hex(r.result) && line.OptionalInt(r.hitPointBonus, r.hasHitPointBonus) &&
                     line.OptionalInt(r.timeBonus, r.hasTimeBonus) && line.Int(r.num) && ReadSlot(line, r.actors) &&
                     ReadSlot(line, r.tags);
                if (ok)
                    singleRecipeCount++;
            }
            // TAG lines and anything unknown: names for people reading the file.

            if (!ok)
            {
                snprintf(error, errorSize, "line %d: bad %s line", lineNumber, kind);
                return false;
            }
        }

        if (!system || actorCount == 0 || recipeCount == 0)
        {
            snprintf(error, errorSize, "incomplete file (%d actors, %d recipes)", actorCount, recipeCount);
            return false;
        }
        SortActors(actors, actorCount);
        return true;
    }

    const Actor* CookData::FindActor(uint32_t hash) const
    {
        int lo = 0;
        int hi = actorCount - 1;
        while (lo <= hi)
        {
            const int mid = (lo + hi) / 2;
            if (actors[mid].hash == hash)
                return &actors[mid];
            if (actors[mid].hash < hash)
                lo = mid + 1;
            else
                hi = mid - 1;
        }
        return nullptr;
    }

    const Actor* CookData::FindActorByName(const char* name) const
    {
        return FindActor(Crc32(name));
    }

    const Actor* CookData::FindActorByDisplayName(const char* display) const
    {
        // A recipe alias can share the name of the pouch item (Item_Plant_J and Item_PlantGet_J are both Silent
        // Princess): the ingredient, not a dish, and the one with the most tags.
        const Actor* found = nullptr;
        for (int i = 0; i < actorCount; i++)
        {
            const Actor& a = actors[i];
            if (strcmp(a.display, display) != 0)
                continue;
            const bool dish = strncmp(a.name, "Item_Cook_", 10) == 0;
            const bool foundDish = found && strncmp(found->name, "Item_Cook_", 10) == 0;
            if (!found || (foundDish && !dish) || (dish == foundDish && a.tagCount > found->tagCount))
                found = &a;
        }
        return found;
    }
}
