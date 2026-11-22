#include "Reserve.h"

#include <cstdio>
#include <cstring>

#include "TextLine.h"

namespace Cozinha
{
    namespace
    {
        template <typename T>
        void SortByHash(T* items, int count)
        {
            for (int i = 1; i < count; i++)
            {
                T tmp = items[i];
                int j = i - 1;
                while (j >= 0 && items[j].hash > tmp.hash)
                {
                    items[j + 1] = items[j];
                    j--;
                }
                items[j + 1] = tmp;
            }
        }

        template <typename T>
        const T* FindByHash(const T* items, int count, uint32_t hash)
        {
            int lo = 0;
            int hi = count - 1;
            while (lo <= hi)
            {
                const int mid = (lo + hi) / 2;
                if (items[mid].hash == hash)
                    return &items[mid];
                if (items[mid].hash < hash)
                    lo = mid + 1;
                else
                    hi = mid - 1;
            }
            return nullptr;
        }

        bool ReadIngredients(TextLine& line, int& count, uint32_t* items, int32_t* amounts)
        {
            int32_t n = 0;
            if (!line.Int(n) || n < 0 || n > MaxRecipeItems)
                return false;
            count = n;
            for (int i = 0; i < n; i++)
                if (!line.Hex(items[i]) || !line.Int(amounts[i]))
                    return false;
            return true;
        }

        bool IsArmorName(const char* name)
        {
            return strncmp(name, "Armor_", 6) == 0;
        }
    }

    bool ReserveData::Load(const char* text, size_t size, char* error, size_t errorSize)
    {
        armorCount = craftCount = shopItemCount = questCount = labelCount = 0;
        bool ok = true;
        ForEachLine(text, size, [&](const char* kind, TextLine& line, int lineNumber) {
            if (strcmp(kind, "ARMOR") == 0)
            {
                if (armorCount >= MaxArmors)
                {
                    snprintf(error, errorSize, "line %d: more than %d armors", lineNumber, MaxArmors);
                    return ok = false;
                }
                ArmorEntry& a = armors[armorCount];
                a = ArmorEntry();
                char series[48];
                ok = line.Hex(a.hash) && line.Word(a.name, sizeof(a.name)) && line.OptionalHex(a.next) &&
                     line.Int(a.star) && line.Int(a.buyingPrice) && line.Int(a.creatingPrice) &&
                     line.Word(series, sizeof(series)) && ReadIngredients(line, a.ingredientCount, a.ingredients, a.amounts);
                a.amiibo = strncmp(series, "Amiibo", 6) == 0;
                if (ok)
                    armorCount++;
            }
            else if (strcmp(kind, "CRAFT") == 0)
            {
                if (craftCount >= MaxCrafts)
                {
                    snprintf(error, errorSize, "line %d: more than %d crafted items", lineNumber, MaxCrafts);
                    return ok = false;
                }
                CraftEntry& c = crafts[craftCount];
                c = CraftEntry();
                ok = line.Hex(c.hash) && line.Word(c.name, sizeof(c.name)) && line.Int(c.price) &&
                     ReadIngredients(line, c.ingredientCount, c.ingredients, c.amounts);
                if (ok)
                    craftCount++;
            }
            else if (strcmp(kind, "SHOP") == 0)
            {
                if (shopItemCount >= MaxShopItems)
                {
                    snprintf(error, errorSize, "line %d: more than %d shop items", lineNumber, MaxShopItems);
                    return ok = false;
                }
                ShopItem& s = shopItems[shopItemCount];
                char how[8];
                ok = line.Hex(s.hash) && line.Word(how, sizeof(how));
                s.crafted = strcmp(how, "craft") == 0;
                if (ok)
                    shopItemCount++;
            }
            else if (strcmp(kind, "LABEL") == 0)
            {
                if (labelCount >= MaxLabels)
                {
                    snprintf(error, errorSize, "line %d: more than %d labels", lineNumber, MaxLabels);
                    return ok = false;
                }
                ok = line.Hex(labels[labelCount].hash);
                if (ok)
                    line.Rest(labels[labelCount].text, sizeof(labels[labelCount].text));
                if (ok)
                    labelCount++;
            }
            else if (strcmp(kind, "QUEST") == 0)
            {
                if (questCount >= MaxQuestNeeds)
                {
                    snprintf(error, errorSize, "line %d: more than %d quest needs", lineNumber, MaxQuestNeeds);
                    return ok = false;
                }
                QuestNeed& q = quests[questCount];
                q = QuestNeed();
                char flagName[80];
                bool known = false;
                ok = line.Hex(q.flag) && line.Word(flagName, sizeof(flagName)) && line.Hex(q.item) &&
                     line.OptionalInt(q.count, known);
                q.countKnown = known;
                if (!known)
                    q.count = 1;
                if (ok)
                {
                    line.Rest(q.title, sizeof(q.title));
                    questCount++;
                }
            }
            if (!ok && error[0] == '\0')
                snprintf(error, errorSize, "line %d: bad %s line", lineNumber, kind);
            return ok;
        });
        if (!ok)
            return false;
        if (armorCount == 0)
        {
            snprintf(error, errorSize, "no ARMOR lines (cook-data.txt from an older gen_cook_data.py?)");
            return false;
        }
        SortByHash(armors, armorCount);
        SortByHash(shopItems, shopItemCount);
        SortByHash(labels, labelCount);
        return true;
    }

