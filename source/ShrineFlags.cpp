#include "ShrineFlags.h"

#include <cstdio>

uint32_t ShrineFlags::Crc32(const char* text)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p != 0; ++p)
    {
        crc ^= *p;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

bool ShrineFlags::FromLocationHash(uint32_t locationHash, Hashes& result)
{
    char name[48];
    for (int number = 0; number < 200; ++number)
    {
        std::snprintf(name, sizeof(name), "Location_Dungeon%03d", number);
        if (Crc32(name) != locationHash)
            continue;

        std::snprintf(name, sizeof(name), "Clear_Dungeon%03d", number);
        result.clear = Crc32(name);
        std::snprintf(name, sizeof(name), "CompleteTreasure_Dungeon%03d", number);
        result.allChests = Crc32(name);
        return true;
    }
    return false;
}
