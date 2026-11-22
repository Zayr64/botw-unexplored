// Cooking simulator against dishes read from the game's screen.
//
// Usage: test_cook <cook-data.txt> [cases.tsv]
// cook-data.txt comes from the game's files (compendium/tools/gen_cook_data.py), so it is not in the repository; without
// it the test says so and passes. cases.tsv comes from savage13/cooking's test dishes (savage13_cases.py).

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "Cook.h"
#include "CookData.h"
#include "Sale.h"
#include "SaveFile.h"
#include "Search.h"

using namespace Compendium;

namespace
{
    int g_failures = 0;

    void Check(bool ok, const std::string& what)
    {
        if (!ok)
        {
            g_failures++;
            printf("FAIL: %s\n", what.c_str());
        }
    }

    const char* EffectName(Effect e)
    {
        switch (e)
        {
        case LifeRecover: return "LifeRecover";
        case LifeMaxUp: return "LifeMaxUp";
        case ResistHot: return "ResistHot";
        case ResistCold: return "ResistCold";
        case ResistElectric: return "ResistElectric";
        case AttackUp: return "AttackUp";
        case DefenseUp: return "DefenseUp";
        case Quietness: return "Quietness";
        case MovingSpeed: return "MovingSpeed";
        case GutsRecover: return "GutsRecover";
        case ExGutsMaxUp: return "ExGutsMaxUp";
        case Fireproof: return "Fireproof";
        default: return "-";
        }
    }

    std::vector<std::string> Split(const std::string& s, char sep)
    {
        std::vector<std::string> out;
        std::string cur;
        std::istringstream in(s);
        while (std::getline(in, cur, sep))
            out.push_back(cur);
        return out;
    }

    CookResult CookNames(const CookData& data, const std::vector<std::string>& names, CritBonus bonus = CritBonus::None)
    {
        uint32_t hashes[5] = {};
        int n = 0;
        for (const std::string& name : names)
            if (n < 5)
                hashes[n++] = Crc32(name.c_str());
        return Cook(data, hashes, n, bonus);
    }

    std::vector<std::string> ByDisplay(const CookData& data, const std::vector<std::string>& displays)
    {
        std::vector<std::string> out;
        for (const std::string& d : displays)
        {
            const Actor* a = data.FindActorByDisplayName(d.c_str());
            Check(a != nullptr, "no actor named " + d);
            out.push_back(a ? a->name : "");
        }
        return out;
    }

    std::string Describe(const CookData& data, const CookResult& r)
    {
        char dish[96];
        DishName(data, r, dish, sizeof(dish));
        char buf[256];
        snprintf(buf, sizeof(buf), "%s life=%d effect=%s vitality=%d potency=%d time=%d price=%d crit=%d%%",
                 dish, r.life, EffectName(r.effect), r.vitality, r.potency, r.time, r.price,
                 r.critChance);
        return buf;
    }

    // Facts from the decomp and from guides, independent of savage13's cases.
    void KnownDishes(const CookData& data)
    {
        // One Acorn: 3 x 1.5 = 4 -> 10, capped at its buying price.
        const Actor* acorn = data.FindActorByDisplayName("Acorn");
        CookResult r = CookNames(data, ByDisplay(data, {"Acorn"}));
        Check(acorn && r.price == acorn->buyingPrice, "one Acorn sells for its buying price: " + Describe(data, r));

        // The best-known money pot: 4 Lynel Guts + Energetic Rhino Beetle = 2330.
        r = CookNames(data, ByDisplay(data, {"Lynel Guts", "Lynel Guts", "Lynel Guts", "Lynel Guts",
                                             "Energetic Rhino Beetle"}));
        Check(r.price == 2330 && !r.failure, "4 Lynel Guts + Energetic Rhino Beetle = 2330: " + Describe(data, r));

        // Ore makes Rock-Hard Food, worth 2 and 1 quarter heart.
        r = CookNames(data, ByDisplay(data, {"Apple", "Amber"}));
        Check(r.failure && r.price == 2 && r.life == 1, "Apple + Amber is Rock-Hard Food: " + Describe(data, r));

        // Monster part with no critter: Dubious Food.
        r = CookNames(data, ByDisplay(data, {"Apple", "Bokoblin Horn"}));
        Check(r.failure && r.price == 2 && r.life == 4, "Apple + Bokoblin Horn is Dubious Food: " + Describe(data, r));

        // 5 Raw Gourmet Meat: 30 hearts, price 490.
        r = CookNames(data, ByDisplay(data, {"Raw Gourmet Meat", "Raw Gourmet Meat", "Raw Gourmet Meat",
                                             "Raw Gourmet Meat", "Raw Gourmet Meat"}));
        Check(r.life == 120 && r.price == 490, "5 Raw Gourmet Meat: " + Describe(data, r));

        // A critical success on a timed effect can add a level, 3 hearts or 5:00.
        r = CookNames(data, ByDisplay(data, {"Mighty Bananas"}));
        Check(r.critBonuses == 7, "Mighty Bananas: any of the three critical bonuses: " + Describe(data, r));
        CookResult t = CookNames(data, ByDisplay(data, {"Mighty Bananas"}), CritBonus::Time);
        Check(t.time == r.time + 300 && t.crit, "critical time bonus adds 5:00: " + Describe(data, t));
    }

