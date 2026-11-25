// A save turned into the memory of a running game, read back by LiveGameData and rebuilt by LiveSave, must read
// exactly like the save: every single flag, and the pouch.
//
//   test_live_save [game_data.sav]
//
// Without a save, a made-up one is used (always run); with one (a real save, which never goes in the repository),
// that one too.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "CookData.h"
#include "FakeGameMemory.h"
#include "LiveGameData.h"
#include "LiveSave.h"
#include "SaveFile.h"

using namespace Compendium;

namespace
{
    using Kind = LiveGameData::FlagKind;

    struct Records
    {
        std::map<uint32_t, std::vector<uint32_t>> byHash; // values in file order
    };

    Records Parse(const std::vector<uint8_t>& bytes)
    {
        Records r;
        for (size_t offset = 12; offset + 8 <= bytes.size(); offset += 8)
        {
            uint32_t hash, value;
            std::memcpy(&hash, bytes.data() + offset, 4);
            std::memcpy(&value, bytes.data() + offset + 4, 4);
            r.byHash[hash].push_back(value);
        }
        return r;
    }

    std::vector<uint8_t> Serialize(const Records& r)
    {
        std::vector<uint8_t> out;
        const uint32_t header[3] = {0x471E, 0xFFFFFFFF, 1};
        out.insert(out.end(), reinterpret_cast<const uint8_t*>(header), reinterpret_cast<const uint8_t*>(header) + 12);
        for (const auto& entry : r.byHash)
            for (const uint32_t value : entry.second)
            {
                const uint32_t record[2] = {entry.first, value};
                out.insert(out.end(), reinterpret_cast<const uint8_t*>(record), reinterpret_cast<const uint8_t*>(record) + 8);
            }
        const uint32_t end = 0xFFFFFFFF;
        out.insert(out.end(), reinterpret_cast<const uint8_t*>(&end), reinterpret_cast<const uint8_t*>(&end) + 4);
        return out;
    }

    // The game the save stands for: single flags by their number of records (types the save does not tell are
    // guessed; the f32 the app reads are marked), the pouch as the three arrays. The counts of the pouch are also
    // met as single objects outside the tables, like the elements of arrays in the game.
    void MakeGame(const Records& r, FakeGame& game)
    {
        const uint32_t names = Crc32("PorchItem"), counts = Crc32("PorchItem_Value1"), equipped = Crc32("PorchItem_EquipFlag");
        const uint32_t f32s[] = {Crc32("StaminaMax"), Crc32("WM_BloodyMoonTimer"), Crc32("WM_Time")};
        std::vector<FakeFlag> flags;
        std::vector<bool> listed;
        std::vector<FakeArray> arrays;
        for (const auto& entry : r.byHash)
        {
            const uint32_t hash = entry.first;
            const std::vector<uint32_t>& v = entry.second;
            if (hash == names)
            {
                FakeArray a = {Kind::String64, hash, {}};
                for (size_t i = 0; i + 16 <= v.size(); i += 16)
                {
                    std::vector<uint8_t> text(64, 0);
                    std::memcpy(text.data(), &v[i], 64);
                    a.values.push_back(text);
                }
                arrays.push_back(a);
                continue;
            }
            if (hash == counts || hash == equipped)
            {
                FakeArray a = {hash == counts ? Kind::S32 : Kind::Bool, hash, {}};
                for (const uint32_t value : v)
                {
                    std::vector<uint8_t> bytes(4, 0);
                    std::memcpy(bytes.data(), &value, 4);
                    a.values.push_back(bytes);
                    if (hash == counts)
                    {
                        FakeFlag loose = {Kind::S32, hash, {}};
                        std::memcpy(loose.value, &value, 4);
                        flags.push_back(loose);
                        listed.push_back(false);
                    }
                }
                arrays.push_back(a);
                continue;
            }
            if (v.size() == 8 || v.size() == 3)
            {
                // Texts of 32 and vectors: not used by the app, but the game always has some (the scan needs them).
                FakeFlag flag = {v.size() == 8 ? Kind::String32 : Kind::Vector3f, hash, {}};
                std::memcpy(flag.value, v.data(), v.size() * 4);
                flags.push_back(flag);
                listed.push_back(true);
                continue;
            }
            if (v.size() != 1)
                continue; // other arrays: not used by the app
            FakeFlag flag = {Kind::S32, hash, {}};
            for (const uint32_t f : f32s)
                if (f == hash)
                    flag.kind = Kind::F32;
            if (flag.kind == Kind::S32 && v[0] <= 1)
                flag.kind = Kind::Bool;
            if (flag.kind == Kind::Bool)
                flag.value[0] = static_cast<uint8_t>(v[0] | 0x20); // category bits above bit 0, as in the game
            else
                std::memcpy(flag.value, &v[0], 4);
            flags.push_back(flag);
            listed.push_back(true);
        }
        game.Build(LiveGameData::Botw160(), flags, 4096, 0, &listed);
        game.BuildArrays(LiveGameData::Botw160(), arrays);
    }

