#include "Regions.h"

#include <algorithm>
#include <array>
#include <cstdio>

#include "ShrineFlags.h"

namespace
{
    // In the order of the towers: MapTower_01 is the Hebra Tower and so on.
    const char* const names[Regions::Count] = {
        "Hebra", "Tabantha", "Gerudo", "Wasteland", "Woodland",
        "Central", "Great Plateau", "Dueling Peaks", "Lake", "Eldin",
        "Akkala", "Lanayru", "Hateno", "Faron", "Ridgeland"
    };
}

int Regions::Of(uint32_t hash)
{
    const Data::RegionEntry* begin = Data::RegionEntries;
    const Data::RegionEntry* end = begin + Data::RegionEntriesCount;
    const Data::RegionEntry* found = std::lower_bound(begin, end, hash,
        [](const Data::RegionEntry& entry, uint32_t value) { return entry.hash < value; });
    return found != end && found->hash == hash ? found->region : None;
}

const char* Regions::Name(int region)
{
    return region >= 0 && region < Count ? names[region] : "";
}

uint32_t Regions::TowerLocationHash(int region)
{
    static const std::array<uint32_t, Count> hashes = []()
    {
        std::array<uint32_t, Count> result;
        char name[32];
        for (int i = 0; i < Count; ++i)
        {
            std::snprintf(name, sizeof(name), "Location_MapTower%02d", i + 1);
            result[i] = ShrineFlags::Crc32(name);
        }
        return result;
    }();
    return region >= 0 && region < Count ? hashes[region] : 0;
}

const Data::RegionBounds& Regions::Bounds(int region)
{
    static const Data::RegionBounds whole = {-5000.0f, -4000.0f, 5000.0f, 4000.0f};
    return region >= 0 && region < Count ? Data::RegionBoundsList[region] : whole;
}
