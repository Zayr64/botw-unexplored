// MapMarks (zayr32 test): a pin on the running game's map, written to the Flag<T> objects and to their proxies.
#include <cassert>
#include <cmath>
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

    // A map screen as the game holds it: the manager, five pins, 100 stamps, the medallion, and the screen's control.
    struct FakeScreen
    {
        uint64_t manager, pins, stamps, medallion, control, records;
    };

    void PutMark(FakeGame& game, uint64_t at, uint64_t vtable, int32_t icon, int32_t index, float x, float y, float z,
                 uint32_t state, uint64_t record)
    {
        game.memory.Map(at, 0x100);
        game.memory.Put<uint64_t>(at, vtable);
        game.memory.Put<uint64_t>(at + 0x08, record);
        game.memory.Put<int32_t>(at + 0x1C, icon);
        game.memory.Put<int32_t>(at + 0x20, static_cast<int32_t>(std::floor((x + 5000.0f) / 250.0f)));
        game.memory.Put<int32_t>(at + 0x24, static_cast<int32_t>(std::floor((z + 4000.0f) / 250.0f)));
        const float position[3] = {x, y, z};
        game.memory.Write(at + 0x28, position, sizeof(position));
        game.memory.Put<uint32_t>(at + 0x3C, state);
        game.memory.Put<int32_t>(at + 0x40, index);
    }

    FakeScreen BuildScreen(FakeGame& game)
    {
        const uint64_t heap = 0x5000000000ULL;
        FakeScreen screen = {heap, heap + 0x2000, heap + 0x3000, heap + 0x6000, heap + 0x7000, heap + 0x8000};
        game.memory.Map(heap, 0x10000);
        game.memory.Put<uint64_t>(game.mainBase + 0x2CA63B0, screen.manager);
        game.memory.Put<uint64_t>(screen.manager, game.mainBase + 0x2B86848);
        // Pins: 37 to 41, the second (38) put by the game, with its record
        game.memory.Put<int32_t>(screen.manager + 0x658, 5);
        game.memory.Put<int32_t>(screen.manager + 0x65C, 5);
        game.memory.Put<uint64_t>(screen.manager + 0x660, heap + 0x1000);
        for (int i = 0; i < 5; ++i)
        {
            const uint64_t pin = screen.pins + 0x80 * i;
            game.memory.Put<uint64_t>(heap + 0x1000 + 8 * i, pin);
            if (i == 1)
                PutMark(game, pin, game.mainBase + 0x2B866B0, 38, 1, 500.0f, 100.0f, 600.0f, 0x10, screen.records);
            else
                PutMark(game, pin, game.mainBase + 0x2B866B0, 37 + i, i, MapMarks::EmptyX, 0.0f, 0.0f, 0, 0);
        }
        game.memory.Put<uint64_t>(screen.records, game.mainBase + 0x2C00000);
        // Stamps: the first (30) on the map, with its record; the second is the first free one
        game.memory.Put<int32_t>(screen.manager + 0x610, 100);
        game.memory.Put<int32_t>(screen.manager + 0x614, 100);
        game.memory.Put<uint64_t>(screen.manager + 0x618, heap + 0x1100);
        for (int j = 0; j < 100; ++j)
        {
            const uint64_t stamp = screen.stamps + 0x50 * j;
            game.memory.Put<uint64_t>(heap + 0x1100 + 8 * j, stamp);
            if (j == 0)
                PutMark(game, stamp, game.mainBase + 0x2B874F0, 30, 0, -10.0f, 5.0f, -20.0f, 0x10, screen.records + 0x100);
            else
                PutMark(game, stamp, game.mainBase + 0x2B874F0, -1, j, MapMarks::EmptyX, 0.0f, 0.0f, 0, 0);
        }
        game.memory.Put<uint64_t>(screen.manager + 0x650, screen.stamps + 0x50);
        // The medallion, as on the console: placed, no record
        game.memory.Put<uint64_t>(screen.manager + 0x480, screen.medallion);
        PutMark(game, screen.medallion, game.mainBase + 0x2B875E8, 42, -1, 793.7f, 189.2f, 3721.9f, 0x210, 0);
        // The screen's control
        game.memory.Put<uint64_t>(game.mainBase + 0x2CA63B8, screen.control);
        game.memory.Put<uint64_t>(screen.control, game.mainBase + 0x2B8AAD8);
        game.memory.Map(screen.control, 0x1000);
        game.memory.Put<uint64_t>(screen.control + 0x8D8, screen.manager);
        return screen;
    }

    bool Has(const MapMarks::Result& result, const char* text)
    {
        if (result.details.find(text) != std::string::npos)
            return true;
        std::printf("not in the details: %s\n%s", text, result.details.c_str());
        return false;
    }

    void TestReadScreen()
    {
        FakeGame game;
        // The flags: the medallion, a pin the app wrote (37, not on the screen yet) and the stamp
        BuildGame(game, {{0, 42, 793.7f, 189.2f, 3721.9f}, {1, 37, 80.0f, 140.6f, -620.0f}, {2, 30, -10.0f, 5.0f, -20.0f}});
        const FakeScreen screen = BuildScreen(game);
        const int storesBefore = game.memory.storeCount;
        MapMarks::Result result;
        assert(MapMarks::ReadScreen(game.memory, game.mainBase, LiveGameData::Botw160(), result));
        assert(game.memory.storeCount == storesBefore); // read only
        assert(result.ok && result.failedChecks == 0 && result.error.empty());
        assert(Has(result, "map screen: manager at 5000000000, vtable main+2b86848"));
        assert(Has(result, "control at 5000007000: vtable main+2b8aad8 (expected main+2b8aad8), +0x8d8 the manager: yes"));
        assert(Has(result, "pins: 5 of 5 at 5000001000"));
        assert(Has(result, "pin 1 at 5000002080: vtable main+2b866b0, icon 38, cell 22 18, at 500.00 100.00 600.00, "
                           "state 0x10, index 1, record 5000008000\n"));
        assert(Has(result, "  record at 5000008000: first word main+2c00000"));
        assert(Has(result, "pin 4 at 5000002200: vtable main+2b866b0, icon 41"));
        assert(Has(result, "stamps: 100 of 100 at 5000001100; 1 on the map; the first free one (5000003050) is stamp 1, "
                           "the first not on the map stamp 1"));
        assert(Has(result, "stamp 0 at 5000003000: vtable main+2b874f0, icon 30"));
        assert(result.details.find("stamp 2 at") == std::string::npos); // never used: not told
        assert(Has(result, "the medallion at 5000006000: vtable main+2b875e8, icon 42, cell 23 30"));
        assert(Has(result, "flag slot 0 (icon 42): on the map screen as the medallion"));
        assert(Has(result, "flag slot 1 (icon 37): not on the map screen\n"));
        assert(Has(result, "flag slot 2 (icon 30): on the map screen as stamp 0"));
        assert(Has(result, "pin 1 (icon 38): on the map screen, not in the flags"));
        assert(Has(result, "checks: all passed"));

        // A pin out of place, and a cell that does not match: told, the rest still read
        game.memory.Put<int32_t>(screen.pins + 0x80 * 3 + 0x40, 7);
        game.memory.Put<int32_t>(screen.pins + 0x80 + 0x20, 5);
        MapMarks::Result odd;
        assert(MapMarks::ReadScreen(game.memory, game.mainBase, LiveGameData::Botw160(), odd));
        assert(odd.failedChecks == 1);
        assert(Has(odd, "check failed: pin 3's index"));
        assert(Has(odd, "(the grid of 250 gives 22 18)"));
        assert(Has(odd, "checks: 1 failed"));

        // Another manager (another version): nothing read past it
        game.memory.Put<uint64_t>(screen.manager, game.mainBase + 0x2B86850);
        MapMarks::Result other;
        assert(!MapMarks::ReadScreen(game.memory, game.mainBase, LiveGameData::Botw160(), other));
        assert(!other.error.empty());
        assert(other.details.find("pins:") == std::string::npos);
        // No map screen at all
        FakeGame bare;
        BuildGame(bare, {});
        MapMarks::Result none;
        assert(!MapMarks::ReadScreen(bare.memory, bare.mainBase, LiveGameData::Botw160(), none));
        assert(game.memory.storeCount == storesBefore);
        std::printf("read screen ok\n");
    }
}

int main()
{
    TestReadScreen();
    TestFirstFreePin();
    TestFivePinsUsed();
    TestProxyThatDoesNotMatch();
    TestWriteFailsHalfWay();
    TestNoMarks();
    std::printf("All map marks tests passed\n");
    return 0;
}
