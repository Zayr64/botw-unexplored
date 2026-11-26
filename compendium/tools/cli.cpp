// The cooking app's planning, on the PC: reads cook-data.txt and a game_data.sav and prints the reserve, the best
// dish for each effect and the sale plan.
//
// Usage: compendium_cli <cook-data.txt> <game_data.sav> [--reveal] [--whole-pouch]
//   --reveal       show what the hidden tiers are for (armor not owned, Ancient Oven, quests): spoilers
//   --whole-pouch  search dishes with everything in the pouch, not only what is free after the reserve
//   --hearts-first the best dish of an effect: more hearts before the cheaper, as up to 0.1.8
//                  (Ranking::HeartsThenPrice; the app and the default: the cheaper first)
//   --reserve owned|no-armor|all   what the reserve keeps before the dishes: the armor you have only (the app's X
//                  off), all but the armor you do not have, or everything (the app's X on; the default)

#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "Cook.h"
#include "CookData.h"
#include "Reserve.h"
#include "Sale.h"
#include "SaveFile.h"
#include "Search.h"

using namespace Compendium;

namespace
{
    bool ReadFile(const char* path, std::string& out)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        out.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return true;
    }

    const char* NameOf(const CookData& cook, const ReserveData& reserve, uint32_t hash)
    {
        const Actor* a = cook.FindActor(hash);
        if (a && a->display[0])
            return a->display;
        const char* label = reserve.Label(hash);
        if (label[0])
            return label;
        return a ? a->name : "?";
    }

    const char* EffectLabel(Effect e)
    {
        switch (e)
        {
        case LifeMaxUp: return "Hearty (yellow hearts)";
        case GutsRecover: return "Energizing (stamina)";
        case ExGutsMaxUp: return "Enduring (extra stamina)";
        case ResistHot: return "Chilly (heat resistance)";
        case ResistCold: return "Spicy (cold resistance)";
        case ResistElectric: return "Electro (shock resistance)";
        case AttackUp: return "Mighty (attack)";
        case DefenseUp: return "Tough (defense)";
        case Quietness: return "Sneaky (stealth)";
        case MovingSpeed: return "Hasty (speed)";
        case Fireproof: return "Fireproof";
        default: return "Hearts";
        }
    }

    std::string Strength(const CookResult& r)
    {
        char buf[96];
        switch (r.effect)
        {
        case LifeMaxUp:
            snprintf(buf, sizeof(buf), "+%d yellow hearts, full recovery", r.vitality / 4);
            break;
        case GutsRecover:
            snprintf(buf, sizeof(buf), "%.1f wheels of stamina", r.vitality / 1000.0);
            break;
        case ExGutsMaxUp:
            snprintf(buf, sizeof(buf), "+%.1f extra wheels", r.vitality / 5.0);
            break;
        case EffectNone:
            buf[0] = '\0';
            break;
        default:
            snprintf(buf, sizeof(buf), "level %d, %d:%02d", r.vitality, r.time / 60, r.time % 60);
            break;
        }
        return buf;
    }

    std::string Describe(const CookData& cook, const ReserveData& reserve, const FoundDish& d)
    {
        char name[96];
        DishName(cook, d.result, name, sizeof(name));
        std::string out = name;
        out += "  <=  ";
        for (int i = 0; i < d.itemCount; i++)
        {
            if (i)
                out += " + ";
            out += NameOf(cook, reserve, d.items[i]);
        }
        char tail[160];
        const std::string strength = Strength(d.result);
        snprintf(tail, sizeof(tail), "\n      %s%s%.2f hearts, sells for %d, ingredients worth %d, critical %d%%",
                 strength.c_str(), strength.empty() ? "" : ", ", d.result.life / 4.0, d.result.price, d.cost,
                 d.result.critChance > 100 ? 100 : d.result.critChance);
        out += tail;
        if (d.result.critChance > 0 && d.result.critBonuses)
        {
            out += " (";
            bool first = true;
            if (d.result.critBonuses & static_cast<uint8_t>(CritBonus::Life))
                out += first ? "+3 hearts" : " or +3 hearts", first = false;
            if (d.result.critBonuses & static_cast<uint8_t>(CritBonus::Vitality))
                out += first ? "+1 level" : " or +1 level", first = false;
            if (d.result.critBonuses & static_cast<uint8_t>(CritBonus::Time))
                out += first ? "+5:00" : " or +5:00", first = false;
            out += ")";
        }
        return out;
    }
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: %s <cook-data.txt> <game_data.sav> [--reveal] [--whole-pouch]\n", argv[0]);
        return 2;
    }
    bool reveal = false;
    bool wholePouch = false;
    std::vector<std::string> cookOnly; // --cook "A,B,C": just cook these (English names) and stop
    Ranking ranking = Ranking::PriceThenHearts;
    uint8_t mask = AllTiers;
    for (int i = 3; i < argc; i++)
    {
        reveal = reveal || strcmp(argv[i], "--reveal") == 0;
        wholePouch = wholePouch || strcmp(argv[i], "--whole-pouch") == 0;
        if (strcmp(argv[i], "--hearts-first") == 0)
            ranking = Ranking::HeartsThenPrice;
        if (strcmp(argv[i], "--reserve") == 0 && i + 1 < argc)
        {
            const char* which = argv[++i];
            const uint8_t owned = (1u << OwnedReachable) | (1u << OwnedNextFairy);
            mask = strcmp(which, "owned") == 0      ? owned
                 : strcmp(which, "no-armor") == 0   ? static_cast<uint8_t>(owned | (1u << Crafted) | (1u << Quests))
                                                    : AllTiers;
        }
        if (strcmp(argv[i], "--cook") == 0 && i + 1 < argc)
            cookOnly.push_back(argv[++i]);
    }

    std::string text, saveBytes;
    if (!ReadFile(argv[1], text) || !ReadFile(argv[2], saveBytes))
    {
        fprintf(stderr, "cannot read the files\n");
        return 1;
    }
    std::unique_ptr<CookData> cook(new CookData());
    std::unique_ptr<ReserveData> reserve(new ReserveData());
    char error[160] = {};
    if (!cook->Load(text.data(), text.size(), error, sizeof(error)) ||
        !reserve->Load(text.data(), text.size(), error, sizeof(error)))
    {
        fprintf(stderr, "cook-data.txt: %s\n", error);
        return 1;
    }
    if (!cookOnly.empty())
    {
        for (const std::string& list : cookOnly)
        {
            FoundDish d;
            size_t start = 0;
            while (start <= list.size() && d.itemCount < 5)
            {
                const size_t comma = list.find(',', start);
                const std::string name = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                const Actor* a = cook->FindActorByDisplayName(name.c_str());
                if (!a)
                {
                    fprintf(stderr, "unknown ingredient: %s\n", name.c_str());
                    return 1;
                }
                d.items[d.itemCount++] = a->hash;
                if (a->hasSellingPrice)
                    d.cost += a->sellingPrice;
                if (comma == std::string::npos)
                    break;
                start = comma + 1;
            }
            d.result = Cook(*cook, d.items, d.itemCount);
            printf("%s\n", Describe(*cook, *reserve, d).c_str());
            for (CritBonus b : {CritBonus::Life, CritBonus::Vitality, CritBonus::Time})
            {
                if (!(d.result.critBonuses & static_cast<uint8_t>(b)))
                    continue;
                FoundDish c = d;
                c.result = Cook(*cook, d.items, d.itemCount, b);
                printf("   if critical: %s\n", Describe(*cook, *reserve, c).c_str());
            }
        }
        return 0;
    }

    SaveFile save;
    if (!save.Load(reinterpret_cast<const uint8_t*>(saveBytes.data()), saveBytes.size()))
    {
        fprintf(stderr, "not a game_data.sav\n");
        return 1;
    }
    std::vector<PouchItem> pouch(PouchSize);
    const int pouchCount = save.ReadPouch(pouch.data(), PouchSize);
    int dishes = 0;
    for (int i = 0; i < pouchCount; i++)
        dishes += strncmp(pouch[i].name, "Item_Cook_", 10) == 0;

    std::unique_ptr<ReservePlan> plan(new ReservePlan());
    PlanReserve(*cook, *reserve, save, pouch.data(), pouchCount, *plan);
    // The armor in the pouch piece by piece (the detail of the Reserve tab) adds up to the two tiers of that armor.
    int mismatches = 0;
    for (int i = 0; i < plan->materialCount; i++)
    {
        const MaterialPlan& m = plan->materials[i];
        int32_t sum = 0;
        for (int k = 0; k < plan->pieceNeedCount; k++)
            if (plan->pieceNeeds[k].item == m.item)
                sum += plan->pieceNeeds[k].amount;
        mismatches += sum != m.need[OwnedReachable] + m.need[OwnedNextFairy];
    }
    printf("piece needs: %d (piece, material) pairs, %d materials whose pieces do not add up\n", plan->pieceNeedCount,
           mismatches);

    printf("== Save\n");
    printf("rupees %d, pouch %d slots (%d dishes), Great Fairies %d of 4, armor pieces %d\n", plan->rupees, pouchCount,
           dishes, plan->fairies, plan->ownedArmorPieces);

    printf("\n== Reserve (T0 your armor up to %d stars, T1 the stars beyond and closed fountains; hidden: T2 armor you "
           "do not have, T3 Ancient Oven, T4 side quests)\n", plan->fairies);
    printf("rupees: T0 %d, T1 %d, T2 %d, T3 %d, T4 %d (have %d)\n", plan->rupeesNeeded[0], plan->rupeesNeeded[1],
           plan->rupeesNeeded[2], plan->rupeesNeeded[3], plan->rupeesNeeded[4], plan->rupees);
    printf("upgrades left on your armor: %d within the fairies, %d beyond; hidden: %d armor pieces (%d for sale), %d "
           "Ancient Oven items, %d quests (%d with an unknown amount)\n", plan->ownedUpgradesLeft[0],
           plan->ownedUpgradesLeft[1], plan->notOwnedPieces, plan->notOwnedForSale, plan->craftedItems,
           plan->pendingQuests, plan->questsWithUnknownCount);
    printf("%-28s %5s %5s %5s %5s %5s %5s %6s\n", "material", "have", "T0", "T1", "T2", "T3", "T4", "free");
    int hiddenNames = 0;
    for (int i = 0; i < plan->materialCount; i++)
    {
        const MaterialPlan& m = plan->materials[i];
        if (m.Needed(AllTiers) == 0)
            continue;
        const bool showName = reveal || m.everObtained;
        if (!showName)
            hiddenNames++;
        char name[64];
        snprintf(name, sizeof(name), "%s", showName ? NameOf(*cook, *reserve, m.item) : "(not found yet)");
        printf("%-28s %5d %5d %5d %5d %5d %5d %6d%s\n", name, m.have, m.need[0], m.need[1], m.need[2], m.need[3],
               m.need[4], m.Free(AllTiers), m.have < m.Needed(AllTiers) ? "  short" : "");
    }
    if (reveal)
    {
        printf("\n-- revealed: quests pending\n");
        for (int i = 0; i < reserve->questCount; i++)
        {
            const QuestNeed& q = reserve->quests[i];
            bool finished = false;
            save.GetBool(q.flag, finished);
            if (!finished)
                printf("  %s: %s x%d%s\n", q.title, NameOf(*cook, *reserve, q.item), q.count, q.countKnown ? "" : "?");
        }
    }

    // Stock for cooking: free after every tier, or the whole pouch.
    std::vector<StockItem> stock;
    for (int i = 0; i < plan->materialCount; i++)
    {
        const MaterialPlan& m = plan->materials[i];
        StockItem s;
        s.hash = m.item;
        s.count = wholePouch ? m.have : m.Free(mask);
        if (s.count > 0)
            stock.push_back(s);
    }

    printf("\n== Best dishes (%s)\n", wholePouch ? "whole pouch" : "free after the reserve");
    const Effect effects[] = {AttackUp, DefenseUp, MovingSpeed, Quietness, ResistHot, ResistCold, ResistElectric,
                              Fireproof, LifeMaxUp, GutsRecover, ExGutsMaxUp};
    int evaluated = 0;
    for (Effect e : effects)
    {
        const SearchResult r = FindBestForEffect(*cook, stock.data(), static_cast<int>(stock.size()), e, ranking);
        evaluated += r.evaluated;
        printf("%s:\n", EffectLabel(e));
        if (!r.found)
        {
            printf("  nothing to cook with what is free\n");
            continue;
        }
        printf("  best:     %s\n", Describe(*cook, *reserve, r.best).c_str());
        if (r.cheapest.itemCount && r.cheapest.cost < r.best.cost)
            printf("  cheapest: %s\n", Describe(*cook, *reserve, r.cheapest).c_str());
    }
    {
        const SearchResult r = FindMostHearts(*cook, stock.data(), static_cast<int>(stock.size()));
        evaluated += r.evaluated;
        printf("Hearts:\n");
        if (r.found)
        {
            printf("  best:     %s\n", Describe(*cook, *reserve, r.best).c_str());
            if (r.cheapest.itemCount && r.cheapest.cost < r.best.cost)
                printf("  cheapest: %s\n", Describe(*cook, *reserve, r.cheapest).c_str());
        }
    }
    printf("(%d combinations cooked)\n", evaluated);

    std::vector<StockItem> free;
    for (int i = 0; i < plan->materialCount; i++)
    {
        StockItem s;
        s.hash = plan->materials[i].item;
        s.count = plan->materials[i].Free(AllTiers);
        if (s.count > 0)
            free.push_back(s);
    }
    std::unique_ptr<SalePlan> sale(new SalePlan());
    PlanSale(*cook, free.data(), static_cast<int>(free.size()), 60 - dishes, *sale);
    printf("\n== Sale (free after the reserve)\n");
    printf("elixirs: %d pots, %d rupees (the same items raw: %d); monster parts left with no critter: %d (raw %d)\n",
           sale->potCount, sale->cookedTotal, sale->cookedRawTotal, sale->partsLeft, sale->partsLeftRaw);
    for (int i = 0; i < sale->potCount && i < 6; i++)
    {
        const SalePot& p = sale->pots[i];
        printf("  %5d (raw %4d) ", p.price, p.rawPrice);
        for (int k = 0; k < p.itemCount; k++)
            printf("%s%s", k ? " + " : "", NameOf(*cook, *reserve, p.items[k]));
        printf("\n");
    }
    printf("sell as they are: %d rupees\n", sale->rawTotal);
    for (int i = 0; i < sale->rawCount && i < 10; i++)
        printf("  %5d  %s x%d\n", sale->raw[i].price, NameOf(*cook, *reserve, sale->raw[i].item), sale->raw[i].count);
    if (hiddenNames)
        printf("\n(%d materials you have not found yet are not named; --reveal names them)\n", hiddenNames);
    return 0;
}
