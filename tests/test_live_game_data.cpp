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
    struct Collector : LiveGameData::Visitor
    {
        // kind, hash -> every value seen for it
        std::multimap<std::pair<int, uint32_t>, std::string> seen;
        FakeGameMemory* memory = nullptr;
        bool addressesMatch = true;

        void OnFlag(LiveGameData::FlagKind kind, uint32_t hash, const uint8_t* value, uint64_t address) override
        {
            const size_t size = kind == LiveGameData::FlagKind::Bool ? 1 : kind == LiveGameData::FlagKind::S32 ? 4 :
                                kind == LiveGameData::FlagKind::String32 ? 32 : 12;
            // The address given is where that same value is in the game's memory.
            uint8_t again[32] = {};
            if (memory)
            {
                if (!memory->Read(address, again, size) || std::memcmp(again, value, size) != 0)
                    addressesMatch = false;
                --memory->readCount; // this check is not one of the reads being counted
            }
            seen.insert(std::make_pair(std::make_pair(static_cast<int>(kind), hash),
                                       std::string(reinterpret_cast<const char*>(value), size)));
        }
    };

    uint32_t Next(uint32_t& state)
    {
        state = state * 1664525u + 1013904223u;
        return state;
    }

    std::vector<FakeFlag> MakeFlags()
    {
        std::vector<FakeFlag> flags;
        uint32_t state = 12345;
        for (int i = 0; i < 5000; ++i)
        {
            FakeFlag flag = {LiveGameData::FlagKind::Bool, Next(state), {}};
            flag.value[0] = static_cast<uint8_t>((Next(state) >> 8 & 1) | 0x10); // category bits above bit 0
            flags.push_back(flag);
        }
        for (int i = 0; i < 300; ++i)
        {
            FakeFlag flag = {LiveGameData::FlagKind::S32, Next(state), {}};
            const int32_t value = static_cast<int32_t>(Next(state));
            std::memcpy(flag.value, &value, 4);
            flags.push_back(flag);
        }
        for (int i = 0; i < 3; ++i)
        {
            FakeFlag flag = {LiveGameData::FlagKind::String32, Next(state), {}};
            std::snprintf(reinterpret_cast<char*>(flag.value), sizeof(flag.value), "MainField%d", i);
            flags.push_back(flag);
        }
        for (int i = 0; i < 2; ++i)
        {
            FakeFlag flag = {LiveGameData::FlagKind::Vector3f, Next(state), {}};
            const float value[3] = {-997.4f + i, 187.0f, 1603.1f};
            std::memcpy(flag.value, value, sizeof(value));
            flags.push_back(flag);
        }
        // The same hash twice with different values: both must be reported.
        FakeFlag duplicate = flags[10];
        duplicate.value[0] ^= 1;
        flags.push_back(duplicate);
        return flags;
    }

    void ExpectAllSeen(const std::vector<FakeFlag>& flags, const Collector& collector)
    {
        assert(collector.seen.size() == flags.size());
        for (const FakeFlag& flag : flags)
        {
            const size_t size = flag.kind == LiveGameData::FlagKind::Bool ? 1 : flag.kind == LiveGameData::FlagKind::S32 ? 4 :
                                flag.kind == LiveGameData::FlagKind::String32 ? 32 : 12;
            const std::string expected(reinterpret_cast<const char*>(flag.value), size);
            const auto range = collector.seen.equal_range(std::make_pair(static_cast<int>(flag.kind), flag.hash));
            bool found = false;
            for (auto it = range.first; it != range.second; ++it)
                found = found || it->second == expected;
            assert(found);
        }
    }
}

