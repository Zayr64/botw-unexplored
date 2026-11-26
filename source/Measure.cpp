#include "Measure.h"

#include <chrono>
#include <cstdio>

#include "FreeMemory.h"
#include "Graphics/Texture2D.h"
#include "Log.h"

namespace
{
    using Clock = std::chrono::steady_clock;
    Clock::time_point s_Start = Clock::now();
    Clock::time_point s_Last = s_Start;

    std::string Megabytes(uint64_t bytes)
    {
        char text[32];
        std::snprintf(text, sizeof(text), "%.1f MB", bytes / (1024.0 * 1024.0));
        return text;
    }
}

void Measure::Start()
{
    s_Start = s_Last = Clock::now();
}

void Measure::Step(const std::string& step)
{
    const Clock::time_point now = Clock::now();
    const long long total = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_Start).count();
    const long long since = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_Last).count();
    s_Last = now;
    const uint64_t free = FreeMemory::Bytes();
    Log("MEASURE " + step + ": " + std::to_string(total) + " ms (+" + std::to_string(since) + "), heap free " +
        (free == FreeMemory::Unknown ? std::string("unknown") : Megabytes(free)) + ", images " +
        Megabytes(Texture2D::LiveBytes()));
}
