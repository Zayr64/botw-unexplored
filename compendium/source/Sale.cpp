#include "Sale.h"

namespace Compendium
{
    namespace
    {
        constexpr int MaxKinds = 160;

        struct Kind
        {
            const Actor* actor = nullptr;
            int32_t count = 0;
        };

        // Most valuable first.
        void SortByPrice(Kind* kinds, int n)
        {
            for (int i = 1; i < n; i++)
            {
                Kind tmp = kinds[i];
                int j = i - 1;
                while (j >= 0 && kinds[j].actor->sellingPrice < tmp.actor->sellingPrice)
                {
                    kinds[j + 1] = kinds[j];
                    j--;
                }
                kinds[j + 1] = tmp;
            }
        }
    }

    bool IsPrizedCritter(const Actor& actor)
    {
        return actor.HasTag(Tags::CookInsect()) && (actor.effectType == LifeMaxUp || actor.effectType == ExGutsMaxUp);
    }

    void PlanSale(const CookData& data, const StockItem* free, int freeCount, int potLimit, SalePlan& out,
                  bool keepPrized)
    {
        out = SalePlan();
        Kind parts[MaxKinds];
        int partKinds = 0;
        Kind critters[MaxKinds];
        int critterKinds = 0;
        const uint32_t fairy = Crc32("Animal_Insect_F"); // makes Fairy Tonic, worth 2

        for (int i = 0; i < freeCount; i++)
        {
            if (free[i].count <= 0)
                continue;
            const Actor* a = data.FindActor(free[i].hash);
            if (!a || !a->hasSellingPrice)
                continue;
            if (a->HasTag(Tags::CookOre()) || a->HasTag(Tags::CookLowPrice()))
            {
                if (out.rawCount < MaxRawSales)
                {
                    RawSale& r = out.raw[out.rawCount++];
                    r.item = a->hash;
                    r.count = free[i].count;
                    r.price = free[i].count * a->sellingPrice;
                    out.rawTotal += r.price;
                }
            }
            else if (a->HasTag(Tags::CookEnemy()) && a->hash != data.monsterExtract && partKinds < MaxKinds)
            {
                parts[partKinds].actor = a;
                parts[partKinds].count = free[i].count;
                partKinds++;
            }
            else if (keepPrized && IsPrizedCritter(*a))
            {
                if (out.keptCount < MaxKeptCritters)
                {
                    RawSale& r = out.kept[out.keptCount++];
                    r.item = a->hash;
                    r.count = free[i].count;
                    r.price = free[i].count * a->sellingPrice;
                }
            }
            else if (a->HasTag(Tags::CookInsect()) && a->hash != fairy && critterKinds < MaxKinds)
            {
                critters[critterKinds].actor = a;
                critters[critterKinds].count = free[i].count;
                critterKinds++;
            }
        }
        SortByPrice(parts, partKinds);
        SortByPrice(critters, critterKinds);

        // Each pot: the most valuable critter left and the four most valuable parts left.
        int p = 0;
        int k = 0;
        if (potLimit > MaxSalePots)
            potLimit = MaxSalePots;
        while (out.potCount < potLimit)
        {
            while (k < critterKinds && critters[k].count == 0)
                k++;
            while (p < partKinds && parts[p].count == 0)
                p++;
            if (k >= critterKinds || p >= partKinds)
                break;
            SalePot pot;
            pot.items[pot.itemCount++] = critters[k].actor->hash;
            pot.rawPrice += critters[k].actor->sellingPrice;
            critters[k].count--;
            for (int q = p; q < partKinds && pot.itemCount < 5; q++)
            {
                while (parts[q].count > 0 && pot.itemCount < 5)
                {
                    pot.items[pot.itemCount++] = parts[q].actor->hash;
                    pot.rawPrice += parts[q].actor->sellingPrice;
                    parts[q].count--;
                }
            }
            const CookResult r = Cook(data, pot.items, pot.itemCount);
            pot.price = r.failure ? 2 : r.price;
            out.pots[out.potCount++] = pot;
            out.cookedTotal += pot.price;
            out.cookedRawTotal += pot.rawPrice;
        }
        for (int q = 0; q < partKinds; q++)
        {
            out.partsLeft += parts[q].count;
            out.partsLeftRaw += parts[q].count * parts[q].actor->sellingPrice;
        }

        // Raw sales, most valuable first.
        for (int i = 1; i < out.rawCount; i++)
        {
            RawSale tmp = out.raw[i];
            int j = i - 1;
            while (j >= 0 && out.raw[j].price < tmp.price)
            {
                out.raw[j + 1] = out.raw[j];
                j--;
            }
            out.raw[j + 1] = tmp;
        }
    }
}
