// The extras of LiveGameData: f32 flags and array flags (zayr15). They never make a scan fail; their vtables are
// taken from the game's own tables (or, for the arrays of a kind with no single flag, from the array asked for).
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "FakeGameMemory.h"
#include "LiveGameData.h"

namespace
{
    using Kind = LiveGameData::FlagKind;

    struct Collector : LiveGameData::Visitor
    {
        std::multimap<std::pair<int, uint32_t>, std::string> seen;
        std::map<uint32_t, std::vector<std::string>> arrays; // hash -> values in the order received
        std::map<uint32_t, int> lastIndex;
        bool inOrder = true;

        void OnFlag(Kind kind, uint32_t hash, const uint8_t* value, uint64_t) override
        {
            seen.insert(std::make_pair(std::make_pair(static_cast<int>(kind), hash),
                                       std::string(reinterpret_cast<const char*>(value), FakeValueSize(kind))));
        }

        void OnArrayElement(Kind kind, uint32_t hash, int index, const uint8_t* value) override
        {
            const auto last = lastIndex.find(hash);
            if ((last == lastIndex.end() && index != 0) || (last != lastIndex.end() && index != last->second + 1))
                inOrder = false;
            lastIndex[hash] = index;
            arrays[hash].push_back(std::string(reinterpret_cast<const char*>(value), FakeValueSize(kind)));
        }

        int Count(Kind kind) const
        {
            int n = 0;
            for (const auto& entry : seen)
                n += entry.first.first == static_cast<int>(kind) ? 1 : 0;
            return n;
        }
    };

    uint32_t Next(uint32_t& state)
    {
        state = state * 1664525u + 1013904223u;
        return state;
    }

    // A few core flags (the scan needs them), and `f32Count` f32 flags.
    std::vector<FakeFlag> MakeFlags(int f32Count, int string64Count = 0)
    {
        std::vector<FakeFlag> flags;
        uint32_t state = 777;
        for (int i = 0; i < 200; ++i)
        {
            FakeFlag flag = {Kind::Bool, Next(state), {}};
            flag.value[0] = static_cast<uint8_t>(Next(state) >> 9 & 1);
            flags.push_back(flag);
        }
        for (int i = 0; i < 40; ++i)
        {
            FakeFlag flag = {Kind::S32, Next(state), {}};
            const int32_t value = static_cast<int32_t>(Next(state) % 5000);
            std::memcpy(flag.value, &value, 4);
            flags.push_back(flag);
        }
        FakeFlag text = {Kind::String32, Next(state), {}};
        std::strcpy(reinterpret_cast<char*>(text.value), "MainField");
        flags.push_back(text);
        FakeFlag vector = {Kind::Vector3f, Next(state), {}};
        const float position[3] = {-1000.0f, 200.0f, 1600.0f};
        std::memcpy(vector.value, position, sizeof(position));
        flags.push_back(vector);
        for (int i = 0; i < f32Count; ++i)
        {
            FakeFlag flag = {Kind::F32, Next(state), {}};
            const float value = static_cast<float>(i) * 1.25f + 0.5f;
            std::memcpy(flag.value, &value, 4);
            flags.push_back(flag);
        }
        for (int i = 0; i < string64Count; ++i)
        {
            FakeFlag flag = {Kind::String64, Next(state), {}};
            std::snprintf(reinterpret_cast<char*>(flag.value), sizeof(flag.value), "A text of sixty-four, number %d", i);
            flags.push_back(flag);
        }
        return flags;
    }

    std::vector<uint8_t> Text(const char* text)
    {
        std::vector<uint8_t> value(64, 0);
        std::strncpy(reinterpret_cast<char*>(value.data()), text, 63);
        return value;
    }

    std::vector<uint8_t> S32(int32_t number)
    {
        std::vector<uint8_t> value(4, 0);
        std::memcpy(value.data(), &number, 4);
        return value;
    }

