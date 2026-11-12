#pragma once

#include <cstdint>
#include <string>

#include "LiveGameData.h"

// Read-only access to the memory of BotW while it runs behind this app (the app opened from the Album).
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