    const ArmorEntry* ReserveData::FindArmor(uint32_t hash) const
    {
        return FindByHash(armors, armorCount, hash);
    }

    const ShopItem* ReserveData::FindShopItem(uint32_t hash) const
    {
        return FindByHash(shopItems, shopItemCount, hash);
    }

    const char* ReserveData::Label(uint32_t hash) const
    {
        const LabelEntry* l = FindByHash(labels, labelCount, hash);
        return l ? l->text : "";
    }

    int32_t MaterialPlan::Needed(uint8_t tierMask) const
    {
        int32_t n = 0;
        for (int t = 0; t < TierCount; t++)
            if (tierMask & (1u << t))
                n += need[t];
        return n;
    }

    int32_t MaterialPlan::Free(uint8_t tierMask) const
    {
        const int32_t free = have - Needed(tierMask);
        return free > 0 ? free : 0;
    }

    const MaterialPlan* ReservePlan::Find(uint32_t item) const
    {
        for (int i = 0; i < materialCount; i++)
            if (materials[i].item == item)
                return &materials[i];
        return nullptr;
    }

    int32_t ReservePlan::RupeesNeeded(uint8_t tierMask) const
    {
        int32_t n = 0;
        for (int t = 0; t < TierCount; t++)
            if (tierMask & (1u << t))
                n += rupeesNeeded[t];
        return n;
    }

    namespace
    {
        MaterialPlan* Material(ReservePlan& plan, uint32_t item)
        {
            for (int i = 0; i < plan.materialCount; i++)
                if (plan.materials[i].item == item)
                    return &plan.materials[i];
            if (plan.materialCount >= MaxMaterials)
                return nullptr;
            MaterialPlan& m = plan.materials[plan.materialCount++];
            m = MaterialPlan();
            m.item = item;
            return &m;
        }

        void AddNeeds(ReservePlan& plan, Tier tier, int count, const uint32_t* items, const int32_t* amounts,
                      int32_t times = 1)
        {
            for (int i = 0; i < count; i++)
            {
                if (amounts[i] <= 0)
                    continue;
                MaterialPlan* m = Material(plan, items[i]);
                if (m)
                    m->need[tier] += amounts[i] * times;
            }
        }

        bool IsGet(const SaveFile& save, const char* actor)
        {
            char flag[96];
            snprintf(flag, sizeof(flag), "IsGet_%s", actor);
            bool v = false;
            return save.GetBool(flag, v) && v;
        }

        // The ingredients of an armor step without the piece one star down.
        int MaterialsOf(const ArmorEntry& a, uint32_t* items, int32_t* amounts, const ReserveData& data)
        {
            int n = 0;
            for (int i = 0; i < a.ingredientCount; i++)
            {
                if (data.FindArmor(a.ingredients[i]))
                    continue;
                items[n] = a.ingredients[i];
                amounts[n] = a.amounts[i];
                n++;
            }
            return n;
        }
    }

