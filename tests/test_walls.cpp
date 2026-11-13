#include <cassert>

#include "Walls.h"

int main()
{
    // 140 walls, each with its own flag, inside the map (about -5000..5000 by -4000..4000).
    assert(Data::WallsCount == 140);
    bool found = false;
    for (int i = 0; i < Data::WallsCount; ++i)
    {
        const Data::Wall& wall = Data::Walls[i];
        assert(wall.hash != 0 && (i == 0 || Data::Walls[i - 1].hash < wall.hash));
        assert(wall.x > -5100.0f && wall.x < 5100.0f && wall.y > -4100.0f && wall.y < 4100.0f);
        // MainField_WallCrack_382719991, from the hash list of marcrobledo/savegame-editors (zelda-botw.hashes.csv).
        if (wall.hash == 2536340063u)
            found = wall.x < -4137.0f && wall.x > -4139.0f && wall.y < -3734.0f && wall.y > -3735.0f;
    }
    assert(found);
    return 0;
}
