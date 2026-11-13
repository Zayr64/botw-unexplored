#pragma once

#include <string>
#include <vector>

#include "SaveLoadDecision.h"
#include <cstdint>
#include <switch.h>
#include <thread>

#include "Chests.h"
#include "Data.h"
#include "Walls.h"

namespace SavefileIO
{
    struct ParsedSaveState
    {
        bool hasDLC = false;
        bool hasPlayerPosition = false;
        float playerX = 0.0f; // game coordinates of the last save, or of Link right now (see below)
        float playerZ = 0.0f;
        bool playerPositionIsLive = false; // read from the running game instead of the last save
        bool playerInsideShrine = false;   // the position is the entrance of the shrine Link is in
        std::vector<uint32_t> clearedShrines;   // by discovery hash (Location_DungeonNNN)
        std::vector<uint32_t> allChestsShrines; // by discovery hash (Location_DungeonNNN)
        std::vector<uint32_t> openedChests;     // flag hashes of the opened chests of the world map, sorted
        bool hasDlcChests = false;              // the chests of the Expansion Pass exist in this game
        std::vector<uint32_t> brokenWalls;        // flag hashes of the walls already broken open, sorted
        std::vector<uint32_t> openedShrineChests; // flag hashes of the opened chests inside the shrines, sorted
        std::vector<uint32_t> doneLocationStates; // locations with the tower activated or the fairy awakened, sorted
        bool balladStarted = false;               // the Champions' Ballad has begun: its shrines can exist
        std::vector<uint32_t> progressFlags;      // the set flags among those the progress panel counts, sorted
        std::vector<Data::Korok*> foundKoroks;
        std::vector<Data::Korok*> missingKoroks;
        std::vector<Data::Shrine*> foundShrines;
        std::vector<Data::Shrine*> missingShrines;
        std::vector<Data::DLCShrine*> foundDLCShrines;
        std::vector<Data::DLCShrine*> missingDLCShrines;
        std::vector<Data::Location*> visitedLocations;
        std::vector<Data::Location*> unexploredLocations;
        std::vector<Data::Hinox*> defeatedHinoxes;
        std::vector<Data::Hinox*> undefeatedHinoxes;
        std::vector<Data::Talus*> defeatedTaluses;
        std::vector<Data::Talus*> undefeatedTaluses;
        std::vector<Data::Molduga*> defeatedMoldugas;
        std::vector<Data::Molduga*> undefeatedMoldugas;
    };

    struct SaveState : ParsedSaveState
    {
        u64 accountUid1 = 0;
        u64 accountUid2 = 0;
        int mostRecentNormalModeFile = -1;
        int mostRecentMasterModeFile = -1;
        int masterModeSlot = 0;
        bool loadedSavefile = false;
        bool gameIsRunning = false;
        bool noSavefileForUser = false;
        bool masterModeFileExists = false;
        bool masterModeFileLoaded = false;
        bool liveSynced = false;   // the progress shown was read from the running game, not from a save backup
        std::string liveMessage;    // with the game running: why its memory could not be read
        int loadedSlot = -1;        // save slot shown (-1 when read from the running game without a backup)
        uint32_t saveTime = 0;      // when that save was made (its backup, when the save has no date); 0 = unknown
    };

    extern SaveState CurrentState;

    extern std::vector<Data::Korok*>& foundKoroks;
    extern std::vector<Data::Korok*>& missingKoroks;
    extern std::vector<Data::Shrine*>& foundShrines;
    extern std::vector<Data::Shrine*>& missingShrines;
    extern std::vector<Data::DLCShrine*>& foundDLCShrines;
    extern std::vector<Data::DLCShrine*>& missingDLCShrines;
    extern std::vector<Data::Location*>& visitedLocations;
    extern std::vector<Data::Location*>& unexploredLocations;
    extern std::vector<Data::Hinox*>& defeatedHinoxes;
    extern std::vector<Data::Hinox*>& undefeatedHinoxes;
    extern std::vector<Data::Talus*>& defeatedTaluses;
    extern std::vector<Data::Talus*>& undefeatedTaluses;
    extern std::vector<Data::Molduga*>& defeatedMoldugas;
    extern std::vector<Data::Molduga*>& undefeatedMoldugas;

    // followGameMode: with the game running, show the mode (normal or master) it is being played in.
    bool LoadGamesave(bool loadMasterMode = false, bool chooseProfile = false, bool followGameMode = false);

    SaveLoadDecision::MountStatus MountSavefile(bool openProfilePicker, SaveState& candidate);
    bool UnmountSavefile();

    bool LoadBackup(bool masterMode, SaveState& candidate);

    uint32_t GetSavefilePlaytime(const std::string& filepath);
    uint32_t GetSavefileTimestamp(const std::string& captionPath);
    bool IsShrineCleared(uint32_t locationHash);
    bool HasAllShrineChests(uint32_t locationHash);
    bool IsChestOpened(uint32_t flagHash);
    bool IsWallBroken(uint32_t flagHash);
    // The chests inside a shrine: how many there are and how many are opened. False for a shrine not in the table.
    bool GetShrineChests(uint32_t locationHash, int& opened, int& total);
    // Whether the tower of this location is activated, or its great fairy awakened (see LocationState.h).
    bool IsLocationStateDone(uint32_t locationHash);
    // The shrines of the Champions' Ballad are not in the world before it begins.
    bool DlcShrinesAppear();
    // Whether a flag the progress panel counts (quests, Divine Beasts, compendium; see Progress.h) is set.
    bool IsProgressFlagSet(uint32_t flagHash);
    // The game has the Expansion Pass: a chest of it opened, its main quest ready or a save in Master Mode.
    bool HasExpansionPass();
    int GetMostRecentSavefile(const std::string& dir, bool masterMode = false); // Needs slash at end of dir

    void CopySavefiles();
    s32 CopyFile(const std::string &srcPath, const std::string &dstPath);

    bool ParseFile(const char* filepath);

    bool DirectoryExists(const std::string& filepath);
    bool FileExists(const std::string& filepath);

    extern u64& AccountUid1;
    extern u64& AccountUid2;

    extern int& MostRecentNormalModeFile;
    extern int& MostRecentMasterModeFile;

    extern bool& LoadedSavefile;
    extern bool& GameIsRunning;
    extern bool& LiveSynced;
    extern bool& NoSavefileForUser;
    extern bool& MasterModeFileExists;
    extern bool& MasterModeFileLoaded;
    extern bool& HasDLC;

    extern int& MasterModeSlot;
};