    void RoundTrip(const std::vector<uint8_t>& original, const char* what)
    {
        const Records records = Parse(original);
        FakeGame game;
        MakeGame(records, game);

        LiveSave live;
        LiveGameData::Options options;
        options.f32 = true;
        options.arrays = LiveArrays();
        options.arrayCount = LiveArrayCount();
        LiveGameData::Report report;
        const bool ok = LiveGameData::Scan(game.memory, game.mainBase, LiveGameData::Botw160(), live, report, options);
        if (!ok || !report.f32 || report.arraysFound != 3)
        {
            std::printf("%s\n%s\n", report.error.c_str(), report.details.c_str());
            std::fflush(stdout);
        }
        assert(ok && report.f32 && report.arraysFound == 3);

        std::vector<uint8_t> rebuilt;
        std::string why;
        assert(live.Build(rebuilt, why) && why.empty());

        SaveFile a, b;
        assert(a.Load(original.data(), original.size()) && b.Load(rebuilt.data(), rebuilt.size()));

        // Every single flag the app could read.
        int singles = 0;
        for (const auto& entry : records.byHash)
        {
            if (entry.second.size() != 1)
                continue;
            int32_t va = 0, vb = 0;
            assert(a.GetS32(entry.first, va));
            if (!b.GetS32(entry.first, vb) || va != vb)
            {
                std::printf("%s: flag %08x is %d in the save and %d live\n", what, entry.first, va, vb);
                assert(false);
            }
            ++singles;
        }
        float sa = 0, sb = 0;
        assert(a.GetF32("StaminaMax", sa) == b.GetF32("StaminaMax", sb) && sa == sb);

        // The pouch.
        std::vector<PouchItem> pa(PouchSize), pb(PouchSize);
        const int na = a.ReadPouch(pa.data(), PouchSize), nb = b.ReadPouch(pb.data(), PouchSize);
        assert(na == nb);
        for (int i = 0; i < na; i++)
            assert(pa[i].hash == pb[i].hash && std::strcmp(pa[i].name, pb[i].name) == 0 && pa[i].value == pb[i].value &&
                   pa[i].equipped == pb[i].equipped);
        std::printf("%s: %d single flags and %d pouch items read the same live (f32 %d, %zu records)\n", what, singles, na,
                    report.objects[static_cast<int>(Kind::F32)], live.RecordCount());
    }

    // A small save: play time, rupees, hearts, stamina, a few hundred flags and a pouch of 37 items.
    std::vector<uint8_t> MadeUpSave()
    {
        Records r;
        r.byHash[0x73C29681].push_back(76471); // play time
        r.byHash[Crc32("CurrentRupee")].push_back(1249);
        r.byHash[Crc32("MaxHartValue")].push_back(60);
        float stamina = 2200.0f, timer = 665.919f, time = 305.372f;
        uint32_t bits;
        std::memcpy(&bits, &stamina, 4);
        r.byHash[Crc32("StaminaMax")].push_back(bits);
        std::memcpy(&bits, &timer, 4);
        r.byHash[Crc32("WM_BloodyMoonTimer")].push_back(bits);
        std::memcpy(&bits, &time, 4);
        r.byHash[Crc32("WM_Time")].push_back(bits);
        const char map[32] = "MainField";
        uint32_t text[8];
        std::memcpy(text, map, 32);
        r.byHash[0xD913B769].assign(text, text + 8); // the type of map the game is on
        const float position[3] = {-997.4f, 187.0f, 1603.1f};
        uint32_t vector[3];
        std::memcpy(vector, position, 12);
        r.byHash[0xA40BA103].assign(vector, vector + 3); // PlayerSavePos
        uint32_t state = 99;
        for (int i = 0; i < 600; i++)
        {
            state = state * 1664525u + 1013904223u;
            r.byHash[state].push_back(i % 3 == 0 ? 1 : (i % 3 == 1 ? 0 : static_cast<uint32_t>(i * 7)));
        }
        std::vector<uint32_t>& names = r.byHash[Crc32("PorchItem")];
        std::vector<uint32_t>& counts = r.byHash[Crc32("PorchItem_Value1")];
        std::vector<uint32_t>& equipped = r.byHash[Crc32("PorchItem_EquipFlag")];
        for (int i = 0; i < PouchSize; i++)
        {
            char name[64] = {};
            if (i < 37)
                std::snprintf(name, sizeof(name), "Item_Fruit_%c", 'A' + i % 26);
            uint32_t chunk[16];
            std::memcpy(chunk, name, 64);
            names.insert(names.end(), chunk, chunk + 16);
            counts.push_back(i < 37 ? static_cast<uint32_t>(i + 1) : 0);
            equipped.push_back(i == 4 ? 1 : 0);
        }
        return Serialize(r);
    }
}

int main(int argc, char** argv)
{
    RoundTrip(MadeUpSave(), "made-up save");

    // Not ready: a game still at the title screen (no play time), and one whose pouch is not there.
    {
        LiveSave live;
        std::vector<uint8_t> out;
        std::string why;
        assert(!live.Build(out, why) && !why.empty());
    }

    if (argc > 1)
    {
        std::ifstream file(argv[1], std::ios::binary);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        assert(bytes.size() > 1000);
        RoundTrip(bytes, argv[1]);
    }
    std::printf("live save: ok\n");
    return 0;
}