    void PlanReserve(const CookData& cook, const ReserveData& data, const SaveFile& save, const PouchItem* pouch,
                     int pouchCount, ReservePlan& out)
    {
        out = ReservePlan();
        save.GetS32("CurrentRupee", out.rupees);

        // Fountains: RevivalGreatFairy1-4 (5 is the horse fairy). Each one restored allows one more star; the
        // closed ones cost, in the order they are opened, what AmountRupeeForRevivalFountainN says.
        int32_t closedCost = 0;
        for (int i = 1; i <= FountainCount; i++)
        {
            char name[48];
            snprintf(name, sizeof(name), "RevivalGreatFairy%d", i);
            bool restored = false;
            save.GetBool(name, restored);
            if (restored)
            {
                out.fairies++;
                continue;
            }
            snprintf(name, sizeof(name), "AmountRupeeForRevivalFountain%d", i);
            int32_t price = 0;
            if (save.GetS32(name, price) && price > 0)
                closedCost += price;
        }
        out.rupeesNeeded[OwnedNextFairy] += closedCost;

        // Materials in the pouch.
        for (int i = 0; i < pouchCount; i++)
        {
            const PouchItem& p = pouch[i];
            if (IsArmorName(p.name) || strncmp(p.name, "Weapon_", 7) == 0 || strncmp(p.name, "Item_Cook_", 10) == 0)
                continue;
            MaterialPlan* m = Material(out, p.hash);
            if (m)
            {
                m->have += p.value;
                m->everObtained = true;
            }
        }

        // Armor in the pouch: every step up from it. A step to star s needs s fairies.
        bool chainOwned[MaxArmors] = {};
        for (int i = 0; i < pouchCount; i++)
        {
            const ArmorEntry* a = data.FindArmor(pouch[i].hash);
            if (!a)
                continue;
            out.ownedArmorPieces++;
            chainOwned[a - data.armors] = true;
            for (const ArmorEntry* step = data.FindArmor(a->next); step; step = data.FindArmor(step->next))
            {
                chainOwned[step - data.armors] = true;
                const int stars = step->star - 1;
                const Tier tier = stars <= out.fairies ? OwnedReachable : OwnedNextFairy;
                out.ownedUpgradesLeft[tier == OwnedReachable ? 0 : 1]++;
                uint32_t items[MaxRecipeItems];
                int32_t amounts[MaxRecipeItems];
                const int n = MaterialsOf(*step, items, amounts, data);
                AddNeeds(out, tier, n, items, amounts);
                if (!step->next)
                    break;
            }
        }
        // Pieces below an owned one (only reachable by going down the chain) count as owned too.
        for (int i = 0; i < data.armorCount; i++)
        {
            if (!chainOwned[i])
                continue;
            for (int j = 0; j < data.armors[i].ingredientCount; j++)
            {
                const ArmorEntry* below = data.FindArmor(data.armors[i].ingredients[j]);
                while (below && !chainOwned[below - data.armors])
                {
                    chainOwned[below - data.armors] = true;
                    const ArmorEntry* next = nullptr;
                    for (int k = 0; k < below->ingredientCount && !next; k++)
                        next = data.FindArmor(below->ingredients[k]);
                    below = next;
                }
            }
        }

        // Armor not in the pouch: the base pieces (no star, not made from another piece, not amiibo), bought where a
        // shop sells them or made at the Ancient Oven, and then every upgrade.
        for (int i = 0; i < data.armorCount; i++)
        {
            const ArmorEntry& base = data.armors[i];
            if (chainOwned[i] || base.star != 1 || base.amiibo)
                continue;
            const size_t len = strlen(base.name);
            if (len > 2 && strcmp(base.name + len - 2, "_B") == 0)
                continue; // another look of a piece, with the same upgrades
            bool madeFromArmor = false;
            for (int k = 0; k < base.ingredientCount; k++)
                madeFromArmor = madeFromArmor || data.FindArmor(base.ingredients[k]) != nullptr;
            if (madeFromArmor)
                continue;

            const ShopItem* shop = data.FindShopItem(base.hash);
            const bool hasUpgrades = base.next != 0;
            if (!shop && !hasUpgrades)
                continue; // found in the world, nothing to hold back for it
            out.notOwnedPieces++;
            if (shop)
            {
                out.notOwnedForSale++;
                if (shop->crafted)
                {
                    out.rupeesNeeded[ArmorNotOwned] += base.creatingPrice;
                    AddNeeds(out, ArmorNotOwned, base.ingredientCount, base.ingredients, base.amounts);
                }
                else
                {
                    out.rupeesNeeded[ArmorNotOwned] += base.buyingPrice;
                }
            }
            for (const ArmorEntry* step = data.FindArmor(base.next); step; step = data.FindArmor(step->next))
            {
                uint32_t items[MaxRecipeItems];
                int32_t amounts[MaxRecipeItems];
                const int n = MaterialsOf(*step, items, amounts, data);
                AddNeeds(out, ArmorNotOwned, n, items, amounts);
                if (!step->next)
                    break;
            }
        }

        // Ancient Oven: weapons, bow and shield never obtained, one of each. Arrows are left out (bought many times).
        for (int i = 0; i < data.craftCount; i++)
        {
            const CraftEntry& c = data.crafts[i];
            if (strncmp(c.name, "Weapon_", 7) != 0 || IsGet(save, c.name))
                continue;
            out.craftedItems++;
            out.rupeesNeeded[Crafted] += c.price;
            AddNeeds(out, Crafted, c.ingredientCount, c.ingredients, c.amounts);
        }

        // Side quests not finished.
        uint32_t counted[MaxQuestNeeds] = {};
        int countedQuests = 0;
        for (int i = 0; i < data.questCount; i++)
        {
            const QuestNeed& q = data.quests[i];
            bool finished = false;
            save.GetBool(q.flag, finished);
            if (finished)
                continue;
            MaterialPlan* m = Material(out, q.item);
            if (m)
                m->need[Quests] += q.count;
            if (!q.countKnown)
                out.questsWithUnknownCount++;
            bool seen = false;
            for (int k = 0; k < countedQuests; k++)
                seen = seen || counted[k] == q.flag;
            if (!seen && countedQuests < MaxQuestNeeds)
                counted[countedQuests++] = q.flag;
        }
        out.pendingQuests = countedQuests;

        // Names of materials the player has had at some point can be shown; the others stay hidden.
        for (int i = 0; i < out.materialCount; i++)
        {
            MaterialPlan& m = out.materials[i];
            const Actor* actor = cook.FindActor(m.item);
            if (!m.everObtained && actor)
                m.everObtained = IsGet(save, actor->name);
        }
    }
}
