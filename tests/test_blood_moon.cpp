#include <cassert>
#include <cstdio>
#include <cstring>

#include "BloodMoon.h"
#include "ShrineFlags.h"

namespace
{
    uint32_t Bits(float value)
    {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    BloodMoon::Input Make(float timer, float time, bool bloodyDay = false, bool leftPlateau = true, int32_t reserve = 0)
    {
        BloodMoon::Input in;
        BloodMoon::Take(ShrineFlags::Crc32("WM_BloodyMoonTimer"), Bits(timer), in);
        BloodMoon::Take(ShrineFlags::Crc32("WM_Time"), Bits(time), in);
        BloodMoon::Take(ShrineFlags::Crc32("WM_BloodyDay"), bloodyDay ? 1 : 0, in);
        BloodMoon::Take(ShrineFlags::Crc32("FirstTouchdown"), leftPlateau ? 1 : 0, in);
        uint32_t r;
        std::memcpy(&r, &reserve, 4);
        BloodMoon::Take(ShrineFlags::Crc32("WM_bloodyEndReserveTimer"), r, in);
        return in;
    }
}

int main()
{
    using BloodMoon::Status;

    // Nothing known: no estimate.
    assert(BloodMoon::Next(BloodMoon::Input()).status == Status::Unknown);
    // Other records are not taken.
    {
        BloodMoon::Input in;
        BloodMoon::Take(ShrineFlags::Crc32("CurrentRupee"), 1249, in);
        assert(!in.hasTimer && !in.hasTime);
    }

    // The save of 17/11: timer 665.919, 07:21 (305.372 is 20:21), plateau left.
    {
        const BloodMoon::Estimate e = BloodMoon::Next(Make(665.919f, 305.372f));
        assert(e.status == Status::Counting);
        assert(e.hour == 20 && e.minute == 21);
        // 1854.081 units to 2520, reached at (305.372 + 1854.081) mod 360 = 359.453: 0.547 to that midnight, then a
        // day: 2215.081 units x 4 s = 8860.3 s = 147.7 min, rounded up.
        assert(e.minutesOfPlay == 148);
    }

    // Already over 7 days (the midnight has not come yet): to the next midnight, then a day.
    {
        const BloodMoon::Estimate e = BloodMoon::Next(Make(2600.0f, 180.0f)); // noon
        assert(e.status == Status::Counting && e.hour == 12 && e.minute == 0);
        assert(e.minutesOfPlay == (180 + 360) * 4 / 60); // 36 min
    }

    // Tonight: WM_BloodyDay set, no reserve counting: at the next midnight.
    {
        const BloodMoon::Estimate e = BloodMoon::Next(Make(10.0f, 270.0f, true)); // 18:00
        assert(e.status == Status::Tonight && e.minutesOfPlay == 6); // 90 units = 360 s
    }

    // Right after a Blood Moon WM_BloodyDay is still set while the reserve counts down: not tonight again.
    {
        const BloodMoon::Estimate e = BloodMoon::Next(Make(0.5f, 0.5f, true, true, 120));
        assert(e.status == Status::Counting && e.minutesOfPlay > 7 * 24);
    }

    // Before leaving the Great Plateau: no Blood Moon.
    assert(BloodMoon::Next(Make(3000.0f, 100.0f, false, false)).status == Status::NotStarted);

    std::printf("blood moon: ok\n");
    return 0;
}