    // The pouch as the game keeps it: 420 names (the first ones used), counts and the equipped marks.
    std::vector<FakeArray> MakePouch(uint32_t names, uint32_t counts, uint32_t equipped)
    {
        FakeArray a = {Kind::String64, names, {}}, b = {Kind::S32, counts, {}}, c = {Kind::Bool, equipped, {}};
        for (int i = 0; i < 420; ++i)
        {
            char name[64];
            std::snprintf(name, sizeof(name), "Item_Material_%03d", i);
            a.values.push_back(Text(i < 37 ? name : ""));
            b.values.push_back(S32(i < 37 ? i * 3 + 1 : 0));
            c.values.push_back(std::vector<uint8_t>(1, i == 2 ? 1 : 0));
        }
        // Other arrays of the same kinds, not asked for.
        FakeArray other = {Kind::String64, 0x11111111, {Text("Decoy"), Text("Decoy")}};
        FakeArray otherCounts = {Kind::S32, 0x22222222, {S32(9), S32(8), S32(7)}};
        return {other, a, otherCounts, b, c};
    }

    void Print(const LiveGameData::Report& report)
    {
        std::printf("%s\n%s\n", report.error.c_str(), report.details.c_str());
        std::fflush(stdout);
    }
}

int main()
{
    const LiveGameData::Layout& layout = LiveGameData::Botw160();
    const int f32 = static_cast<int>(Kind::F32);

    // f32 flags, asked for: all read, with the vtable taken from their table.
    {
        const std::vector<FakeFlag> flags = MakeFlags(150);
        FakeGame game;
        game.Build(layout, flags, 64);
        Collector collector;
        LiveGameData::Options options;
        options.f32 = true;
        LiveGameData::Report report;
        const bool ok = LiveGameData::Scan(game.memory, game.mainBase, layout, collector, report, options);
        if (!ok || !report.f32)
            Print(report);
        assert(ok && report.f32);
        assert(report.tableSize[f32] == 150 && report.objects[f32] == 150 && collector.Count(Kind::F32) == 150);
        for (const FakeFlag& flag : flags)
            if (flag.kind == Kind::F32)
                assert(collector.seen.find(std::make_pair(f32, flag.hash)) != collector.seen.end());
        assert(report.details.find("f32: vtable main+2bd1208 (expected main+2bd1208)") != std::string::npos);
    }

    // Not asked for: the scan is the same as before, no f32 seen.
    {
        FakeGame game;
        game.Build(layout, MakeFlags(150));
        Collector collector;
        LiveGameData::Report report;
        assert(LiveGameData::Scan(game.memory, game.mainBase, layout, collector, report));
        assert(!report.f32 && collector.Count(Kind::F32) == 0 && report.objects[f32] == 0);
    }

    // The game's f32 vtable is not the one expected: taken from the table all the same, and both go to the log.
    {
        LiveGameData::Layout other = layout;
        other.kinds[f32].vtable = 0x2BD9990;
        FakeGame game;
        game.Build(other, MakeFlags(30));
        Collector collector;
        LiveGameData::Options options;
        options.f32 = true;
        LiveGameData::Report report;
        assert(LiveGameData::Scan(game.memory, game.mainBase, layout, collector, report, options));
        assert(report.f32 && collector.Count(Kind::F32) == 30);
        assert(report.details.find("f32: vtable main+2bd9990 (expected main+2bd1208)") != std::string::npos);
    }

    // The f32 table does not hold what is expected (hashes going down): f32 left out, the rest read as usual.
    {
        const std::vector<FakeFlag> flags = MakeFlags(30);
        FakeGame game;
        game.Build(layout, flags);
        {
            uint64_t table = 0, first = 0, second = 0;
            game.memory.Read(game.flagBuffer + layout.tablesOffset + layout.kinds[f32].table * 16 + 8, &table, 8);
            game.memory.Read(table, &first, 8);
            game.memory.Read(table + 8, &second, 8);
            game.memory.Put<uint64_t>(table, second);
            game.memory.Put<uint64_t>(table + 8, first);
        }
        Collector collector;
        LiveGameData::Options options;
        options.f32 = true;
        LiveGameData::Report report;
        assert(LiveGameData::Scan(game.memory, game.mainBase, layout, collector, report, options));
        assert(report.ok && !report.f32 && collector.Count(Kind::F32) == 0);
        assert(collector.Count(Kind::Bool) == 200 && collector.Count(Kind::S32) == 40);
        assert(report.details.find("f32: the objects of its table do not look like flags") != std::string::npos);
    }

    // Arrays: the pouch, element by element in index order, with no single 64-character flag to learn from.
    const uint32_t names = 0x5F283289, counts = 0x6A09E8E3, equipped = 0x0DA7D5A1;
    const uint32_t asked[3] = {names, counts, equipped};
    for (const int string64Singles : {0, 5})
    {
        FakeGame game;
        game.Build(layout, MakeFlags(0, string64Singles));
        game.BuildArrays(layout, MakePouch(names, counts, equipped));
        Collector collector;
        LiveGameData::Options options;
        options.arrays = asked;
        options.arrayCount = 3;
        LiveGameData::Report report;
        const bool ok = LiveGameData::Scan(game.memory, game.mainBase, layout, collector, report, options);
        if (!ok || report.arraysFound != 3)
            Print(report);
        assert(ok && report.arraysFound == 3 && collector.inOrder);
        assert(collector.arrays[names].size() == 420 && collector.arrays[counts].size() == 420 &&
               collector.arrays[equipped].size() == 420);
        assert(std::strcmp(collector.arrays[names][5].c_str(), "Item_Material_005") == 0);
        assert(collector.arrays[names][37][0] == '\0');
        int32_t count = 0;
        std::memcpy(&count, collector.arrays[counts][5].data(), 4);
        assert(count == 16);
        assert(collector.arrays[equipped][2][0] == 1 && collector.arrays[equipped][3][0] == 0);
        assert(collector.arrays.find(0x11111111) == collector.arrays.end()); // not asked for
        // The single 64-character flags are never visited as such.
        assert(collector.Count(Kind::String64) == 0);
    }

    // One element of an array with another hash: that array is left out whole, the others are read.
    {
        FakeGame game;
        game.Build(layout, MakeFlags(0));
        std::vector<FakeArray> pouch = MakePouch(names, counts, equipped);
        game.BuildArrays(layout, pouch);
        // Element 200 of the names: find it through the tables and change its hash.
        uint64_t outer = 0, array = 0, elements = 0, element = 0;
        game.memory.Read(game.flagBuffer + layout.arrayTablesOffset + layout.kinds[static_cast<int>(Kind::String64)].table * 16 + 8,
                         &outer, 8);
        game.memory.Read(outer + 8, &array, 8); // the second string64 array is the names
        game.memory.Read(array + 8, &elements, 8);
        game.memory.Read(elements + 200 * 8, &element, 8);
        game.memory.Put<uint32_t>(element + layout.kinds[static_cast<int>(Kind::String64)].hashOffset, 0x33333333);
        Collector collector;
        LiveGameData::Options options;
        options.arrays = asked;
        options.arrayCount = 3;
        LiveGameData::Report report;
        assert(LiveGameData::Scan(game.memory, game.mainBase, layout, collector, report, options));
        assert(report.arraysFound == 2 && collector.arrays.find(names) == collector.arrays.end());
        assert(collector.arrays[counts].size() == 420);
    }

    // The 64-character objects are laid out otherwise in the game (another offset of the hash): not found, no harm.
    {
        LiveGameData::Layout other = layout;
        other.kinds[static_cast<int>(Kind::String64)].hashOffset = 0x168;
        FakeGame game;
        game.Build(other, MakeFlags(0));
        game.BuildArrays(other, MakePouch(names, counts, equipped));
        Collector collector;
        LiveGameData::Options options;
        options.arrays = asked;
        options.arrayCount = 3;
        LiveGameData::Report report;
        assert(LiveGameData::Scan(game.memory, game.mainBase, layout, collector, report, options));
        assert(report.ok && report.arraysFound == 2 && collector.arrays.find(names) == collector.arrays.end());
    }

    std::printf("live extras: ok\n");
    return 0;
}
