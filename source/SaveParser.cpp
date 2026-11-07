#include "SaveParser.h"

#include <cmath>
#include <cstring>

namespace
{
    uint32_t ReadU32(const uint8_t* data, size_t offset)
    {
        return static_cast<uint32_t>(data[offset]) |
               (static_cast<uint32_t>(data[offset + 1]) << 8) |
               (static_cast<uint32_t>(data[offset + 2]) << 16) |
               (static_cast<uint32_t>(data[offset + 3]) << 24);
    }

    float AsFloat(uint32_t bits)
    {
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    bool IsPlausiblePosition(const float position[3])
    {
        for (int i = 0; i < 3; ++i)
            if (!std::isfinite(position[i]))
                return false;
        // The map spans about -5000..5000 (x) and -4000..4000 (z).
        return std::fabs(position[0]) <= 6000.0f && std::fabs(position[2]) <= 5000.0f &&
               std::fabs(position[1]) <= 3000.0f && (position[0] != 0.0f || position[2] != 0.0f);
    }
}

bool SaveParser::ParseSaveBuffer(const uint8_t* data, size_t size, ParsedSaveState& result)
{
    if (data == nullptr || size < HeaderSize)
        return false;

    const size_t trailingDataSize = (size - HeaderSize) % RecordSize;
    // Switch game_data.sav files may end with a four-byte trailer. Other partial
    // record sizes cannot be distinguished from corruption and are rejected.
    if (trailingDataSize != 0 && trailingDataSize != SupportedTrailingDataSize)
        return false;

    const size_t recordCount = (size - HeaderSize) / RecordSize;
    if (recordCount < MinimumRecordCount)
        return false;

    ParsedSaveState candidate;
    candidate.records.reserve(recordCount);
    bool foundPlaytime = false;
    size_t nonZeroHashCount = 0;
    int playerPositionParts = 0;

    for (size_t offset = HeaderSize; offset + RecordSize <= size; offset += RecordSize)
    {
        const Record record = {ReadU32(data, offset), ReadU32(data, offset + 4)};
        candidate.records.push_back(record);
        if (record.hash != 0)
            ++nonZeroHashCount;
        if (record.hash == PlaytimeHash && record.value != 0)
        {
            candidate.playtime = record.value;
            foundPlaytime = true;
        }
        if (record.hash == BalladOfHeroesReadyHash)
            candidate.hasDLC = record.value == 1;
        if (record.hash == PlayerSavePosHash && playerPositionParts < 3)
            candidate.playerPosition[playerPositionParts++] = AsFloat(record.value);
    }

    candidate.hasPlayerPosition = playerPositionParts == 3 && IsPlausiblePosition(candidate.playerPosition);

    // A real game_data.sav has a non-empty record set and a non-zero playtime control record.
    if (!foundPlaytime || nonZeroHashCount < MinimumRecordCount)
        return false;

    result = candidate;
    return true;
}
