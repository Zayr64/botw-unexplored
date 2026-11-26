// Host fake of source/CheatService.c: a "running game" whose memory is built from a save file.
//
//   HOSTVIEW_LIVE_SAVE   game_data.sav with the state the running game should have (unset: no game)
//   HOSTVIEW_LIVE_POS    "x,y,z" of Link (unset: Link does not exist yet)
//   HOSTVIEW_LIVE_MAP    "CDungeon:Dungeon042" to put the game inside a shrine
//   HOSTVIEW_LIVE_BREAK  1: the flag objects look like something else (another game version)
//   HOSTVIEW_LIVE_HARD   1: the game runs in Master Mode
//   HOSTVIEW_LIVE_NO_POUCH 1: the pouch's arrays are missing
#include "CheatService.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <vector>

#include "FakeGameMemory.h"
#include "LiveGameData.h"

namespace
{
    FakeGame* s_Game = nullptr;

    uint32_t Crc32(const char* text)
    {
        uint32_t crc = 0xFFFFFFFFu;
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; p++)
        {
            crc ^= *p;
            for (int k = 0; k < 8; k++)
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
        return ~crc;
    }

    // The save does not tell the type of a flag: the f32 the apps read, and the arrays of the pouch, by name.
    bool IsF32(uint32_t hash)
    {
        static const uint32_t f32s[] = {Crc32("StaminaMax"), Crc32("WM_BloodyMoonTimer"), Crc32("WM_Time")};
        for (const uint32_t f : f32s)
            if (f == hash)
                return true;
        return false;
    }

    bool EnvOn(const char* name)
    {
        const char* value = getenv(name);
        return value && value[0] == '1';
    }

