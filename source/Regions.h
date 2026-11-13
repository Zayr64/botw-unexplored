#pragma once

#include <cstdint>

// The fifteen regions of the map, the areas each tower reveals. tools/gen_regions.py works out the region of
// every object the app shows, so the app only carries that table and a bounding box per region.
namespace Data
{
    struct RegionEntry
    {
        uint32_t hash;  // the completion hash of an object
        uint8_t region; // 0 to 14, in the order of the towers (MapTower_01 to _15)
    };

    struct RegionBounds
    {
        float minX, minZ, maxX, maxZ; // game coordinates
    };

    extern const RegionEntry RegionEntries[]; // sorted by hash
    extern const int RegionEntriesCount;
    extern const RegionBounds RegionBoundsList[];
}

namespace Regions
{
    constexpr int Count = 15;
    constexpr int None = -1;

    // The region of the object with this completion hash, or None for one that is not in the table.
    int Of(uint32_t hash);
    // The name of a region, that of its tower: "Hebra", "Tabantha"...
    const char* Name(int region);
    // The visit flag of the tower of a region (Location_MapTowerNN), the hash the app knows the tower by.
    uint32_t TowerLocationHash(int region);
    const Data::RegionBounds& Bounds(int region);
}
