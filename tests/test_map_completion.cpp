#include "MapCompletion.h"

#include <cassert>

int main()
{
    assert(MapCompletion::TotalEntries == 900 + 120 + 187);

    assert(MapCompletion::Hundredths(0) == 0);
    assert(MapCompletion::Hundredths(-5) == 0);
    assert(MapCompletion::Hundredths(1207) == 10000);
    assert(MapCompletion::Hundredths(1300) == 10000);
    // One missing entry is shown by the game as 99.91%.
    assert(MapCompletion::Hundredths(1206) == 9991);
    assert(MapCompletion::Hundredths(1) == 8);
    assert(MapCompletion::Hundredths(604) == 5004);

    assert(MapCompletion::Format(9991, '.') == "99.91%");
    assert(MapCompletion::Format(9991, ',') == "99,91%");
    assert(MapCompletion::Format(10000, '.') == "100.00%");
    assert(MapCompletion::Format(8, '.') == "0.08%");
    assert(MapCompletion::Format(5004, ',') == "50,04%");
    assert(MapCompletion::Format(-1, '.') == "0.00%");
    assert(MapCompletion::Format(20000, '.') == "100.00%");
    return 0;
}
