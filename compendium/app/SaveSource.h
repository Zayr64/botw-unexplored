#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Where the app gets game_data.sav from.
//
// With BotW closed, the newest Normal Mode slot of the user's save, opened read-only; a copy goes to
// sdmc:/switch/botw-compendium/save/ for the times the game is open. With BotW open (the app started from the Album),
// the save cannot be opened: the flags are read from the game's memory, as they are at this moment (BotW Unexplored's
// reader, through Atmosphere's cheat service, read-only). When that cannot be done, the newest copy on the SD card is
// used: this app's, or the one BotW Unexplored keeps.
namespace SaveSource
{
    enum class Kind
    {
        None,
        Game,           // read from the console's save just now
        Live,           // read from the running game's memory just now
        Copy,           // this app's copy on the SD card
        UnexploredCopy, // BotW Unexplored's copy on the SD card
    };

    struct Loaded
    {
        Kind kind = Kind::None;
        std::vector<uint8_t> bytes;
        uint32_t saveTime = 0; // caption.sav time (seconds since 1970, as the game writes it), 0 if unknown
        int slot = -1;
        bool gameRunning = false;
        std::string message;     // why nothing could be read, for the screen and the log
        std::string liveMessage; // with the game running: why its memory could not be read (a copy is used instead)
    };

    Loaded Load();

    const char* KindText(Kind kind);
}
