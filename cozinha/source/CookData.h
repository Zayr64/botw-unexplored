#pragma once

#include <cstddef>
#include <cstdint>

// The game's cooking data (ActorInfo values, Cooking/CookData), read from cook-data.txt.
//
// The file is generated on the PC from a dump of the game (cozinha/tools/gen_cook_data.py) and lives only on the SD
// card. Everything here is fixed-size arrays filled once, with no allocation, so the same code can run in an overlay.
namespace Cozinha
{
    // CookEffectId of the decomp (uking::CookEffectId).
    enum Effect : int32_t
    {
        EffectNone = -1,
        LifeRecover = 1,
        LifeMaxUp = 2,
        ResistHot = 4,
        ResistCold = 5,
        ResistElectric = 6,
        AttackUp = 10,
        DefenseUp = 11,
        Quietness = 12,
        MovingSpeed = 13,
        GutsRecover = 14,
        ExGutsMaxUp = 15,
        Fireproof = 16,
    };
    constexpr int EffectSlots = 17;

    constexpr int MaxActors = 400;  // 317 in 1.6.0
    constexpr int MaxActorTags = 24; // 22
    constexpr int MaxRecipes = 160; // 125
    constexpr int MaxSingleRecipes = 24; // 13
    constexpr int MaxSlots = 5;
    constexpr int MaxSlotHashes = 12; // 7
    constexpr int NameLength = 48;

    struct Actor
    {
        uint32_t hash = 0;
        char name[NameLength] = {};    // actor name, e.g. Item_Fruit_A
        char display[NameLength] = {}; // English name, empty when the game has none
        bool hasSellingPrice = false;
        bool hasBuyingPrice = false;
        int32_t sellingPrice = 0;
        int32_t buyingPrice = 0;
        // The game only uses these when they are above zero, so a missing value is stored as 0.
        int32_t hitPointRecover = 0;
        int32_t effectLevel = 0;
        Effect effectType = EffectNone;
        int32_t spiceTime = 0;
        int32_t spiceHitPoint = 0;
        int32_t spiceMaxHeart = 0;
        int32_t spiceStamina = 0;
        int32_t spiceSuccessRate = 0;
        int tagCount = 0;
        uint32_t tags[MaxActorTags] = {};

        bool HasTag(uint32_t tag) const;
    };

    struct EffectEntry
    {
        int32_t boostTime = 0;
        int32_t max = 0;
        int32_t min = 0;
        float multiplier = 1.0f;
        int32_t ssa = 0;
    };

    // A set of actor names or tags; any one of them fills the slot.
    struct Slot
    {
        int count = 0;
        uint32_t hashes[MaxSlotHashes] = {};
    };

    struct Recipe
    {
        uint32_t result = 0;
        bool hasHitPointBonus = false;
        bool hasTimeBonus = false;
        int32_t hitPointBonus = 0;
        int32_t timeBonus = 0;
        int actorSlotCount = 0;
        Slot actorSlots[MaxSlots];
        int tagSlotCount = 0;
        Slot tagSlots[MaxSlots];
    };

    struct SingleRecipe
    {
        uint32_t result = 0;
        bool hasHitPointBonus = false;
        bool hasTimeBonus = false;
        int32_t hitPointBonus = 0;
        int32_t timeBonus = 0;
        int32_t num = 0;
        Slot actors;
        Slot tags;
    };

    namespace Tags
    {
        uint32_t CookEnemy();
        uint32_t CookSpice();
        uint32_t CookLowPrice();
        uint32_t CookFailure();
        uint32_t CookEMedicine();
        uint32_t CookInsect();
        uint32_t CookOre();
    }

    uint32_t Crc32(const char* text);

    class CookData
    {
    public:
        // Parses the text of cook-data.txt. On failure, returns false and leaves a short reason in `error`.
        bool Load(const char* text, size_t size, char* error, size_t errorSize);

        const Actor* FindActor(uint32_t hash) const;
        const Actor* FindActorByName(const char* name) const;
        // The first actor with this English name (several actors can share one), or nullptr.
        const Actor* FindActorByDisplayName(const char* display) const;

        uint32_t failActor = 0;        // Dubious Food
        uint32_t fairyTonic = 0;
        uint32_t monsterExtract = 0;
        float lifeRecoverMultiplier = 1.0f;
        float failActorLifeRecoverMultiplier = 1.0f;
        int32_t failActorLifeRecover = 4;
        int32_t stoneFoodLifeRecover = 1;
        int32_t critEffectTime = 300;
        float ingredientNumMultipliers[5] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
        int32_t ingredientNumSuccessRates[5] = {0, 5, 10, 15, 20};
        EffectEntry effects[EffectSlots];
        // The word the game puts before the name of a dish with this effect ("Chilly", "Mighty"); empty if none.
        char effectNames[EffectSlots][24] = {};

        int actorCount = 0;
        Actor actors[MaxActors]; // sorted by hash after Load
        int recipeCount = 0;
        Recipe recipes[MaxRecipes];
        int singleRecipeCount = 0;
        SingleRecipe singleRecipes[MaxSingleRecipes];
    };
}
