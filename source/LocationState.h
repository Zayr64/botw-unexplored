#pragma once

#include <cstdint>

// What a few locations have left to do after they are visited: a tower is activated and a great fairy is
// awakened. Each has a flag of its own in the save, besides the one of the visit (Location_<name>):
// MapTower_NN for the tower of Location_MapTowerNN, and RevivalGreatFairyN for the four fountains
// (Location_WeaponCureSpring, _02, _03, _04) and the spring of the horse god (Location_MaronSpring, N = 5).
namespace LocationState
{
    enum class Kind : uint8_t
    {
        Tower,
        GreatFairy
    };

    struct Entry
    {
        uint32_t locationHash; // CRC32 of the visit flag, the hash the app knows the location by
        uint32_t flagHash;     // CRC32 of the flag set when it is activated or awakened
        Kind kind;
    };

    constexpr int Count = 20; // 15 towers, 4 fountains and the spring

    const Entry* Table();

    // The entry of a location, or nullptr for the locations that are only visited.
    const Entry* ForLocation(uint32_t locationHash);
    // The entry whose state flag this is, or nullptr.
    const Entry* ForFlag(uint32_t flagHash);
}
