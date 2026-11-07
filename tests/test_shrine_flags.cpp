#include <cassert>

#include "ShrineFlags.h"

int main()
{
    // Values from the hash list of marcrobledo/savegame-editors (zelda-botw.hashes.csv).
    assert(ShrineFlags::Crc32("Location_Dungeon107") == 12786689u);
    assert(ShrineFlags::Crc32("Clear_Dungeon107") == 1745826544u);
    assert(ShrineFlags::Crc32("CompleteTreasure_Dungeon107") == 3425979062u);

    ShrineFlags::Hashes hashes;
    assert(ShrineFlags::FromLocationHash(12786689u, hashes));
    assert(hashes.clear == 1745826544u);
    assert(hashes.allChests == 3425979062u);

    // First DLC shrine (Location_Dungeon120).
    assert(ShrineFlags::FromLocationHash(2895244064u, hashes));
    assert(hashes.clear == 3294479825u);

    // Location_HyruleCastle is not a shrine.
    assert(!ShrineFlags::FromLocationHash(3414442901u, hashes));
    return 0;
}
