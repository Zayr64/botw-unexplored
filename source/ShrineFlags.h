#pragma once

#include <cstdint>

// Save flags of a shrine besides its discovery flag (Location_DungeonNNN, the one
// used by the map completion): Clear_DungeonNNN is set when the shrine is
// completed and CompleteTreasure_DungeonNNN when all of its chests are opened.
namespace ShrineFlags
{
    struct Hashes
    {
        uint32_t clear = 0;
        uint32_t allChests = 0;
    };

    // CRC32 (zlib polynomial), the hash the game uses for flag names.
    uint32_t Crc32(const char* text);

    // Returns false when locationHash is not Location_DungeonNNN for NNN in 000-199.
    bool FromLocationHash(uint32_t locationHash, Hashes& result);
}
