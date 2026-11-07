#pragma once

#include <string>

// In-game map completion: every Korok seed, every discovered base-game shrine
// and every visited location count once (900 + 120 + 187 = 1207 entries).
// DLC shrines are not part of this total.
namespace MapCompletion
{
    constexpr int TotalEntries = 1207;

    // Completion in hundredths of a percent, truncated like the game
    // (1206 of 1207 entries is shown as 99.91%, not 99.92%).
    int Hundredths(int foundEntries);

    // "99.91%" style text with the given decimal separator.
    std::string Format(int hundredths, char decimalSeparator);
}
