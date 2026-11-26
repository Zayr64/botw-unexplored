// The reader of quests.txt (QuestMap): what a good line gives, what is skipped, what is not such a file.
// QUESTS_FILE: a real quests.txt (made from the game's files, never in the repository) to read too.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "QuestMap.h"

int main()
{
    std::vector<QuestMap::Entry> entries;
    const std::string text =
        "# BOTW_UNEXPLORED_QUESTS 1\r\n"
        "side\t0\t1234\t5678\t0\t3352.40\t2138.81\tThe Weapon Connoisseur\tHateno Village\tA young man wants a weapon.\r\n"
        "shrine\t1\t42\t43\t44\t-10\t20.5\tA Name\t\t\n"
        "main\t0\t1\t2\t0\t0\t0\tNot a kind\tX\tY\n"
        "side\t0\t0\t2\t0\t0\t0\tNo start flag\tX\tY\n"
        "side\t0\t9\t2\t0\tabc\t0\tBad x\tX\tY\n"
        "side\t0\t9\t2\t0\t0\t0\t\tX\tY\n"
        "side\t0\t9\t2\t0\t0\t0\tToo few\n";
    assert(QuestMap::Parse(text, entries));
    assert(entries.size() == 2);
    const QuestMap::Entry& a = entries[0];
    assert(!a.shrine && !a.dlc && a.startHash == 1234 && a.finishHash == 5678 && a.requiresHash == 0);
    assert(a.x > 3352.3f && a.x < 3352.5f && a.z > 2138.8f && a.z < 2138.9f);
    assert(a.name == "The Weapon Connoisseur" && a.location == "Hateno Village" &&
           a.description == "A young man wants a weapon.");
    const QuestMap::Entry& b = entries[1];
    assert(b.shrine && b.dlc && b.requiresHash == 44 && b.x == -10.0f && b.location.empty() && b.description.empty());

    assert(!QuestMap::Parse("something else\nside\t0\t1\t2\t0\t0\t0\tA\tB\tC\n", entries) && entries.empty());
    assert(!QuestMap::Parse("", entries));
    assert(QuestMap::Parse("# BOTW_UNEXPLORED_QUESTS 1\n", entries) && entries.empty());
    assert(!QuestMap::Load("this/file/does/not/exist.txt") && QuestMap::Entries().empty());

    if (const char* path = std::getenv("QUESTS_FILE"))
    {
        assert(QuestMap::Load(path));
        const size_t count = QuestMap::Entries().size();
        int regions = 0;
        for (const QuestMap::Entry& entry : QuestMap::Entries())
            regions += QuestMap::RegionOf(entry.startHash) >= 0;
        std::printf("%s: %zu quests, %d with a region\n", path, count, regions);
        assert(count > 100);
    }
    std::printf("quest map: ok\n");
    return 0;
}
