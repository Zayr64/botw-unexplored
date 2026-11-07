#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace SaveParser
{
    constexpr uint32_t PlaytimeHash = 0x73c29681;
    constexpr uint32_t BalladOfHeroesReadyHash = 1186840637;
    // PlayerSavePos: x, height and z of the last save, stored as three records with the same hash.
    constexpr uint32_t PlayerSavePosHash = 0xa40ba103;
    constexpr size_t HeaderSize = 0x0c;
    constexpr size_t RecordSize = 8;
    constexpr size_t MinimumRecordCount = 32;
    constexpr size_t SupportedTrailingDataSize = 4;

    struct Record
    {
        uint32_t hash = 0;
        uint32_t value = 0;
    };

    struct ParsedSaveState
    {
        uint32_t playtime = 0;
        bool hasDLC = false;
        bool hasPlayerPosition = false;
        float playerPosition[3] = {0.0f, 0.0f, 0.0f};
        std::vector<Record> records;
    };

    bool ParseSaveBuffer(const uint8_t* data, size_t size, ParsedSaveState& result);
}
