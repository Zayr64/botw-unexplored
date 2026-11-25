#include "BloodMoon.h"

#include <cmath>
#include <cstring>

#include "ShrineFlags.h"

namespace
{
    constexpr float Day = 360.0f;             // timeToFloat(24, 0)
    constexpr float Threshold = 7.0f * Day;   // mBloodMoonTimer > 7_days
    constexpr float SecondsPerUnit = 4.0f;    // DefaultTimeStep: 1/4 unit a second at 30 frames a second

    float AsFloat(uint32_t bits)
    {
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    int MinutesOf(float units)
    {
        return static_cast<int>(std::ceil(units * SecondsPerUnit / 60.0f));
    }
}

void BloodMoon::Take(uint32_t hash, uint32_t value, Input& input)
{
    static const uint32_t timer = ShrineFlags::Crc32("WM_BloodyMoonTimer");
    static const uint32_t time = ShrineFlags::Crc32("WM_Time");
    static const uint32_t bloodyDay = ShrineFlags::Crc32("WM_BloodyDay");
    static const uint32_t leftPlateau = ShrineFlags::Crc32("FirstTouchdown");
    static const uint32_t endReserve = ShrineFlags::Crc32("WM_bloodyEndReserveTimer");

    if (hash == timer)
    {
        input.timer = AsFloat(value);
        input.hasTimer = true;
    }
    else if (hash == time)
    {
        input.time = AsFloat(value);
        input.hasTime = true;
    }
    else if (hash == bloodyDay)
        input.bloodyDay = value != 0;
    else if (hash == leftPlateau)
        input.leftPlateau = value != 0;
    else if (hash == endReserve)
        std::memcpy(&input.endReserve, &value, sizeof(input.endReserve));
}

BloodMoon::Estimate BloodMoon::Next(const Input& input)
{
    Estimate estimate;
    if (!input.hasTimer || !input.hasTime || !std::isfinite(input.timer) || !std::isfinite(input.time))
        return estimate;

    float time = std::fmod(input.time, Day);
    if (time < 0.0f)
        time += Day;
    const int clock = static_cast<int>(time * 4.0f) % (24 * 60); // 4 minutes of the world's clock per unit
    estimate.hour = clock / 60;
    estimate.minute = clock % 60;

    if (!input.leftPlateau)
    {
        estimate.status = Status::NotStarted;
        return estimate;
    }

    const float toMidnight = Day - time;
    // WM_BloodyDay stays set for a while after a Blood Moon (WM_bloodyEndReserveTimer counts down then): only before it
    // does it mean tonight.
    if (input.bloodyDay && input.endReserve <= 0)
    {
        estimate.status = Status::Tonight;
        estimate.minutesOfPlay = MinutesOf(toMidnight);
        return estimate;
    }

    // Until the timer passes 7 days, then to the first midnight after that (WM_BloodyDay set), then one more day.
    const float toThreshold = input.timer < Threshold ? Threshold - input.timer : 0.0f;
    float toThatMidnight = Day - std::fmod(time + toThreshold, Day);
    if (toThatMidnight >= Day)
        toThatMidnight = 0.0f;
    estimate.status = Status::Counting;
    estimate.minutesOfPlay = MinutesOf(toThreshold + toThatMidnight + Day);
    return estimate;
}
