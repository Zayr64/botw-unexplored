#include <cassert>
#include <cstring>

#include "Regions.h"

int main()
{
    // Every object of the app has a region; the table is sorted by hash.
    assert(Data::RegionEntriesCount == 2759);
    int perRegion[Regions::Count] = {};
    for (int i = 0; i < Data::RegionEntriesCount; ++i)
    {
        const Data::RegionEntry& entry = Data::RegionEntries[i];
        assert(i == 0 || Data::RegionEntries[i - 1].hash < entry.hash);
        assert(entry.region < Regions::Count);
        ++perRegion[entry.region];
        assert(Regions::Of(entry.hash) == entry.region);
    }
    for (int region = 0; region < Regions::Count; ++region)
    {
        assert(perRegion[region] > 50);
        // A region has the number of its tower, and the tower stands in it.
        assert(Regions::Of(Regions::TowerLocationHash(region)) == region);
        assert(Regions::Name(region)[0] != 0);
        const Data::RegionBounds& bounds = Regions::Bounds(region);
        assert(bounds.minX < bounds.maxX && bounds.minZ < bounds.maxZ);
        assert(bounds.minX >= -5000.0f && bounds.maxX <= 5000.0f && bounds.minZ >= -4001.0f && bounds.maxZ <= 4001.0f);
    }

    // Values from the hash list of marcrobledo/savegame-editors (zelda-botw.hashes.csv).
    // Location_MapTower07 is the Great Plateau Tower, Location_MapTower01 the Hebra Tower.
    assert(Regions::TowerLocationHash(6) == 2845242333u && std::strcmp(Regions::Name(6), "Great Plateau") == 0);
    assert(Regions::TowerLocationHash(0) == 1089820392u && std::strcmp(Regions::Name(0), "Hebra") == 0);
    assert(std::strcmp(Regions::Name(14), "Ridgeland") == 0);
    // The chest MainField_TBox_Field_Iron_628080506 is in the Central region; the wall
    // MainField_WallCrack_382719991, in the far northwest, in Hebra.
    assert(Regions::Of(2175256561u) == 5 && Regions::Of(2536340063u) == 0);

    // Not an object of the app, not a region.
    assert(Regions::Of(1u) == Regions::None);
    assert(Regions::Name(Regions::None)[0] == 0 && Regions::Name(15)[0] == 0);
    assert(Regions::TowerLocationHash(15) == 0);
    const Data::RegionBounds& whole = Regions::Bounds(Regions::None);
    assert(whole.minX == -5000.0f && whole.maxX == 5000.0f);
    return 0;
}
