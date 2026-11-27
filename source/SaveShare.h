#pragma once

#include <cstdint>
#include <string>
#include <vector>

// BotW Companion (the single app, phase 1): the save, or the running game, read once for the map and the
// Compendium's tabs. The map reads first (SavefileIO::LoadGamesave): with the game closed it keeps the bytes of the
// Normal Mode game_data.sav it parsed; with the game running its scan of the game's memory also makes the save the
// tabs read (the pouch's arrays come along, Compendium::LiveSave). The tabs then take it from here instead of
// mounting the save or scanning the memory again (compendium/app/SaveSource.cpp). Nothing here when the map could
// not read: the tabs read by themselves, as the BotW Compendium does.
namespace SaveShare
{
    enum class From
    {
        None,       // nothing shared: the tabs read by themselves
        Save,       // the console's save, read with the game closed
        Live,       // a save made from the running game's memory
        LiveFailed, // the game runs and its memory could not give the tabs' save: they use a copy on the SD card
    };

    struct Shared
    {
        From from = From::None;
        std::vector<uint8_t> bytes; // game_data.sav (Save, Live)
        int slot = -1;              // Save
        uint32_t saveTime = 0;      // Save: caption.sav's time, 0 if unknown
        std::string profile;        // "<uid1> - <uid2>", the folder name of the copies
        std::string message;        // LiveFailed: why
    };

    void Clear();
    void SetSave(std::vector<uint8_t> bytes, int slot, uint32_t saveTime, const std::string& profile);
    void SetLive(std::vector<uint8_t> bytes, const std::string& profile);
    void SetLiveFailed(const std::string& message, const std::string& profile);
    const Shared& Get();

    // The folder name of a profile's copies, as both apps name it.
    std::string ProfileName(uint64_t uid1, uint64_t uid2);
}
