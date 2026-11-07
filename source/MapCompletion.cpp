#include "MapCompletion.h"

int MapCompletion::Hundredths(int foundEntries)
{
    if (foundEntries <= 0)
        return 0;
    if (foundEntries >= TotalEntries)
        return 10000;
    return foundEntries * 10000 / TotalEntries;
}

std::string MapCompletion::Format(int hundredths, char decimalSeparator)
{
    if (hundredths < 0)
        hundredths = 0;
    if (hundredths > 10000)
        hundredths = 10000;

    const int fraction = hundredths % 100;
    std::string text = std::to_string(hundredths / 100);
    text += decimalSeparator;
    if (fraction < 10)
        text += '0';
    text += std::to_string(fraction);
    text += '%';
    return text;
}