int main()
{
    const LiveGameData::Layout& layout = LiveGameData::Botw160();
    const std::vector<FakeFlag> flags = MakeFlags();

    // Everything is found, with buffers of several sizes (objects across page ends, unmapped gaps).
    for (const size_t perBuffer : {1000u, 37u, 4096u, 1u})
    {
        FakeGame game;
        game.Build(layout, flags, perBuffer);
        Collector collector;
        collector.memory = &game.memory;
        LiveGameData::Report report;
        const bool ok = LiveGameData::Scan(game.memory, game.mainBase, layout, collector, report);
        if (!ok)
            std::printf("%s\n%s\n", report.error.c_str(), report.details.c_str());
        assert(ok && report.ok && report.error.empty());
        assert(collector.addressesMatch);
        assert(report.tableSize[0] == 5001 && report.objects[0] == 5001);
        assert(report.tableSize[1] == 300 && report.objects[1] == 300);
        assert(report.tableSize[2] == 3 && report.tableSize[3] == 2);
        ExpectAllSeen(flags, collector);
        // With large buffers like the game's: a few hundred reads at most, not one per flag.
        assert(perBuffer < 1000 || game.memory.readCount < 400);
    }

    // Objects that are not in the tables (array flags) are reported too and do not break anything.
    {
        std::vector<bool> listed(flags.size(), true);
        listed[3] = listed[4] = false;
        FakeGame game;
        game.Build(layout, flags, 1000, 0, &listed);
        Collector collector;
        LiveGameData::Report report;
        assert(LiveGameData::Scan(game.memory, game.mainBase, layout, collector, report));
        assert(report.tableSize[0] == 4999 && report.objects[0] == 5001);
        ExpectAllSeen(flags, collector);
    }

    // The game has not created its data yet.
    {
        FakeGame game;
        game.Build(layout, flags);
        game.memory.Put<uint64_t>(game.mainBase + layout.managerPointer, 0);
        Collector collector;
        LiveGameData::Report report;
        assert(!LiveGameData::Scan(game.memory, game.mainBase, layout, collector, report));
        assert(!report.ok && !report.error.empty() && collector.seen.empty());
    }

    // Another layout (another game version): the getters would read another buffer.
    {
        FakeGame game;
        game.Build(layout, flags);
        game.memory.Put<uint64_t>(game.manager + layout.flagBufferRef, game.manager + layout.flagBufferOffset - 8);
        Collector collector;
        LiveGameData::Report report;
        assert(!LiveGameData::Scan(game.memory, game.mainBase, layout, collector, report));
        assert(collector.seen.empty());
    }

    // The tables hold other objects (not Flag<T>): refused, with the first object in the details.
    {
        FakeGame game;
        game.Build(layout, flags, 1000, 0x40);
        Collector collector;
        LiveGameData::Report report;
        assert(!LiveGameData::Scan(game.memory, game.mainBase, layout, collector, report));
        assert(report.details.find("first object") != std::string::npos);
    }

    // A table with an absurd size, and one with a bad pointer.
    {
        FakeGame game;
        game.Build(layout, flags);
        game.memory.Put<int32_t>(game.flagBuffer + layout.tablesOffset, 5000000);
        Collector collector;
        LiveGameData::Report report;
        assert(!LiveGameData::Scan(game.memory, game.mainBase, layout, collector, report));
    }
    {
        FakeGame game;
        game.Build(layout, flags);
        uint64_t table = 0;
        game.memory.Read(game.flagBuffer + layout.tablesOffset + 8, &table, 8);
        game.memory.Put<uint64_t>(table + 8 * 100, 0x1234567);
        Collector collector;
        LiveGameData::Report report;
        assert(!LiveGameData::Scan(game.memory, game.mainBase, layout, collector, report));
    }

    // Part of the flags cannot be read: refused instead of reporting a partial state.
    {
        FakeGame game;
        game.Build(layout, flags, 1000);
        // Unmap the last buffer of bool objects.
        game.memory.Unmap(game.lastBuffer[0].first, game.lastBuffer[0].second);
        Collector collector;
        LiveGameData::Report report;
        assert(!LiveGameData::Scan(game.memory, game.mainBase, layout, collector, report));
        assert(!report.ok);
    }

    // Link's position.
    {
        FakeGame game;
        game.Build(layout, flags);
        float position[3] = {0, 0, 0};
        assert(!LiveGameData::ReadPlayerPosition(game.memory, game.mainBase, layout, position)); // no actor yet
        game.SetPlayerPosition(layout, 0.0f, 0.0f, 0.0f);
        assert(!LiveGameData::ReadPlayerPosition(game.memory, game.mainBase, layout, position)); // still loading
        game.SetPlayerPosition(layout, -952.75f, 193.27f, 1508.21f);
        assert(LiveGameData::ReadPlayerPosition(game.memory, game.mainBase, layout, position));
        assert(position[0] == -952.75f && position[1] == 193.27f && position[2] == 1508.21f);
    }

    return 0;
}
