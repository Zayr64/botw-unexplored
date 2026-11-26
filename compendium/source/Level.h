#pragma once

#include <cstddef>
#include <cstdint>

#include "CookData.h"
#include "SaveFile.h"

// The game's hidden level ("EXP"), as ksys::eco::LevelSensor::calculatePoints computes it (zeldaret/botw): the
// kill count of each of the 50 enemies in Ecosystem/LevelSensor.byml times its points, summed in float32; that sum
// times Level2EnemyPower is the enemy level and times Level2WeaponPower the weapon level. Each enemy family's ranks
// appear from a level up (the "value" of each rank); the map can still hold a rank back, so the rank shown is the
// highest the level allows, not a promise for every enemy.
namespace Compendium
{
    constexpr int MaxLevelFlags = 64;
    constexpr int MaxSpecies = 8;
    constexpr int MaxRanks = 8;

    struct LevelFlag
    {
        uint32_t flag = 0;            // CRC32 of Defeated_<actor>_Num
        char actor[NameLength] = {}; // the enemy, e.g. Enemy_Bokoblin_Senior
        float point = 0.0f;
        int32_t max = 0;              // the most kills that can count (10 for most; see ReachableKills); 0 if unknown
    };

    // The most kills of `actor` that can ever count, from the limit of its flag in the game's data (MaxValue, 10 for
    // most enemies, 2 for the Blights). The bosses fought once stay at 1 although their flag allows 10: the counter of
    // Monk Maz Koshia only goes up while it is 0 and that of Dark Beast Ganon only the first time it is beaten
    // (zeldamods.org, "Difficulty scaling"), and Master Kohga is placed once and never revived by the Blood Moon.
    // The overworld bosses (Molduga, Molduking...) are revived by it, so their 10 can be reached.
    int32_t ReachableKills(const char* actor, int32_t flagMax);

    struct Rank
    {
        char actor[NameLength] = {};
        float value = 0.0f; // the enemy level from which this rank appears
    };

    struct Species
    {
        char name[32] = {};
        int rankCount = 0;
        Rank ranks[MaxRanks];
    };

    class LevelData
    {
    public:
        // Reads the LEVELSET, LEVELFLAG and LEVELSPECIES lines of cook-data.txt.
        bool Load(const char* text, size_t size, char* error, size_t errorSize);

        float enemyPower = 0.0f;
        float weaponPower = 0.0f;
        int flagCount = 0;
        LevelFlag flags[MaxLevelFlags];
        int speciesCount = 0;
        Species species[MaxSpecies];
    };

    struct LevelFlagState
    {
        int32_t kills = 0;
        float total = 0.0f; // point x kills
    };

    struct SpeciesState
    {
        int current = -1; // highest rank the enemy level reaches, -1 for none
        bool currentKnown = false; // the player has defeated an enemy of that rank (its name can be shown)
        int knownRank = -1;        // the lowest rank of the family the player has defeated, -1 for none
        bool nextKnown = false;
    };

    struct LevelPlan
    {
        float points = 0.0f;
        float maxPoints = 0.0f;  // with every counter full
        float enemyLevel = 0.0f;
        float weaponLevel = 0.0f;
        int defeatedKinds = 0; // flags with at least one kill
        LevelFlagState flags[MaxLevelFlags];
        SpeciesState species[MaxSpecies];
    };

    // Kills of an enemy as the save counts them: Defeated_<actor>_Num (a bool for the ones counted once).
    int32_t Kills(const SaveFile& save, const char* actor);

    void PlanLevel(const LevelData& data, const SaveFile& save, LevelPlan& out);
}
