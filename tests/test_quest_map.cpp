// The reader of quests.txt (QuestMap): what a good line gives, what is skipped, what is not such a file; the steps of
// version 2 and the step a quest in progress is at.
// QUESTS_FILE: a real quests.txt (made from the game's files, never in the repository) to read too; with QUESTS_SAVE
// (a game_data.sav) the quests in progress in that save are counted.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "QuestMap.h"

int main()
{
    std::vector<QuestMap::Entry> entries;
    const std::string text =
        "# BOTW_UNEXPLORED_QUESTS 1\r\n"
        "side\t0\t1234\t5678\t0\t3352.40\t2138.81\tThe Weapon Connoisseur\tHateno Village\tA young man wants a weapon.\r\n"
        "shrine\t1\t42\t43\t44\t-10\t20.5\tA Name\t\t\n"
        "main\t0\t1\t2\t0\t0\t0\tNot a kind in version 1\tX\tY\n"
        "side\t0\t0\t2\t0\t0\t0\tNo start flag\tX\tY\n"
        "side\t0\t9\t2\t0\tabc\t0\tBad x\tX\tY\n"
        "side\t0\t9\t2\t0\t0\t0\t\tX\tY\n"
        "side\t0\t9\t2\t0\t0\t0\tToo few\n"
        "step\t1\t0\t\tNot in version 1\n";
    assert(QuestMap::Parse(text, entries));
    assert(entries.size() == 2);
    const QuestMap::Entry a = entries[0]; // a copy: entries is read again below
    assert(!a.shrine && !a.main && !a.dlc && a.startHash == 1234 && a.finishHash == 5678 && a.requiresHash == 0);
    assert(a.x > 3352.3f && a.x < 3352.5f && a.z > 2138.8f && a.z < 2138.9f);
    assert(a.name == "The Weapon Connoisseur" && a.location == "Hateno Village" &&
           a.description == "A young man wants a weapon.");
    assert(a.steps.empty());
    const QuestMap::Entry& b = entries[1];
    assert(b.shrine && b.dlc && b.requiresHash == 44 && b.x == -10.0f && b.location.empty() && b.description.empty());

    assert(!QuestMap::Parse("something else\nside\t0\t1\t2\t0\t0\t0\tA\tB\tC\n", entries) && entries.empty());
    assert(!QuestMap::Parse("", entries));
    assert(QuestMap::Parse("# BOTW_UNEXPLORED_QUESTS 1\n", entries) && entries.empty());
    assert(!QuestMap::Parse("# BOTW_UNEXPLORED_QUESTS 3\n", entries));
    assert(!QuestMap::Load("this/file/does/not/exist.txt") && QuestMap::Entries().empty());

    // Version 2: main quests, and the steps after each quest (all but the last)
    const std::string v2 =
        "# BOTW_UNEXPLORED_QUESTS 2\n"
        "side\t0\t100\t199\t0\t10\t20\tFour steps\tPlace\tFirst text\n"
        "step\t100\t0\t\tTaken: no marker\n"
        "step\t101\t0\t30.5,40,0\tGo there\n"
        "step\t102\t0\t1,2,0;3,4,777;5,6,0;7,8,0;9,9,0\tFind them\n"
        "main\t0\t300\t399\t0\t0\t0\tA main quest\t\t\n"
        "step\t300\t0\t\tStarted\n"
        "step\t301\t1\t\tFinish before the last step\n"
        "side\t0\t0\t2\t0\t0\t0\tSkipped quest\tX\tY\n"
        "step\t500\t0\t\tBelongs to the skipped quest\n"
        "shrine\t0\t600\t699\t0\t0\t0\tBad steps\t\t\n"
        "step\t600\t2\t\tBad end field\n"
        "step\t601\t0\t1,2\tBad marker\n"
        "step\t602\t0\t\n";
    assert(QuestMap::Parse(v2, entries));
    assert(entries.size() == 3);
    const QuestMap::Entry& four = entries[0];
    assert(four.steps.size() == 3 && four.steps[0].markers.empty() && four.steps[0].text == "Taken: no marker");
    assert(four.steps[1].markers.size() == 1 && four.steps[1].markers[0].x == 30.5f && four.steps[1].markers[0].z == 40.0f);
    assert(four.steps[2].markers.size() == QuestMap::MaxMarkers && four.steps[2].markers[1].offHash == 777);
    const QuestMap::Entry& mainQuest = entries[1];
    assert(mainQuest.main && !mainQuest.shrine && mainQuest.steps.size() == 2 && mainQuest.steps[1].ends);
    assert(entries[2].name == "Bad steps" && entries[2].steps.empty());

    // The step a quest is at: the last one whose flag is set, once taken; -1 when not taken, done or ended early
    std::set<uint32_t> set;
    const auto isSet = [&set](uint32_t hash) { return set.count(hash) > 0; };
    assert(QuestMap::CurrentStep(four, isSet) == -1); // not taken
    set = {100};
    assert(QuestMap::CurrentStep(four, isSet) == 0);
    assert(QuestMap::ActiveMarkers(four, 0, isSet).empty()); // no marker: the map shows where it was taken
    set = {100, 101};
    assert(QuestMap::CurrentStep(four, isSet) == 1 && QuestMap::ActiveMarkers(four, 1, isSet).size() == 1);
    set = {100, 101, 102};
    assert(QuestMap::CurrentStep(four, isSet) == 2 && QuestMap::ActiveMarkers(four, 2, isSet).size() == 4);
    set.insert(777); // one of them found: its marker goes
    const std::vector<QuestMap::Marker> left = QuestMap::ActiveMarkers(four, 2, isSet);
    assert(left.size() == 3 && left[1].x == 5.0f);
    set.insert(199); // done
    assert(QuestMap::CurrentStep(four, isSet) == -1);
    set = {300};
    assert(QuestMap::CurrentStep(mainQuest, isSet) == 0);
    set = {300, 301}; // the step that ends it
    assert(QuestMap::CurrentStep(mainQuest, isSet) == -1);
    set = {101}; // a later flag without the quest taken: not in progress
    assert(QuestMap::CurrentStep(four, isSet) == -1);
    assert(QuestMap::CurrentStep(a, [](uint32_t) { return true; }) == -1); // version 1: no steps
    assert(QuestMap::ActiveMarkers(four, 7, isSet).empty() && QuestMap::ActiveMarkers(four, -1, isSet).empty());

    if (const char* path = std::getenv("QUESTS_FILE"))
    {
        assert(QuestMap::Load(path));
        const size_t count = QuestMap::Entries().size();
        int regions = 0, steps = 0, mains = 0;
        for (const QuestMap::Entry& entry : QuestMap::Entries())
        {
            regions += QuestMap::RegionOf(entry.startHash) >= 0;
            steps += static_cast<int>(entry.steps.size());
            mains += entry.main;
        }
        std::printf("%s: %zu quests (%d main), %d steps, %d with a region\n", path, count, mains, steps, regions);
        assert(count > 100);
        if (const char* savePath = std::getenv("QUESTS_SAVE"))
        {
            std::ifstream file(savePath, std::ios::binary);
            const std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            assert(data.size() > 0x20);
            std::unordered_map<uint32_t, uint32_t> values;
            for (size_t off = 0x0C; off + 8 <= data.size(); off += 8)
            {
                uint32_t hash = 0, value = 0;
                std::memcpy(&hash, data.data() + off, 4);
                std::memcpy(&value, data.data() + off + 4, 4);
                values.emplace(hash, value);
            }
            const auto inSave = [&values](uint32_t hash) {
                const auto found = values.find(hash);
                return found != values.end() && found->second != 0;
            };
            int inProgress = 0, marked = 0;
            for (const QuestMap::Entry& entry : QuestMap::Entries())
            {
                const int step = QuestMap::CurrentStep(entry, inSave);
                if (step < 0)
                    continue;
                ++inProgress;
                marked += !QuestMap::ActiveMarkers(entry, step, inSave).empty();
            }
            std::printf("%s: %d quests in progress, %d with the game's marker\n", savePath, inProgress, marked);
        }
    }
    std::printf("quest map: ok\n");
    return 0;
}
