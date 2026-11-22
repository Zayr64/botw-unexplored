#pragma once

#include <cstddef>
#include <cstdint>

#include "CookData.h"
#include "SaveFile.h"

// How much of each material and how many rupees to hold back, and so what is free to cook or sell.
//
// Needs come in tiers, so the app can show them apart and the player can leave some out:
//   OwnedReachable  upgrades of the armor in the pouch, up to the stars the restored Great Fairies allow (visible)
//   OwnedNextFairy  the remaining stars of that armor, plus the rupees of the fountains still closed (visible)
//   ArmorNotOwned   armor the pouch does not have: its price where a shop sells it, and every upgrade (hidden)
//   Crafted         Ancient Oven items never obtained (hidden)
//   Quests          side quests not finished that take items (hidden)
// "Hidden" follows the user's choice of 22/11: it counts, but what it is for is only shown on request.
namespace Compendium
{
    enum Tier : int
    {
        OwnedReachable = 0,
        OwnedNextFairy = 1,
        ArmorNotOwned = 2,
        Crafted = 3,
        Quests = 4,
        TierCount = 5,
    };

    constexpr int MaxArmors = 480;
    constexpr int MaxCrafts = 16;
    constexpr int MaxShopItems = 320;
    constexpr int MaxLabels = 720;
    constexpr int MaxQuestNeeds = 64;
    constexpr int MaxRecipeItems = 6;
    constexpr int MaxMaterials = 320;
    constexpr int FountainCount = 4;

    struct ArmorEntry
    {
        uint32_t hash = 0;
        char name[NameLength] = {};
        uint32_t next = 0;       // the actor one star up, 0 at the top
        int32_t star = 1;        // armorStarNum: 1 = no star
        int32_t buyingPrice = 0;
        int32_t creatingPrice = 0;
        bool amiibo = false;
        int ingredientCount = 0; // what makes this actor (the piece one star down comes first)
        uint32_t ingredients[MaxRecipeItems] = {};
        int32_t amounts[MaxRecipeItems] = {};
    };

    struct CraftEntry
    {
        uint32_t hash = 0;
        char name[NameLength] = {};
        int32_t price = 0;
        int ingredientCount = 0;
        uint32_t ingredients[MaxRecipeItems] = {};
        int32_t amounts[MaxRecipeItems] = {};
    };

    struct QuestNeed
    {
        uint32_t flag = 0;       // the quest's finish flag
        uint32_t item = 0;
        int32_t count = 1;
        bool countKnown = true;  // false when the game's text gives no number (counted as 1)
        char title[64] = {};
    };

    struct ShopItem
    {
        uint32_t hash = 0;
        bool crafted = false;    // made at the Ancient Oven, not bought
    };

    class ReserveData
    {
    public:
        // Reads the ARMOR, CRAFT, SHOP, ITEM, LABEL and QUEST lines of cook-data.txt (the other lines are CookData's).
        bool Load(const char* text, size_t size, char* error, size_t errorSize);

        const ArmorEntry* FindArmor(uint32_t hash) const;
        const ShopItem* FindShopItem(uint32_t hash) const;
        const char* Label(uint32_t hash) const; // English name of an armor, crafted item or material; "" if none

        int armorCount = 0;
        ArmorEntry armors[MaxArmors]; // sorted by hash
        int craftCount = 0;
        CraftEntry crafts[MaxCrafts];
        int shopItemCount = 0;
        ShopItem shopItems[MaxShopItems]; // sorted by hash
        int questCount = 0;
        QuestNeed quests[MaxQuestNeeds];
        int labelCount = 0;
        struct LabelEntry
        {
            uint32_t hash = 0;
            char text[NameLength] = {};
        } labels[MaxLabels]; // sorted by hash
    };

    struct MaterialPlan
    {
        uint32_t item = 0;
        int32_t have = 0;
        int32_t need[TierCount] = {};
        bool everObtained = false; // in the pouch now, or its IsGet_ flag is set: its name can be shown

        int32_t Needed(uint8_t tierMask) const;
        int32_t Free(uint8_t tierMask) const;
    };

    struct ReservePlan
    {
        int fairies = 0;            // Great Fairy fountains restored (0 to 4)
        int32_t rupees = 0;
        int32_t rupeesNeeded[TierCount] = {};
        int ownedArmorPieces = 0;
        int ownedUpgradesLeft[2] = {}; // upgrades left within / beyond the fairies' limit
        int notOwnedPieces = 0;       // armor pieces counted in ArmorNotOwned
        int notOwnedForSale = 0;      // of those, sold in a shop or at the Ancient Oven
        int craftedItems = 0;
        int pendingQuests = 0;
        int questsWithUnknownCount = 0;
        int materialCount = 0;
        MaterialPlan materials[MaxMaterials];

        const MaterialPlan* Find(uint32_t item) const;
        int32_t RupeesNeeded(uint8_t tierMask) const;
    };

    constexpr uint8_t AllTiers = (1u << TierCount) - 1;

    // `pouch`: SaveFile::ReadPouch. The save is read again for rupees, fountains, quests and IsGet_ flags; `cook` gives
    // the actor names of the materials, for their IsGet_ flags.
    void PlanReserve(const CookData& cook, const ReserveData& data, const SaveFile& save, const PouchItem* pouch,
                     int pouchCount, ReservePlan& out);
}