    FakeGame* Game()
    {
        if (s_Game)
            return s_Game;
        const char* path = getenv("HOSTVIEW_LIVE_SAVE");
        if (!path)
            return nullptr;
        std::ifstream file(path, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (bytes.size() < 12)
        {
            fprintf(stderr, "hostview: cannot read %s\n", path);
            exit(2);
        }

        // hash -> values, in file order
        std::map<uint32_t, std::vector<uint32_t>> values;
        for (size_t offset = 12; offset + 8 <= bytes.size(); offset += 8)
        {
            uint32_t hash, value;
            std::memcpy(&hash, bytes.data() + offset, 4);
            std::memcpy(&value, bytes.data() + offset + 4, 4);
            values[hash].push_back(value);
        }

        std::vector<FakeFlag> flags;
        std::vector<FakeArray> arrays;
        const uint32_t pouchNames = Crc32("PorchItem"), pouchCounts = Crc32("PorchItem_Value1"),
                       pouchEquipped = Crc32("PorchItem_EquipFlag");
        const uint32_t markIcons = Crc32("MapApp_MapIconNo"), markPositions = Crc32("MapApp_MapIconPos");
        for (const auto& entry : values)
        {
            // The marks of the game's map (zayr32): 200 icons and 200 positions
            if (entry.first == markIcons || entry.first == markPositions)
            {
                const bool icons = entry.first == markIcons;
                FakeArray array = {icons ? LiveGameData::FlagKind::S32 : LiveGameData::FlagKind::Vector3f, entry.first, {}};
                const size_t per = icons ? 1 : 3;
                for (size_t i = 0; i + per <= entry.second.size(); i += per)
                {
                    std::vector<uint8_t> bytes(per * 4, 0);
                    std::memcpy(bytes.data(), &entry.second[i], per * 4);
                    array.values.push_back(bytes);
                }
                arrays.push_back(array);
                continue;
            }
            FakeFlag flag = {LiveGameData::FlagKind::Bool, entry.first, {}};
            const std::vector<uint32_t>& list = entry.second;
            if (entry.first == pouchNames || entry.first == pouchCounts || entry.first == pouchEquipped)
            {
                // The pouch, as the game keeps it: three arrays of 420 (names of 64 characters, 16 records each).
                FakeArray array = {entry.first == pouchNames ? LiveGameData::FlagKind::String64
                                   : entry.first == pouchCounts ? LiveGameData::FlagKind::S32
                                                                : LiveGameData::FlagKind::Bool,
                                   entry.first, {}};
                const size_t per = entry.first == pouchNames ? 16 : 1;
                for (size_t i = 0; i + per <= list.size(); i += per)
                {
                    std::vector<uint8_t> bytes(per * 4, 0);
                    std::memcpy(bytes.data(), &list[i], per * 4);
                    array.values.push_back(bytes);
                }
                arrays.push_back(array);
                continue;
            }
            if (list.size() == 1 && IsF32(entry.first))
            {
                flag.kind = LiveGameData::FlagKind::F32;
                std::memcpy(flag.value, &list[0], 4);
            }
            else if (list.size() == 1 && list[0] <= 1)
                flag.value[0] = static_cast<uint8_t>(list[0] | 0x20);
            else if (list.size() == 1)
            {
                flag.kind = LiveGameData::FlagKind::S32;
                std::memcpy(flag.value, &list[0], 4);
            }
            else if (list.size() == 8)
            {
                flag.kind = LiveGameData::FlagKind::String32;
                std::memcpy(flag.value, list.data(), 32);
            }
            else if (list.size() == 3)
            {
                flag.kind = LiveGameData::FlagKind::Vector3f;
                std::memcpy(flag.value, list.data(), 12);
            }
            else
                continue;
            flags.push_back(flag);
        }

        if (const char* map = getenv("HOSTVIEW_LIVE_MAP"))
        {
            const std::string text(map);
            const size_t colon = text.find(':');
            for (FakeFlag& flag : flags)
            {
                if (flag.kind != LiveGameData::FlagKind::String32)
                    continue;
                if (flag.hash == 0xD913B769 || flag.hash == 0x0BEE9E46)
                {
                    std::memset(flag.value, 0, sizeof(flag.value));
                    const std::string part = flag.hash == 0xD913B769 ? text.substr(0, colon) : text.substr(colon + 1);
                    std::memcpy(flag.value, part.data(), std::min<size_t>(part.size(), 31));
                }
            }
        }
        // A flag that only exists in memory.
        FakeFlag hardMode = {LiveGameData::FlagKind::Bool, 0xE51256E2, {}};
        hardMode.value[0] = EnvOn("HOSTVIEW_LIVE_HARD") ? 1 : 0;
        flags.push_back(hardMode);

        s_Game = new FakeGame();
        s_Game->Build(LiveGameData::Botw160(), flags, 4096, EnvOn("HOSTVIEW_LIVE_BREAK") ? 0x40 : 0);
        // HOSTVIEW_LIVE_NO_POUCH 1: the arrays are not there (the pouch cannot be read live).
        if (!EnvOn("HOSTVIEW_LIVE_NO_POUCH"))
            s_Game->BuildArrays(LiveGameData::Botw160(), arrays, EnvOn("HOSTVIEW_LIVE_BREAK") ? 0x40 : 0);
        if (const char* position = getenv("HOSTVIEW_LIVE_POS"))
        {
            float x = 0, y = 0, z = 0;
            if (sscanf(position, "%f,%f,%f", &x, &y, &z) == 3)
                s_Game->SetPlayerPosition(LiveGameData::Botw160(), x, y, z);
        }
        printf("hostview: fake game with %zu flags from %s\n", flags.size(), path);
        return s_Game;
    }
}

extern "C" bool cheatServiceExists(void) { return true; }
extern "C" bool cheatServiceApplicationRunning(void) { return Game() != nullptr; }
extern "C" uint32_t cheatServiceOpen(void) { return 0; }
extern "C" void cheatServiceClose(void) {}
extern "C" uint32_t cheatServiceAttach(void) { return 0; }

extern "C" uint32_t cheatServiceGetMetadata(CheatServiceMetadata* out)
{
    FakeGame* game = Game();
    if (!game)
        return 1;
    std::memset(out, 0, sizeof(*out));
    out->title_id = 0x01007EF00011E000ULL;
    out->main_nso.base = game->mainBase;
    out->main_nso.size = 0x3483000;
    std::memcpy(out->main_nso_build_id, LiveGameData::BotwBuildId160, 8);
    if (EnvOn("HOSTVIEW_LIVE_OTHER_VERSION"))
        out->main_nso_build_id[0] ^= 0xFF;
    return 0;
}

extern "C" uint32_t cheatServiceRead(uint64_t address, void* buffer, size_t size)
{
    FakeGame* game = Game();
    return game && game->memory.Read(address, buffer, size) ? 0 : 1;
}

extern "C" uint32_t cheatServiceWrite(uint64_t address, const void* buffer, size_t size)
{
    FakeGame* game = Game();
    return game && game->memory.Store(address, buffer, size) ? 0 : 1;
}