    // A made-up save: header, sorted records, the pouch as 64-character texts split in 4-byte records.
    void SaveFileReading()
    {
        struct Rec
        {
            uint32_t hash;
            uint32_t value;
        };
        std::vector<Rec> recs;
        const char* items[] = {"Item_Fruit_A", "Animal_Insect_C"};
        const int32_t counts[] = {12, 3};
        for (int slot = 0; slot < 3; slot++)
        {
            char name[64] = {};
            if (slot < 2)
                snprintf(name, sizeof(name), "%s", items[slot]);
            for (int k = 0; k < 16; k++)
            {
                uint32_t v;
                memcpy(&v, name + k * 4, 4);
                recs.push_back({Crc32("PorchItem"), v});
            }
            recs.push_back({Crc32("PorchItem_Value1"), static_cast<uint32_t>(slot < 2 ? counts[slot] : 0)});
        }
        recs.push_back({Crc32("CurrentRupee"), 1249});
        for (int i = 0; i < 40; i++)
            recs.push_back({Crc32(("Filler" + std::to_string(i)).c_str()), 0});
        // Stable sort by hash keeps the order of the pouch's records.
        for (size_t i = 1; i < recs.size(); i++)
            for (size_t j = i; j > 0 && recs[j - 1].hash > recs[j].hash; j--)
                std::swap(recs[j - 1], recs[j]);
        std::vector<uint8_t> bytes(12, 0);
        for (const Rec& r : recs)
        {
            const uint8_t* h = reinterpret_cast<const uint8_t*>(&r.hash);
            const uint8_t* v = reinterpret_cast<const uint8_t*>(&r.value);
            bytes.insert(bytes.end(), h, h + 4);
            bytes.insert(bytes.end(), v, v + 4);
        }
        bytes.insert(bytes.end(), 4, 0xFF);

        SaveFile save;
        Check(save.Load(bytes.data(), bytes.size()), "made-up save loads");
        int32_t rupees = 0;
        Check(save.GetS32("CurrentRupee", rupees) && rupees == 1249, "rupees read");
        PouchItem pouch[8];
        const int n = save.ReadPouch(pouch, 8);
        Check(n == 2, "pouch stops at the first empty slot: " + std::to_string(n));
        Check(n >= 2 && strcmp(pouch[0].name, "Item_Fruit_A") == 0 && pouch[0].value == 12 &&
                  strcmp(pouch[1].name, "Animal_Insect_C") == 0 && pouch[1].value == 3,
              "pouch names and counts");
        int32_t missing = 0;
        Check(!save.GetS32("NotAFlag", missing), "missing flag");
    }

    void SearchAndSale(const CookData& data)
    {
        const Actor* banana = data.FindActorByDisplayName("Mighty Bananas");
        const Actor* apple = data.FindActorByDisplayName("Apple");
        if (!banana || !apple)
        {
            Check(false, "Mighty Bananas and Apple in the data");
            return;
        }
        StockItem stock[2];
        stock[0].hash = banana->hash;
        stock[0].count = 5;
        stock[1].hash = apple->hash;
        stock[1].count = 5;
        SearchResult r = FindBestForEffect(data, stock, 2, AttackUp);
        Check(r.found && r.best.result.vitality == 3 && r.best.result.effect == AttackUp,
              "5 Mighty Bananas and 5 Apples: attack level 3: " + Describe(data, r.best.result));
        Check(r.cheapest.itemCount > 0 && r.cheapest.result.vitality == 3 && r.cheapest.cost <= r.best.cost,
              "the cheapest level 3 costs no more than the best");
        r = FindBestForEffect(data, stock, 2, Quietness);
        Check(!r.found, "no stealth dish from bananas and apples");

        const Actor* critter = data.FindActorByDisplayName("Summerwing Butterfly");
        const Actor* talon = data.FindActorByDisplayName("Lizalfos Talon");
        if (!critter || !talon)
        {
            Check(false, "Summerwing Butterfly and Lizalfos Talon in the data");
            return;
        }
        StockItem free[2];
        free[0].hash = critter->hash;
        free[0].count = 1;
        free[1].hash = talon->hash;
        free[1].count = 9;
        std::unique_ptr<SalePlan> sale(new SalePlan());
        PlanSale(data, free, 2, 60, *sale);
        const uint32_t pot[5] = {critter->hash, talon->hash, talon->hash, talon->hash, talon->hash};
        const CookResult expected = Cook(data, pot, 5);
        Check(sale->potCount == 1 && sale->pots[0].price == expected.price && sale->partsLeft == 5,
              "one critter cooks four talons; five are left: " + std::to_string(sale->potCount) + " pots");
    }

