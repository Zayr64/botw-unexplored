#pragma once

#include <cstdint>

// When the next Blood Moon comes, from the flags of the world's clock (the same in the save and in the running game).
//
// The rule, from ksys::world::TimeMgr in the zeldaret/botw decomp (worldTimeMgr.cpp, handleNewDay and calc_):
// WM_Time is the time of day (0 to 360 for 24 hours) and WM_BloodyMoonTimer counts in the same units while the world's
// time passes, one unit every 4 seconds of play. At a midnight, once Link has left the Great Plateau (FirstTouchdown)
// and the timer is over 7 days (2520), the timer starts again and WM_BloodyDay is set; the Blood Moon is at the next
// midnight. When something forbids it then, the game tries again the midnight after.
namespace BloodMoon
{
    struct Input
    {
        bool hasTimer = false;
        bool hasTime = false;
        float timer = 0.0f;       // WM_BloodyMoonTimer
        float time = 0.0f;        // WM_Time
        bool bloodyDay = false;   // WM_BloodyDay: it is tonight
        bool leftPlateau = false; // FirstTouchdown
        int32_t endReserve = 0;   // WM_bloodyEndReserveTimer: set right after one, while the old one ends
    };

    // Takes the record when it is one of the clock's flags. `value` is the record's 4 bytes (a float's bits for f32).
    void Take(uint32_t hash, uint32_t value, Input& input);

    enum class Status
    {
        Unknown,    // the flags are not there
        NotStarted, // Link has not left the Great Plateau: no Blood Moon yet
        Counting,
        Tonight,    // WM_BloodyDay: at the next midnight
    };

    struct Estimate
    {
        Status status = Status::Unknown;
        int minutesOfPlay = 0; // until the Blood Moon, rounded up, counting only time that passes in the world
        int hour = 0;          // the world's clock now
        int minute = 0;
    };

    Estimate Next(const Input& input);
}
