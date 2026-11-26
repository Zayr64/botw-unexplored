// MapMarks (zayr32 test): a pin on the running game's map, written to the Flag<T> objects and to their proxies.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "FakeGameMemory.h"
#include "LiveGameData.h"
#include "MapMarks.h"

namespace
{
    using Kind = LiveGameData::FlagKind;
    constexpr int Slots = 200;

    struct Mark
    {
        int slot;
        int32_t icon;
        float x, y, z;
    };

    std::vector<uint8_t> Bytes(const void* data, size_t size)
    {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        return std::vector<uint8_t>(p, p + size);
    }

    // A game with a few single flags and the two arrays of the map's marks, `marks` in them.
    void BuildGame(FakeGame& game, const std::vector<Mark>& marks)
    {
        const LiveGameData::Layout& layout = LiveGameData::Botw160();
        std::vector<FakeFlag> flags;
        for (uint32_t i = 0; i < 4; ++i)
        {
            FakeFlag bit = {Kind::Bool, 0x100 + i, {}};
            FakeFlag number = {Kind::S32, 0x200 + i, {}};
            FakeFlag text = {Kind::String32, 0x300 + i, {}};
            FakeFlag vector = {Kind::Vector3f, 0x400 + i, {}};
            flags.push_back(bit);
            flags.push_back(number);
            flags.push_back(text);
            flags.push_back(vector);
        }
        game.Build(layout, flags);

        FakeArray icons = {Kind::S32, MapMarks::IconNoHash, {}};
        FakeArray positions = {Kind::Vector3f, MapMarks::IconPosHash, {}};
        for (int i = 0; i < Slots; ++i)
        {
            int32_t icon = MapMarks::EmptyIcon;
            float position[3] = {MapMarks::EmptyX, 0.0f, 0.0f};
            for (const Mark& mark : marks)
                if (mark.slot == i)
                {
                    icon = mark.icon;
                    position[0] = mark.x;
                    position[1] = mark.y;
                    position[2] = mark.z;
                }
            icons.values.push_back(Bytes(&icon, 4));
            positions.values.push_back(Bytes(position, 12));
        }
        // Another s32 array first, so that the marks are not the first array of their table
        FakeArray other = {Kind::S32, 0x6A09FC59, {}};
        for (int i = 0; i < 20; ++i)
        {
            const int32_t value = i;
            other.values.push_back(Bytes(&value, 4));
        }
        game.BuildArrays(layout, {other, icons, positions});
    }

    int32_t IconAt(FakeGame& game, uint64_t address)
    {
        int32_t value = 0;
        assert(game.memory.Read(address, &value, 4));
        return value;
    }

    void PositionAt(FakeGame& game, uint64_t address, float out[3])
    {
        assert(game.memory.Read(address, out, 12));
    }

    const LiveGameData::Layout::Kind& KindInfo(Kind kind)
    {
        return LiveGameData::Botw160().kinds[static_cast<int>(kind)];
    }

    // Both copies of a slot.
    void CheckSlot(FakeGame& game, int slot, int32_t icon, const float position[3])
    {
        const uint64_t flagIcon = game.arrayFlags[MapMarks::IconNoHash][slot];
        const uint64_t flagPosition = game.arrayFlags[MapMarks::IconPosHash][slot];
        const uint64_t proxyIcon = game.arrayProxies[MapMarks::IconNoHash][slot];
        const uint64_t proxyPosition = game.arrayProxies[MapMarks::IconPosHash][slot];
        assert(IconAt(game, flagIcon + KindInfo(Kind::S32).valueOffset) == icon);
        assert(IconAt(game, proxyIcon + FakeGame::ProxyValueOffset) == icon);
        float flagValue[3], proxyValue[3];
        PositionAt(game, flagPosition + KindInfo(Kind::Vector3f).valueOffset, flagValue);
        PositionAt(game, proxyPosition + FakeGame::ProxyValueOffset, proxyValue);
        assert(std::memcmp(flagValue, position, 12) == 0);
        assert(std::memcmp(proxyValue, position, 12) == 0);
    }

    void TestFirstFreePin()
    {
        FakeGame game;
        // A pin already there (37), a stamp, and an icon the map does not offer (42, as in the save of 24/11)
        BuildGame(game, {{0, 42, 793.7f, 189.2f, 3721.9f}, {1, 37, 100.0f, 50.0f, 200.0f}, {3, 30, -10.0f, 5.0f, -20.0f}});
        const float position[3] = {-163.284f, 120.0f, -784.678f};
        MapMarks::Result result;
        assert(MapMarks::Add(game.memory, game.mainBase, LiveGameData::Botw160(), -1, position, result));
        assert(result.ok && result.error.empty());
        assert(result.icon == 38); // 37 is taken
        assert(result.slot == 2);  // the first empty slot
        assert(result.before.size() == 3);
        assert(result.before[0].slot == 0 && result.before[0].icon == 42);
        CheckSlot(game, 2, 38, position);
        // The others are as they were
        const float empty[3] = {MapMarks::EmptyX, 0.0f, 0.0f};
        CheckSlot(game, 4, MapMarks::EmptyIcon, empty);
        const float pin[3] = {100.0f, 50.0f, 200.0f};
        CheckSlot(game, 1, 37, pin);
        assert(game.memory.storeCount == 4);

        // The same place again: refused, nothing written
        MapMarks::Result again;
        assert(!MapMarks::Add(game.memory, game.mainBase, LiveGameData::Botw160(), -1, position, again));
        assert(game.memory.storeCount == 4);
        // Another place: the next pin in the next slot
        const float other[3] = {300.0f, 120.0f, -784.678f};
        MapMarks::Result second;
        assert(MapMarks::Add(game.memory, game.mainBase, LiveGameData::Botw160(), -1, other, second));
        assert(second.icon == 39 && second.slot == 4);
        std::printf("first free pin ok\n");
    }