    void SavageCases(const CookData& data, const char* path)
    {
        std::ifstream in(path);
        if (!in)
        {
            printf("no cases file %s\n", path);
            g_failures++;
            return;
        }
        int total = 0;
        int failed = 0;
        int randomSkipped = 0;
        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty())
                continue;
            const std::vector<std::string> f = Split(line, '\t');
            if (f.size() < 11)
                continue;
            total++;
            const CookResult r = CookNames(data, Split(f[2], ','));
            if (r.monsterExtract)
            {
                // Monster Extract rolls hearts, effect and time: a screenshot is one of several outcomes.
                randomSkipped++;
                continue;
            }
            const Actor* dish = data.FindActor(r.actor);
            char shown[96];
            DishName(data, r, shown, sizeof(shown));
            const std::string& effect = f[3];
            std::string why;
            // savage13 writes the effect's word only for elixirs ("Chilly Elixir", but "Mushroom Skewer").
            if (!dish || (f[1] != dish->display && f[1] != shown))
                why += " name";
            if (f[10] != "-" && std::atoi(f[10].c_str()) != r.price)
                why += " price";
            if (effect != EffectName(r.effect))
                why += " effect";
            if (effect == "LifeMaxUp")
            {
                if (f[5] != "-" && std::atoi(f[5].c_str()) != r.potency)
                    why += " potency";
                if (f[6] != "-" && std::atoi(f[6].c_str()) != r.vitality / 4)
                    why += " level";
            }
            else if (f[4] != "-" && std::atoi(f[4].c_str()) != r.life)
                why += " hp";
            if (effect == "GutsRecover" && f[7] != "-" && std::fabs(std::atof(f[7].c_str()) - r.vitality / 1000.0) > 1e-6)
                why += " stamina";
            if (effect == "ExGutsMaxUp" && f[8] != "-" && std::fabs(std::atof(f[8].c_str()) - r.vitality / 5.0) > 1e-6)
                why += " stamina_extra";
            const bool timed = effect != "-" && effect != "LifeMaxUp" && effect != "GutsRecover" && effect != "ExGutsMaxUp";
            if (timed)
            {
                if (f[5] != "-" && std::atoi(f[5].c_str()) != r.potency)
                    why += " potency";
                if (f[6] != "-" && std::atoi(f[6].c_str()) != r.vitality)
                    why += " level";
                if (f[9] != "-" && std::atoi(f[9].c_str()) != r.time)
                    why += " time";
            }
            if (!why.empty())
            {
                failed++;
                printf("MISMATCH%s: %s %s [%s] expected effect=%s hp=%s potency=%s level=%s stamina=%s/%s time=%s "
                       "price=%s; got %s\n",
                       why.c_str(), f[0].c_str(), f[1].c_str(), f[2].c_str(), f[3].c_str(), f[4].c_str(), f[5].c_str(),
                       f[6].c_str(), f[7].c_str(), f[8].c_str(), f[9].c_str(), f[10].c_str(), Describe(data, r).c_str());
            }
        }
        printf("savage13 cases: %d of %d match (%d with Monster Extract not compared)\n",
               total - failed - randomSkipped, total - randomSkipped, randomSkipped);
        g_failures += failed;
    }
}

int main(int argc, char** argv)
{
    SaveFileReading();
    if (argc < 2)
    {
        printf("test_cook: no cook-data.txt given (it is made from the game's files); only the save reader tested\n");
        return g_failures ? 1 : 0;
    }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in)
    {
        printf("test_cook: %s not found; only the save reader tested\n", argv[1]);
        return g_failures ? 1 : 0;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::unique_ptr<CookData> data(new CookData());
    char error[128] = {};
    if (!data->Load(text.data(), text.size(), error, sizeof(error)))
    {
        printf("FAIL: load: %s\n", error);
        return 1;
    }
    printf("cook-data: %d actors, %d recipes, %d single recipes, CookData %zu bytes\n", data->actorCount,
           data->recipeCount, data->singleRecipeCount, sizeof(CookData));

    KnownDishes(*data);
    SearchAndSale(*data);
    if (argc >= 3)
        SavageCases(*data, argv[2]);

    if (g_failures)
    {
        printf("test_cook: %d failures\n", g_failures);
        return 1;
    }
    printf("test_cook: ok\n");
    return 0;
}
