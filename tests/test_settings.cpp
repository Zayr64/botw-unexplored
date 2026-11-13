#include <cassert>
#include <sstream>
#include <string>
#include <vector>

#include "Settings.h"
#include "Utf8.h"

int main()
{
    SettingsIO::Settings expected;
    expected.cameraX = 12.5f;
    expected.cameraY = -8.0f;
    expected.zoom = 1.5f;
    expected.legendOpen = false;
    expected.visible[3] = true;
    expected.visible[SettingsIO::CategoryIndex(LegendCategories::ButtonTypes::SpecialChests)] = true;
    expected.showMode = 2;
    expected.language = 1;

    std::stringstream current;
    assert(SettingsIO::Save(current, expected));
    SettingsIO::Settings actual;
    assert(SettingsIO::Load(current, actual) == SettingsIO::LoadResult::Current);
    assert(actual.cameraX == expected.cameraX && actual.cameraY == expected.cameraY && actual.zoom == expected.zoom);
    assert(!actual.legendOpen && actual.visible[3] && actual.showMode == 2 && actual.language == 1);
    assert(!actual.visible[7] && actual.visible[8]);

    // The legend categories keep their places in the file; the entry of the old display-mode button is skipped.
    static_assert(SettingsIO::CategoryIndex(LegendCategories::ButtonTypes::Locations) == 5, "");
    static_assert(SettingsIO::CategoryIndex(LegendCategories::ButtonTypes::Chests) == 7, "");
    static_assert(SettingsIO::CategoryIndex(LegendCategories::ButtonTypes::SpecialChests) == 8, "");
    static_assert(SettingsIO::CategoryIndex(LegendCategories::ButtonTypes::Walls) == 9, "");

    // A release from before the chests reads header, version, 4 values, 7 entries, showMode and language, and
    // stops: the file starts with exactly those lines, and the chests come after them.
    std::vector<std::string> lines;
    {
        std::stringstream written;
        assert(SettingsIO::Save(written, expected));
        std::string line;
        while (std::getline(written, line))
            lines.push_back(line);
    }
    assert(lines.size() == 18);
    assert(lines[0] == "BOTW_UNEXPLORED_SETTINGS" && lines[1] == "2" && lines[5] == "0");
    assert(lines[6 + 3] == "1" && lines[13] == "2" && lines[14] == "1");
    assert(lines[15] == "0" && lines[16] == "1" && lines[17] == "0");

    // The file of the release with chests and no walls ends one line earlier: the walls stay off.
    std::stringstream beforeWalls("BOTW_UNEXPLORED_SETTINGS\n2\n4\n-5\n0.5\n1\n1\n1\n0\n0\n0\n1\n0\n0\n-1\n1\n1\n");
    SettingsIO::Settings fromBeforeWalls;
    assert(SettingsIO::Load(beforeWalls, fromBeforeWalls) == SettingsIO::LoadResult::Current);
    assert(fromBeforeWalls.visible[7] && fromBeforeWalls.visible[8] && !fromBeforeWalls.visible[9]);

    // ...and its file, which ends before the chests, is read with them switched off.
    std::stringstream older("BOTW_UNEXPLORED_SETTINGS\n2\n4\n-5\n0.5\n1\n1\n1\n0\n0\n0\n1\n0\n0\n-1\n");
    SettingsIO::Settings fromOlder;
    assert(SettingsIO::Load(older, fromOlder) == SettingsIO::LoadResult::Current);
    assert(fromOlder.cameraX == 4.0f && fromOlder.visible[1] && fromOlder.visible[5]);
    assert(!fromOlder.visible[7] && !fromOlder.visible[8]);
    std::stringstream badChest("BOTW_UNEXPLORED_SETTINGS\n2\n4\n-5\n0.5\n1\n1\n1\n0\n0\n0\n1\n0\n0\n-1\nyes\n");
    assert(SettingsIO::Load(badChest, fromOlder) == SettingsIO::LoadResult::Invalid);

    std::stringstream legacy("4\n-5\n0.5\n1\n1\n0\n0\n0\n0\n0\n0\n");
    assert(SettingsIO::Load(legacy, actual) == SettingsIO::LoadResult::Legacy);
    assert(actual.cameraX == 4.0f && actual.visible[0] && !actual.visible[1]);

    std::stringstream empty;
    assert(SettingsIO::Load(empty, actual) == SettingsIO::LoadResult::Missing);
    std::stringstream invalid("BOTW_UNEXPLORED_SETTINGS\n2\nnan\n");
    assert(SettingsIO::Load(invalid, actual) == SettingsIO::LoadResult::Invalid);
    std::stringstream future("BOTW_UNEXPLORED_SETTINGS\n99\n");
    assert(SettingsIO::Load(future, actual) == SettingsIO::LoadResult::FutureVersion);

    assert(Utf8::Decode("ASCII") == U"ASCII");
    assert(Utf8::Decode("Русский") == U"Русский");
    assert(Utf8::Decode("Español") == U"Español");
    assert(Utf8::Decode("\xF0\x9F\x97\xBA") == U"🗺");
    assert(Utf8::Decode("\xF0\x28\x8C\x28") == U"\uFFFD(\uFFFD(");
}
