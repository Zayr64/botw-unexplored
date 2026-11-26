// The hidden level ("EXP"): the most kills each enemy can count, and the points with a made-up save.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#include "CookData.h"
#include "Level.h"
#include "SaveFile.h"

using namespace Compendium;

namespace
{
    void PutU32(std::vector<uint8_t>& out, uint32_t value)
    {
        for (int i = 0; i < 4; i++)
            out.push_back(static_cast<uint8_t>(value >> (8 * i)));
    }
}

int main()
{
    // The flag's own limit for most, 1 for the bosses fought once (their flags allow 10)
    assert(ReachableKills("Enemy_Bokoblin_Senior", 10) == 10);
    assert(ReachableKills("Enemy_Sandworm", 10) == 10);  // the Moldugas come back with the Blood Moon
    assert(ReachableKills("Enemy_SandwormR", 10) == 10); // so does the Molduking
    assert(ReachableKills("Enemy_SiteBoss_Bow", 2) == 2);
    assert(ReachableKills("Enemy_Assassin_Senior", 10) == 1);
    assert(ReachableKills("Enemy_GanonBeast", 10) == 1);
    assert(ReachableKills("Priest_Boss_Normal", 10) == 1);
    assert(ReachableKills("Enemy_Assassin_Senior", 0) == 1);

    const char text[] =
        "LEVELSET 3c656042 3c449ba6\n"
        "LEVELFLAG cde84ffc Enemy_Bokoblin_Senior 41700000 10\n"
        "LEVELFLAG 2d43d705 Enemy_Sandworm 42480000 10\n"
        "LEVELFLAG 40980032 Enemy_Assassin_Senior 42c80000 10\n"
        "LEVELFLAG 6ecd67f2 Enemy_GanonBeast 44480000 10\n"
        "LEVELFLAG 61e9c3d2 Priest_Boss_Normal 43fa0000 10\n"
        "LEVELSPECIES Bokoblin 1 Enemy_Bokoblin_Senior 00000000\n";
    static LevelData data;
    char error[128] = {};
    assert(data.Load(text, sizeof(text) - 1, error, sizeof(error)));
    assert(data.flagCount == 5);
    assert(data.flags[0].max == 10 && data.flags[1].max == 10);
    assert(data.flags[2].max == 1 && data.flags[3].max == 1 && data.flags[4].max == 1);
    assert(data.flags[2].flag == Crc32("Defeated_Enemy_Assassin_Senior_Num"));

    // A save with 4 Moldugas, Master Kohga and Dark Beast Ganon once, no Monk Maz Koshia: records sorted by hash
    // after a 12-byte header, and enough of them for the loader.
    std::vector<std::pair<uint32_t, uint32_t>> records = {
        {Crc32("Defeated_Enemy_Sandworm_Num"), 4}, {Crc32("Defeated_Enemy_Assassin_Senior_Num"), 1},
        {Crc32("Defeated_Enemy_GanonBeast_Num"), 1}};
    for (uint32_t i = 0; i < 40; i++)
        records.push_back({0x10000000u + i, 0});
    std::sort(records.begin(), records.end());
    std::vector<uint8_t> bytes(12, 0);
    for (const auto& r : records)
    {
        PutU32(bytes, r.first);
        PutU32(bytes, r.second);
    }
    PutU32(bytes, 0xffffffffu);
    SaveFile save;
    assert(save.Load(bytes.data(), bytes.size()));

    static LevelPlan plan;
    PlanLevel(data, save, plan);
    assert(plan.flags[1].kills == 4 && plan.flags[2].kills == 1 && plan.flags[3].kills == 1 && plan.flags[4].kills == 0);
    assert(plan.points == 4 * 50.0f + 100.0f + 800.0f);
    // With every counter full: 10 Bokoblins and Moldugas, each boss once
    assert(plan.maxPoints == 10 * 15.0f + 10 * 50.0f + 100.0f + 800.0f + 500.0f);
    assert(plan.defeatedKinds == 3);
    return 0;
}