    void TestFivePinsUsed()
    {
        FakeGame game;
        BuildGame(game, {{0, 37, 1, 1, 1}, {1, 38, 2, 2, 2}, {2, 39, 3, 3, 3}, {5, 40, 4, 4, 4}, {9, 41, 5, 5, 5}});
        const float position[3] = {10.0f, 0.0f, 10.0f};
        MapMarks::Result result;
        assert(!MapMarks::Add(game.memory, game.mainBase, LiveGameData::Botw160(), -1, position, result));
        assert(!result.error.empty());
        assert(game.memory.storeCount == 0);
        // A stamp can still go in
        MapMarks::Result stamp;
        assert(MapMarks::Add(game.memory, game.mainBase, LiveGameData::Botw160(), 34, position, stamp));
        assert(stamp.slot == 3 && stamp.icon == 34);
        std::printf("five pins used ok\n");
    }

    void TestProxyThatDoesNotMatch()
    {
        FakeGame game;
        BuildGame(game, {});
        // The proxy of slot 7 stands for another flag: nothing is written
        const uint64_t proxy = game.arrayProxies[MapMarks::IconNoHash][7];
        game.memory.Put<uint64_t>(proxy + 8, 0x1234);
        const float position[3] = {10.0f, 0.0f, 10.0f};
        MapMarks::Result result;
        assert(!MapMarks::Add(game.memory, game.mainBase, LiveGameData::Botw160(), -1, position, result));
        assert(game.memory.storeCount == 0);
        std::printf("proxy that does not match ok\n");
    }

    void TestWriteFailsHalfWay()
    {
        FakeGame game;
        BuildGame(game, {});
        game.memory.storeFails = 3; // the third write (the proxy's icon) fails
        const float position[3] = {10.0f, 20.0f, 30.0f};
        MapMarks::Result result;
        assert(!MapMarks::Add(game.memory, game.mainBase, LiveGameData::Botw160(), -1, position, result));
        // Undone: the slot is empty again in both copies
        const float empty[3] = {MapMarks::EmptyX, 0.0f, 0.0f};
        CheckSlot(game, 0, MapMarks::EmptyIcon, empty);
        std::printf("write that fails half way ok\n");
    }

    void TestNoMarks()
    {
        FakeGame game;
        BuildGame(game, {});
        // Without the arrays (another version, a game still loading): nothing found, nothing written
        FakeGame bare;
        std::vector<FakeFlag> flags = {{Kind::Bool, 1, {}}, {Kind::S32, 2, {}}, {Kind::String32, 3, {}}, {Kind::Vector3f, 4, {}}};
        bare.Build(LiveGameData::Botw160(), flags);
        const float position[3] = {10.0f, 0.0f, 10.0f};
        MapMarks::Result result;
        assert(!MapMarks::Add(bare.memory, bare.mainBase, LiveGameData::Botw160(), -1, position, result));
        assert(bare.memory.storeCount == 0);
        // Outside the map
        const float far[3] = {9000.0f, 0.0f, 0.0f};
        assert(!MapMarks::Add(game.memory, game.mainBase, LiveGameData::Botw160(), -1, far, result));
        assert(game.memory.storeCount == 0);
        std::printf("no marks ok\n");
    }

    void TestProbe()
    {
        FakeGame game;
        BuildGame(game, {{0, 42, 793.7f, 189.2f, 3721.9f}, {1, 37, -501.25f, 150.5f, -1011.75f}});
        // A "heap" of 2 MB, its second megabyte unmapped, with a list like the map screen's: the pin of slot 1 as x, y,
        // z after an icon number, 0x1230 into it
        const uint64_t heap = 0x5000000000ULL;
        game.memory.Map(heap, 0x100000);
        const float pin[3] = {-501.25f, 150.5f, -1011.75f};
        game.memory.Put<int32_t>(heap + 0x122C, 37);
        game.memory.Write(heap + 0x1230, pin, sizeof(pin));
        game.memory.Put<uint64_t>(heap + 0x1000, game.mainBase + 0x24B0F30); // the object's vtable
        // The x of slot 0 alone, with no z near it: not a hit
        game.memory.Write(heap + 0x8000, pin, 4);
        const int storesBefore = game.memory.storeCount;
        MapMarks::Result result;
        assert(MapMarks::Probe(game.memory, game.mainBase, LiveGameData::Botw160(), heap, 0x200000, result));
        assert(game.memory.storeCount == storesBefore); // read only
        assert(result.details.find("hit 1: slot 1 at 5000001230") != std::string::npos);
        assert(result.details.find("z at +8") != std::string::npos);
        assert(result.details.find("main+24b0f30 at -560") != std::string::npos);
        assert(result.details.find("hit 2") == std::string::npos);
        assert(result.details.find("16 unreadable") != std::string::npos);
        // No marks: nothing to look for
        FakeGame empty;
        BuildGame(empty, {});
        MapMarks::Result none;
        assert(!MapMarks::Probe(empty.memory, empty.mainBase, LiveGameData::Botw160(), heap, 0x200000, none));
        std::printf("probe ok\n");
    }
}

int main()
{
    TestProbe();
    TestFirstFreePin();
    TestFivePinsUsed();
    TestProxyThatDoesNotMatch();
    TestWriteFailsHalfWay();
    TestNoMarks();
    std::printf("All map marks tests passed\n");
    return 0;
}
