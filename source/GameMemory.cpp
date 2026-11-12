#include "GameMemory.h"

#include <cstdio>
#include <cstring>

#include <switch.h>

#include "CheatService.h"

namespace
{
    constexpr uint64_t BotwTitleId = 0x01007EF00011E000ULL;

    uint64_t s_MainBase = 0;

    class CheatServiceReader : public LiveGameData::Memory
    {
    public:
        bool Read(uint64_t address, void* out, size_t size) override
        {
            return cheatServiceRead(address, out, size) == 0;
        }
    };

    CheatServiceReader s_Reader;
}

GameMemory::Status GameMemory::Open(std::string& message)
{
    s_MainBase = 0;

    // Started from a game's title or a forwarder, this app is the application itself: no game can be running.
    const AppletType appletType = appletGetAppletType();
    if (appletType == AppletType_Application || appletType == AppletType_SystemApplication)
    {
        message = "no game is running";
        return Status::NotAvailable;
    }
    if (!cheatServiceExists())
    {
        message = "Atmosphere's cheat service (dmnt) is not running";
        return Status::NotAvailable;
    }
    if (!cheatServiceApplicationRunning())
    {
        message = "no game is running";
        return Status::NoGame;
    }
    if (cheatServiceOpen() != 0)
    {
        message = "could not open Atmosphere's cheat service";
        return Status::Failed;
    }

    cheatServiceAttach(); // fails when already attached; the metadata tells whether it worked
    CheatServiceMetadata metadata;
    std::memset(&metadata, 0, sizeof(metadata));
    if (cheatServiceGetMetadata(&metadata) != 0)
    {
        cheatServiceClose();
        message = "could not attach to the game";
        return Status::Failed;
    }
    if (metadata.title_id != BotwTitleId)
    {
        cheatServiceClose();
        message = "the running game is not BotW";
        return Status::OtherGame;
    }
    if (std::memcmp(metadata.main_nso_build_id, LiveGameData::BotwBuildId160, sizeof(LiveGameData::BotwBuildId160)) != 0)
    {
        char text[80];
        std::snprintf(text, sizeof(text), "only BotW 1.6.0 can be read (this is build %02X%02X%02X%02X%02X%02X%02X%02X)",
                      metadata.main_nso_build_id[0], metadata.main_nso_build_id[1], metadata.main_nso_build_id[2],
                      metadata.main_nso_build_id[3], metadata.main_nso_build_id[4], metadata.main_nso_build_id[5],
                      metadata.main_nso_build_id[6], metadata.main_nso_build_id[7]);
        cheatServiceClose();
        message = text;
        return Status::OtherVersion;
    }

    s_MainBase = metadata.main_nso.base;
    message.clear();
    return Status::Ready;
}

void GameMemory::Close()
{
    cheatServiceClose();
    s_MainBase = 0;
}

uint64_t GameMemory::MainBase()
{
    return s_MainBase;
}

LiveGameData::Memory& GameMemory::Reader()
{
    return s_Reader;
}
