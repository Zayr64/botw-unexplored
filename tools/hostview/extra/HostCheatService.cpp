// Host fake of source/CheatService.c: a "running game" whose memory is built from a save file.
//
//   HOSTVIEW_LIVE_SAVE   game_data.sav with the state the running game should have (unset: no game)
//   HOSTVIEW_LIVE_POS    "x,y,z" of Link (unset: Link does not exist yet)
//   HOSTVIEW_LIVE_MAP    "CDungeon:Dungeon042" to put the game inside a shrine
//   HOSTVIEW_LIVE_BREAK  1: the flag objects look like something else (another game version)
//   HOSTVIEW_LIVE_HARD   1: the game runs in Master Mode
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
        for (const auto& entry : values)
        {
            FakeFlag flag = {LiveGameData::FlagKind::Bool, entry.first, {}};
            const std::vector<uint32_t>& list = entry.second;
            if (list.size() == 1 && list[0] <= 1)
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
