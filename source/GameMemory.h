#pragma once

#include <cstdint>
#include <string>

#include "LiveGameData.h"

// Access to the memory of BotW while it runs behind this app (the app opened from the Album): read, and written only
// for the pin on the game's map (MapMarks, zayr32).
namespace GameMemory
{
    enum class Status
    {
        Ready,
        NotAvailable, // this app is the running application, or the cheat service is missing
        NoGame,
        OtherGame,
        OtherVersion,
        Failed
    };

    // Attaches to the running game through Atmosphere's cheat service. `message` says why it could not.
    Status Open(std::string& message);
    void Close();

    // Valid between a successful Open and Close.
    uint64_t MainBase();
    LiveGameData::Memory& Reader();
}
