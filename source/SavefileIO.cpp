#include "SavefileIO.h"

#include <fstream>
#include <dirent.h>
#include <iostream>
#include <map>
#include <limits>
#include <stdio.h>

#include <switch.h>

#include "Accounts.h"
#include "Map.h"
#include "Dialog.h"
#include "GameMemory.h"
#include "LiveGameData.h"
#include "Log.h"
#include "SaveLoadDecision.h"
#include "SaveParser.h"
#include "ShrineFlags.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <sys/stat.h>
#include <unordered_map>

namespace
{
    bool saveMounted = false;

    bool ParseFileIntoState(const char* filepath, SavefileIO::ParsedSaveState& parsedState);

    // What the running game holds in memory.
    struct LiveState
    {
        SavefileIO::ParsedSaveState parsed;
        bool masterMode = false;
        std::string message; // why it could not be read
    };
    bool LoadLive(LiveState& live);

    // When a file was last written (POSIX time), 0 when unknown.
    uint32_t FileTime(const std::string& path)
    {
        struct stat info;
        if (stat(path.c_str(), &info) != 0 || info.st_mtime <= 0)
            return 0;
        return static_cast<uint32_t>(info.st_mtime);
    }

    bool ReadFileBuffer(const char* filepath, std::vector<uint8_t>& buffer)
    {
        if (!SavefileIO::FileExists(filepath))
            return false;

        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        const std::streamoff length = file.tellg();
        if (!file || length < 0 || length > static_cast<std::streamoff>(std::numeric_limits<uint32_t>::max()))
            return false;

        buffer.resize(static_cast<size_t>(length));
        file.seekg(0, std::ios::beg);
        return static_cast<bool>(file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size())));
    }

    void ApplyParsedState(SavefileIO::SaveState& state, const SavefileIO::ParsedSaveState& parsedState)
    {
        static_cast<SavefileIO::ParsedSaveState&>(state) = parsedState;
    }

    void CommitState(const SavefileIO::SaveState& candidate)
    {
        SavefileIO::CurrentState = candidate;
    }

    class SaveMountGuard
    {
    public:
        explicit SaveMountGuard(bool mounted) : m_Mounted(mounted) {}
        ~SaveMountGuard()
        {
            if (m_Mounted)
                SavefileIO::UnmountSavefile();
        }

    private:
        bool m_Mounted;
    };
}

bool SavefileIO::LoadGamesave(bool loadMasterMode, bool chooseProfile, bool followGameMode)
{
    Log("Loading gamesave. Master mode: " + std::string(loadMasterMode ? "true" : "false") + 
       ", Choose profile (instead of automatic): " + std::string(chooseProfile ? "true" : "false"));

    SaveState candidate = CurrentState;
    const SaveLoadDecision::MountStatus mountStatus = MountSavefile(chooseProfile, candidate);

    Log("Mount status:", static_cast<int>(mountStatus));

    if (mountStatus == SaveLoadDecision::MountStatus::Success)
    {
        SaveMountGuard mountGuard(true);
        const int saveSlot = loadMasterMode ? candidate.mostRecentMasterModeFile : candidate.mostRecentNormalModeFile;
        if (saveSlot == -1)
        {
            Log(loadMasterMode ? "No Master Mode savefile exists" : "No Normal Mode savefile exists");
            Map::m_NoSavefileDialog->SetOpen(true);
            return false;
        }

        ParsedSaveState parsedState;
        const std::string path = "save:/" + std::to_string(saveSlot) + "/game_data.sav";
        if (!ParseFileIntoState(path.c_str(), parsedState))
        {
            Log(loadMasterMode ? "Master Mode savefile failed to parse" : "Normal Mode savefile failed to parse");
            Map::m_NoSavefileDialog->SetOpen(true);
            return false;
        }

        ApplyParsedState(candidate, parsedState);
        candidate.loadedSavefile = true;
        candidate.gameIsRunning = false;
        candidate.noSavefileForUser = false;
        candidate.masterModeFileLoaded = loadMasterMode;
        candidate.liveSynced = false;
        candidate.liveMessage.clear();
        candidate.loadedSlot = saveSlot;
        candidate.saveTime = GetSavefileTimestamp("save:/" + std::to_string(saveSlot) + "/caption.sav");
        CommitState(candidate);
        Map::m_GameRunningDialog->SetOpen(false);
        Map::m_MasterModeDialog->SetOpen(false);
        Map::m_NoSavefileDialog->SetOpen(false);

        if (!chooseProfile)
            CopySavefiles();
        return true;
    }
    // Failed to mount it. Can happen if no profile was choosen, or some other account thing went wrong 
    if (mountStatus == SaveLoadDecision::MountStatus::NotSelected) {
        Log("Profile picker closed without a selection");

        return false;
    }
    if (mountStatus == SaveLoadDecision::MountStatus::SaveInaccessible) {
        Log("Game is running. Reading its memory...");

        // The game's own flags are newer than any save. The backup stays as the fallback (and tells which
        // slots exist) when the memory cannot be read or the game runs the other mode.
        LiveState live;
        const bool liveRead = LoadLive(live);
        if (liveRead && followGameMode)
            loadMasterMode = live.masterMode;
        const bool useLive = liveRead && live.masterMode == loadMasterMode;

        Log("Loading backup...");
        const bool loadedBackupSuccess = LoadBackup(loadMasterMode, candidate);

        if (!loadedBackupSuccess && !useLive)
        {
            CurrentState.liveMessage = live.message; // shown by the dialog
            Map::m_GameRunningDialog->SetOpen(true);
            return false;
        }
        if (useLive)
        {
            ApplyParsedState(candidate, live.parsed);
            if (!loadedBackupSuccess)
            {
                candidate.loadedSlot = -1;
                candidate.saveTime = 0;
            }
        }
        candidate.liveSynced = useLive;
        candidate.liveMessage = useLive ? std::string()
                              : liveRead ? std::string("the game is running the other mode") : live.message;
        Log(useLive ? std::string("Showing the progress read from the running game")
                    : "Showing the save backup: " + candidate.liveMessage);
        candidate.loadedSavefile = true;
        candidate.gameIsRunning = true;
        candidate.noSavefileForUser = false;
        candidate.masterModeFileLoaded = loadMasterMode;
        CommitState(candidate);
        Map::m_GameRunningDialog->SetOpen(false);
        Map::m_MasterModeDialog->SetOpen(false);
        Map::m_NoSavefileDialog->SetOpen(false);
        return true;
    }
    if (mountStatus == SaveLoadDecision::MountStatus::NoSave) {
        Log("The selected user has no save data");

        Map::m_NoSavefileDialog->SetOpen(true);
        
        return false;
    }

    Log("Unable to access account or profile picker");
    return false;
}

SaveLoadDecision::MountStatus SavefileIO::MountSavefile(bool openProfilePicker, SaveState& candidate)
{
    if (saveMounted)
    {
        Log("Refusing to mount an already mounted save");
        return SaveLoadDecision::MountStatus::AccountError;
    }

    Result rc = 0;
    const u64 botwId = 0x01007ef00011e000ULL;
    AccountUid uid = {candidate.accountUid1, candidate.accountUid2};

    if (!openProfilePicker)
    {
        if (accountUidIsValid(&uid))
        {
            Result rc = fsdevMountSaveDataReadOnly("save", botwId, uid);
            if (R_FAILED(rc))
            {
                Log("Failed to mount save (cached user)");
                candidate.gameIsRunning = true;
                return SaveLoadDecision::MountStatus::SaveInaccessible;
            }
            saveMounted = true;

            Log("Using cached account");

            candidate.mostRecentNormalModeFile = GetMostRecentSavefile("save:/", false);
            candidate.mostRecentMasterModeFile = GetMostRecentSavefile("save:/", true);
            candidate.masterModeFileExists = candidate.mostRecentMasterModeFile != -1;

            if (candidate.mostRecentNormalModeFile == -1)
            {
                Log("No normal mode file exists (cached user)");
                UnmountSavefile();
                return SaveLoadDecision::MountStatus::NoSave;
            }

            return SaveLoadDecision::MountStatus::Success;
        } else {
            Log("No cached profile available");
        }
    }
    
    // Required for getting users
    rc = accountInitialize(AccountServiceType_Administrator);
    if (R_FAILED(rc)) {
        Log("accountInitialize() failed");
        return SaveLoadDecision::MountStatus::AccountError;
    }

    if (openProfilePicker)
    {
        const Accounts::ProfileSelection selection = Accounts::RequestProfileSelection();
        if (selection.status == Accounts::ProfileSelectionStatus::NotSelected)
        {
            accountExit();
            return SaveLoadDecision::MountStatus::NotSelected;
        }
        if (selection.status != Accounts::ProfileSelectionStatus::Selected)
        {
            accountExit();
            return SaveLoadDecision::MountStatus::AccountError;
        }
        uid = selection.uid;
    }
    else if (!accountUidIsValid(&uid))
    {
        rc = accountGetLastOpenedUser(&uid);
        bool couldGetUserAutomatically = false;

        if (R_SUCCEEDED(rc)) {
            // Check if there is a user that last opened an app (ex. if you have restarted the console)
            couldGetUserAutomatically = accountUidIsValid(&uid);
            if (!couldGetUserAutomatically) 
            {
                Log("No last user available.");
                
                // Try to get the user that was used to launch the app when holding down R
                rc = accountGetPreselectedUser(&uid);
                if (R_FAILED(rc))
                {
                    couldGetUserAutomatically = false;
                    Log("No user used to launch the app");
                } else if (accountUidIsValid(&uid))
                {
                    couldGetUserAutomatically = true;
                    Log("Got user used to launch the app");
                }
            } else {
                Log("Using last user used to launch app");
            }
        }

        if (!couldGetUserAutomatically) 
        {
            Log("Opening profile picker");
            const Accounts::ProfileSelection selection = Accounts::RequestProfileSelection();
            if (selection.status == Accounts::ProfileSelectionStatus::NotSelected)
            {
                accountExit();
                return SaveLoadDecision::MountStatus::NotSelected;
            }
            if (selection.status != Accounts::ProfileSelectionStatus::Selected)
            {
                accountExit();
                return SaveLoadDecision::MountStatus::AccountError;
            }
            uid = selection.uid;
        }
    }

    accountExit();

    if (!accountUidIsValid(&uid))
    {
        Log("No valid account uid was selected");
        return SaveLoadDecision::MountStatus::AccountError;
    } else {
        Log("Valid account uid from whatever profile selection method");
    }

    candidate.accountUid1 = uid.uid[0];
    candidate.accountUid2 = uid.uid[1];

    FsSaveDataInfoReader reader;
    FsSaveDataInfo info;
    s64 total_entries = 0;

    bool hasBotwSavedata = false;
    bool readerFailed = false;

    Result res = fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User);
    if (R_FAILED(res)) {
        Log("fsOpenSaveDataInfoReader() failed");
        return SaveLoadDecision::MountStatus::AccountError;
    }

    while (true) {
        res = fsSaveDataInfoReaderRead(&reader, &info, 1, &total_entries);
        if (R_FAILED(res)) {
            readerFailed = true;
            break;
        }
        if (total_entries == 0) {
            break;
        }

        if (info.save_data_type == FsSaveDataType_Account) {
            // Check if the save is botw and belongs to the selected user
            if (info.application_id == botwId && info.uid.uid[0] == uid.uid[0] && info.uid.uid[1] == uid.uid[1])
            {
                hasBotwSavedata = true;
                Log("User has botw savedata");
                break;
            }
        }
    }

    fsSaveDataInfoReaderClose(&reader);

    if (readerFailed)
    {
        Log("fsSaveDataInfoReaderRead() failed");
        return SaveLoadDecision::MountStatus::AccountError;
    }

    if (!hasBotwSavedata)
    {
        Log("User has no botw save data");
        return SaveLoadDecision::MountStatus::NoSave;
    }

    rc = fsdevMountSaveDataReadOnly("save", botwId, uid);
    if (R_FAILED(rc))
    {
        Log("Botw is running. Failed to mount save (or fsdevMountSaveDataReadOnly() failed, who knows)");
        candidate.gameIsRunning = true;
        return SaveLoadDecision::MountStatus::SaveInaccessible;
    }
    saveMounted = true;

    candidate.mostRecentNormalModeFile = GetMostRecentSavefile("save:/", false);
    candidate.mostRecentMasterModeFile = GetMostRecentSavefile("save:/", true);
    candidate.masterModeFileExists = candidate.mostRecentMasterModeFile != -1;

    if (candidate.mostRecentNormalModeFile == -1)
    {
        Log("No savefile exists, even though savedata exists");
        UnmountSavefile();
        return SaveLoadDecision::MountStatus::NoSave;
    }

    return SaveLoadDecision::MountStatus::Success;
}

bool SavefileIO::UnmountSavefile()
{
    if (!saveMounted)
        return true;

    if (R_FAILED(fsdevUnmountDevice("save")))
    {
        Log("Failed to unmount save");
        return false;
    }

    saveMounted = false;
    return true;
}

bool SavefileIO::LoadBackup(bool masterMode, SaveState& candidate)
{
    std::string profileIdStr = std::to_string(candidate.accountUid1) + " - " + std::to_string(candidate.accountUid2);
    std::string savesFolder = "sdmc:/switch/botw-unexplored/saves/" + profileIdStr + "/";

    if (!DirectoryExists("sdmc:/switch/botw-unexplored"))
        return false;
    if (!DirectoryExists("sdmc:/switch/botw-unexplored/saves"))
        return false;
    if (!DirectoryExists(savesFolder))
    {
        Log("No backups exist for this user");
        return false;
    }

    // Get most recent savefile
    const int mostRecentNormalModeFile = GetMostRecentSavefile(savesFolder, false);
    const int mostRecentMasterModeFile = GetMostRecentSavefile(savesFolder, true);

    // So savefile exists
    if (mostRecentNormalModeFile == -1)
        return false;
    if (mostRecentMasterModeFile == -1 && masterMode)
        return false;

    const int saveSlot = masterMode ? mostRecentMasterModeFile : mostRecentNormalModeFile;
    std::string savefilePath = savesFolder + std::to_string(saveSlot) + "/game_data.sav";

    ParsedSaveState parsedState;
    if (!ParseFileIntoState(savefilePath.c_str(), parsedState))
        return false;

    ApplyParsedState(candidate, parsedState);
    candidate.mostRecentNormalModeFile = mostRecentNormalModeFile;
    candidate.mostRecentMasterModeFile = mostRecentMasterModeFile;
    candidate.masterModeFileExists = mostRecentMasterModeFile != -1;
    candidate.loadedSlot = saveSlot;
    // Backups made before zayr8 have no caption.sav: then the time the backup was made.
    candidate.saveTime = GetSavefileTimestamp(savesFolder + std::to_string(saveSlot) + "/caption.sav");
    if (candidate.saveTime == 0)
        candidate.saveTime = FileTime(savefilePath);
    return true;
}

uint32_t SavefileIO::GetSavefilePlaytime(const std::string& filepath)
{
    std::vector<uint8_t> buffer;
    SaveParser::ParsedSaveState parsedState;
    if (!ReadFileBuffer(filepath.c_str(), buffer) || !SaveParser::ParseSaveBuffer(buffer.data(), buffer.size(), parsedState))
        return 0;
    return parsedState.playtime;
}

uint32_t SavefileIO::GetSavefileTimestamp(const std::string& captionPath)
{
    // caption.sav keeps the save time as a POSIX timestamp at offset 0x10.
    FILE* file = fopen(captionPath.c_str(), "rb");
    if (file == nullptr)
        return 0;
    uint8_t bytes[4] = {0, 0, 0, 0};
    const bool read = fseek(file, 0x10, SEEK_SET) == 0 && fread(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
    fclose(file);
    if (!read)
        return 0;
    const uint32_t timestamp = static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
                               (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
    // Between 2017 and 2100; anything else is not a save time.
    return timestamp >= 1483228800u && timestamp <= 4102444800u ? timestamp : 0;
}

int SavefileIO::GetMostRecentSavefile(const std::string& dir, bool masterMode)
{
    // Normal Mode uses slots 0-5, Master Mode 6-7. Backups only have game_data.sav, so they fall back to playtime.
    const int first = masterMode ? 6 : 0;
    const int last = masterMode ? 7 : 5;
    std::vector<SaveLoadDecision::SlotCandidate> slots;
    for (int i = first; i <= last; i++)
    {
        SaveLoadDecision::SlotCandidate slot;
        slot.slot = i;
        slot.playtime = GetSavefilePlaytime(dir + std::to_string(i) + "/game_data.sav"); // 0 = Save doesn't exist
        if (slot.playtime != 0)
            slot.timestamp = GetSavefileTimestamp(dir + std::to_string(i) + "/caption.sav");
        slots.push_back(slot);
    }
    const int mostRecent = SaveLoadDecision::PickMostRecentSlot(slots);
    Log("Most recent save slot:", mostRecent);
    return mostRecent;
}

void SavefileIO::CopySavefiles()
{
    Log("Copying savefiles...");
    std::vector<std::string> savefileFolders;

    if (MostRecentNormalModeFile != -1)
        savefileFolders.push_back(std::to_string(MostRecentNormalModeFile));
    if (MostRecentMasterModeFile != -1) 
        savefileFolders.push_back(std::to_string(MostRecentMasterModeFile));

    std::string profileIdStr = std::to_string(AccountUid1) + " - " + std::to_string(AccountUid2);
    std::string savesFolder = "sdmc:/switch/botw-unexplored/saves/" + profileIdStr;

    if (!DirectoryExists("sdmc:/switch/botw-unexplored"))
        mkdir("sdmc:/switch/botw-unexplored", 0777);
    if (!DirectoryExists("sdmc:/switch/botw-unexplored/saves"))
        mkdir("sdmc:/switch/botw-unexplored/saves", 0777);
    if (!DirectoryExists(savesFolder))
        mkdir(savesFolder.c_str(), 0777);

    for (unsigned int i = 0; i < savefileFolders.size(); i++)
    {
        // Create the destination folder if it doesn't exist
        if (!DirectoryExists(savesFolder + "/" + savefileFolders[i]))
            mkdir(std::string(savesFolder + "/" + savefileFolders[i]).c_str(), 0777);

        // Copy the savefiles
        std::string target = savesFolder + "/" + savefileFolders[i] + "/game_data.sav";

        CopyFile("save:/" + savefileFolders[i] + "/game_data.sav", target);
        // The save time, so that the backup can tell how old it is.
        CopyFile("save:/" + savefileFolders[i] + "/caption.sav", savesFolder + "/" + savefileFolders[i] + "/caption.sav");

        Log("Copied savefile " + savefileFolders[i] + " to " + target);
    }

}

s32 SavefileIO::CopyFile(const std::string &srcPath, const std::string &dstPath)
{
    FILE *src = fopen(srcPath.c_str(), "rb");
    if (src == nullptr)
        return -1;
    FILE *dst = fopen(dstPath.c_str(), "wb+");
    if (dst == nullptr)
    {
        fclose(src);
        return -1;
    }

    fseek(src, 0, SEEK_END);
    rewind(src);

    size_t size;
    std::vector<char> buffer(0x50000);
    while ((size = fread(buffer.data(), 1, buffer.size(), src)) > 0)
    {
        if (fwrite(buffer.data(), 1, size, dst) != size)
        {
            fclose(src);
            fclose(dst);
            return -1;
        }
    }

    fclose(src);
    fclose(dst);

    return 0;
}

bool SavefileIO::DirectoryExists(const std::string &filepath)
{
    DIR *dir = opendir(filepath.c_str());
    if (dir)
    {
        closedir(dir);

        return true;
    }
    else if (ENOENT == errno)
        return false;
    else
    {
        Log("DirectoryExists() error ", filepath);
        return false;
    }

    return false;
}

bool SavefileIO::FileExists(const std::string& filepath)
{
    int res = access(filepath.c_str(), R_OK);
    if (res < 0)
    {
        if (errno == ENOENT)
            Log("File doesn't exist (doesn't have to be bad)", filepath.c_str());
        else if (errno == EACCES)
            Log("File could not be read for some reason (doesn't have to be bad)", filepath.c_str());
        else
            Log("File access failed (doesn't have to be bad)", filepath.c_str());

        return false;
    }

    return true;
}

namespace
{
struct ShrineFlagTarget
{
    uint32_t locationHash;
    bool allChests;
};

// Clear_DungeonNNN and CompleteTreasure_DungeonNNN hashes, pointing back to the shrine's discovery hash.
const std::unordered_map<uint32_t, ShrineFlagTarget>& GetShrineFlagTable()
{
    static const std::unordered_map<uint32_t, ShrineFlagTarget> table = []()
    {
        std::unordered_map<uint32_t, ShrineFlagTarget> result;
        ShrineFlags::Hashes hashes;
        for (int i = 0; i < Data::ShrineCount; ++i)
            if (ShrineFlags::FromLocationHash(Data::Shrines[i].hash, hashes))
            {
                result[hashes.clear] = {Data::Shrines[i].hash, false};
                result[hashes.allChests] = {Data::Shrines[i].hash, true};
            }
        for (int i = 0; i < Data::DLCShrineCount; ++i)
            if (ShrineFlags::FromLocationHash(Data::DLCShrines[i].hash, hashes))
            {
                result[hashes.clear] = {Data::DLCShrines[i].hash, false};
                result[hashes.allChests] = {Data::DLCShrines[i].hash, true};
            }
        return result;
    }();
    return table;
}

// Every flag the map shows, by hash. A hash may belong to more than one list.
struct TrackedFlag
{
    Data::ObjectType type;
    void* object;
};

const std::unordered_multimap<uint32_t, TrackedFlag>& GetTrackedFlags()
{
    static const std::unordered_multimap<uint32_t, TrackedFlag> table = []()
    {
        std::unordered_multimap<uint32_t, TrackedFlag> result;
        for (int i = 0; i < Data::KoroksCount; ++i)
            result.insert({Data::Koroks[i].hash, {Data::ObjectType::Korok, &Data::Koroks[i]}});
        for (int i = 0; i < Data::ShrineCount; ++i)
            result.insert({Data::Shrines[i].hash, {Data::ObjectType::Shrine, &Data::Shrines[i]}});
        for (int i = 0; i < Data::DLCShrineCount; ++i)
            result.insert({Data::DLCShrines[i].hash, {Data::ObjectType::DLCShrine, &Data::DLCShrines[i]}});
        for (int i = 0; i < Data::LocationsCount; ++i)
            result.insert({Data::Locations[i].hash, {Data::ObjectType::Location, &Data::Locations[i]}});
        for (int i = 0; i < Data::HinoxesCount; ++i)
            result.insert({Data::Hinoxes[i].hash, {Data::ObjectType::Hinox, &Data::Hinoxes[i]}});
        for (int i = 0; i < Data::TalusesCount; ++i)
            result.insert({Data::Taluses[i].hash, {Data::ObjectType::Talus, &Data::Taluses[i]}});
        for (int i = 0; i < Data::MoldugasCount; ++i)
            result.insert({Data::Moldugas[i].hash, {Data::ObjectType::Molduga, &Data::Moldugas[i]}});
        return result;
    }();
    return table;
}

// Sorts the hash/value records (of a save file or of the running game) into the lists the map uses.
void ApplyRecords(const SaveParser::ParsedSaveState& bufferState, SavefileIO::ParsedSaveState& parsedState)
{
    const std::unordered_map<uint32_t, ShrineFlagTarget>& shrineFlags = GetShrineFlagTable();
    const std::unordered_multimap<uint32_t, TrackedFlag>& tracked = GetTrackedFlags();
    for (const SaveParser::Record& record : bufferState.records)
    {
        const uint32_t hashValue = record.hash;
        // A value other than 0 means found, discovered, visited or defeated.
        const bool set = record.value != 0;

        const auto shrineFlag = shrineFlags.find(hashValue);
        if (shrineFlag != shrineFlags.end() && set)
            (shrineFlag->second.allChests ? parsedState.allChestsShrines : parsedState.clearedShrines)
                .push_back(shrineFlag->second.locationHash);

        const auto range = tracked.equal_range(hashValue);
        for (auto it = range.first; it != range.second; ++it)
        {
            void* object = it->second.object;
            switch (it->second.type)
            {
            case Data::ObjectType::Korok:
                (set ? parsedState.foundKoroks : parsedState.missingKoroks).push_back(static_cast<Data::Korok*>(object));
                break;
            case Data::ObjectType::Shrine:
                (set ? parsedState.foundShrines : parsedState.missingShrines).push_back(static_cast<Data::Shrine*>(object));
                break;
            case Data::ObjectType::DLCShrine:
                (set ? parsedState.foundDLCShrines : parsedState.missingDLCShrines).push_back(static_cast<Data::DLCShrine*>(object));
                break;
            case Data::ObjectType::Location:
                (set ? parsedState.visitedLocations : parsedState.unexploredLocations).push_back(static_cast<Data::Location*>(object));
                break;
            case Data::ObjectType::Hinox:
                (set ? parsedState.defeatedHinoxes : parsedState.undefeatedHinoxes).push_back(static_cast<Data::Hinox*>(object));
                break;
            case Data::ObjectType::Talus:
                (set ? parsedState.defeatedTaluses : parsedState.undefeatedTaluses).push_back(static_cast<Data::Talus*>(object));
                break;
            case Data::ObjectType::Molduga:
                (set ? parsedState.defeatedMoldugas : parsedState.undefeatedMoldugas).push_back(static_cast<Data::Molduga*>(object));
                break;
            default:
                break;
            }
        }
    }

    parsedState.hasDLC = bufferState.hasDLC;
    parsedState.hasPlayerPosition = bufferState.hasPlayerPosition;
    parsedState.playerX = bufferState.playerPosition[0];
    parsedState.playerZ = bufferState.playerPosition[2];
    parsedState.playerPositionIsLive = false;
    parsedState.playerInsideShrine = false;
}

bool ParseFileIntoState(const char *filepath, SavefileIO::ParsedSaveState& parsedState)
{
    Log("Opening savefile", filepath);

    std::vector<uint8_t> buffer;
    SaveParser::ParsedSaveState bufferState;
    if (!ReadFileBuffer(filepath, buffer) || !SaveParser::ParseSaveBuffer(buffer.data(), buffer.size(), bufferState))
        return false;

    ApplyRecords(bufferState, parsedState);
    Log("User has DLC:", parsedState.hasDLC ? "true" : "false");

    Log("Successfully parsed file", filepath);

    return true;
}

// AoC_HardMode_Enabled: set by the game while it runs in Master Mode (not stored in the save).
constexpr uint32_t HardModeEnabledHash = 0xE51256E2;
// Text flags that say which map the game is on: its type ("MainField", "CDungeon"...) and its name
// ("E-6" for a piece of the world, "Dungeon042" for a shrine). The game writes them with every save.
constexpr uint32_t MapTypeHash = 0xD913B769;
constexpr uint32_t MapNameHash = 0x0BEE9E46;

class LiveCollector : public LiveGameData::Visitor
{
public:
    SaveParser::ParsedSaveState state;
    bool hardMode = false;
    char mapType[33] = {};
    char mapName[33] = {};
    bool hasSavePosition = false;
    float savePosition[3] = {};

    void OnFlag(LiveGameData::FlagKind kind, uint32_t hash, const uint8_t* value, uint64_t) override
    {
        switch (kind)
        {
        case LiveGameData::FlagKind::Bool:
        {
            const bool set = (value[0] & 1) != 0;
            SaveParser::Record record;
            record.hash = hash;
            record.value = set ? 1u : 0u;
            state.records.push_back(record);
            if (hash == SaveParser::BalladOfHeroesReadyHash && set)
                state.hasDLC = true;
            else if (hash == HardModeEnabledHash && set)
                hardMode = true;
            break;
        }
        case LiveGameData::FlagKind::S32:
        {
            SaveParser::Record record;
            record.hash = hash;
            std::memcpy(&record.value, value, sizeof(record.value));
            state.records.push_back(record);
            if (hash == SaveParser::PlaytimeHash && record.value > state.playtime)
                state.playtime = record.value;
            break;
        }
        case LiveGameData::FlagKind::String32:
            if (hash == MapTypeHash)
                std::memcpy(mapType, value, 32);
            else if (hash == MapNameHash)
                std::memcpy(mapName, value, 32);
            break;
        case LiveGameData::FlagKind::Vector3f:
            if (hash == SaveParser::PlayerSavePosHash)
            {
                std::memcpy(savePosition, value, sizeof(savePosition));
                hasSavePosition = true;
            }
            break;
        default:
            break;
        }
    }
};

// World position of the shrine whose map is "DungeonNNN".
bool ShrinePosition(const char* mapName, float& x, float& z)
{
    int number = -1;
    if (std::sscanf(mapName, "Dungeon%3d", &number) != 1 || number < 0)
        return false;
    char flagName[40];
    std::snprintf(flagName, sizeof(flagName), "Location_Dungeon%03d", number);
    const uint32_t hash = ShrineFlags::Crc32(flagName);
    if (const Data::Shrine* shrine = Data::ShrineExists(hash))
    {
        x = shrine->x;
        z = shrine->y;
        return true;
    }
    if (const Data::DLCShrine* shrine = Data::DLCShrineExists(hash))
    {
        x = shrine->x;
        z = shrine->y;
        return true;
    }
    return false;
}

bool LoadLive(LiveState& live)
{
    const GameMemory::Status status = GameMemory::Open(live.message);
    if (status != GameMemory::Status::Ready)
    {
        Log("LIVE: not available:", live.message);
        return false;
    }

    const LiveGameData::Layout& layout = LiveGameData::Botw160();
    LiveCollector collector;
    collector.state.records.reserve(70000);
    LiveGameData::Report report;
    const bool scanned = LiveGameData::Scan(GameMemory::Reader(), GameMemory::MainBase(), layout, collector, report);
    std::istringstream details(report.details);
    for (std::string line; std::getline(details, line);)
        Log("LIVE: " + line);
    if (!scanned)
    {
        GameMemory::Close();
        live.message = report.error;
        return false;
    }

    float position[3] = {0.0f, 0.0f, 0.0f};
    const bool hasLivePosition = LiveGameData::ReadPlayerPosition(GameMemory::Reader(), GameMemory::MainBase(), layout, position) &&
                                 SaveParser::IsPlausiblePosition(position);
    GameMemory::Close();

    if (collector.state.playtime == 0)
    {
        Log("LIVE: no playtime, the game is still at the title screen");
        live.message = "the game has not loaded a save yet";
        return false;
    }

    ApplyRecords(collector.state, live.parsed);
    const size_t koroks = live.parsed.foundKoroks.size() + live.parsed.missingKoroks.size();
    const size_t shrines = live.parsed.foundShrines.size() + live.parsed.missingShrines.size();
    const size_t locations = live.parsed.visitedLocations.size() + live.parsed.unexploredLocations.size();
    Log("LIVE: records " + std::to_string(collector.state.records.size()) + ", koroks " + std::to_string(koroks) +
        ", shrines " + std::to_string(shrines) + ", locations " + std::to_string(locations) + ", playtime " +
        std::to_string(collector.state.playtime) + ", map " + collector.mapType + " " + collector.mapName);
    // The flags this app is about must all be there, or this is not the data it expects.
    if (koroks < static_cast<size_t>(Data::KoroksCount) || shrines < static_cast<size_t>(Data::ShrineCount) ||
        locations < static_cast<size_t>(Data::LocationsCount))
    {
        live.message = "the game's flags do not match this app";
        return false;
    }

    live.masterMode = collector.hardMode;
    live.parsed.hasPlayerPosition = false;
    const bool onWorldMap = collector.mapType[0] == '\0' || std::strcmp(collector.mapType, "MainField") == 0;
    if (!onWorldMap)
    {
        // Inside a shrine (or another map of its own) Link's coordinates are not those of the world map.
        if (ShrinePosition(collector.mapName, live.parsed.playerX, live.parsed.playerZ))
        {
            live.parsed.hasPlayerPosition = true;
            live.parsed.playerPositionIsLive = true;
            live.parsed.playerInsideShrine = true;
        }
    }
    else if (hasLivePosition)
    {
        live.parsed.hasPlayerPosition = true;
        live.parsed.playerPositionIsLive = true;
        live.parsed.playerX = position[0];
        live.parsed.playerZ = position[2];
    }
    else if (collector.hasSavePosition && SaveParser::IsPlausiblePosition(collector.savePosition))
    {
        live.parsed.hasPlayerPosition = true;
        live.parsed.playerX = collector.savePosition[0];
        live.parsed.playerZ = collector.savePosition[2];
    }
    return true;
}
}

bool SavefileIO::ParseFile(const char *filepath)
{
    ParsedSaveState parsedState;
    if (!ParseFileIntoState(filepath, parsedState))
        return false;

    SaveState candidate = CurrentState;
    ApplyParsedState(candidate, parsedState);
    candidate.loadedSavefile = true;
    CommitState(candidate);
    return true;
}

SavefileIO::SaveState SavefileIO::CurrentState;
std::vector<Data::Korok *>& SavefileIO::foundKoroks = CurrentState.foundKoroks;
std::vector<Data::Korok *>& SavefileIO::missingKoroks = CurrentState.missingKoroks;
std::vector<Data::Shrine*>& SavefileIO::foundShrines = CurrentState.foundShrines;
std::vector<Data::Shrine*>& SavefileIO::missingShrines = CurrentState.missingShrines;
std::vector<Data::DLCShrine*>& SavefileIO::foundDLCShrines = CurrentState.foundDLCShrines;
std::vector<Data::DLCShrine*>& SavefileIO::missingDLCShrines = CurrentState.missingDLCShrines;
std::vector<Data::Location *>& SavefileIO::visitedLocations = CurrentState.visitedLocations;
std::vector<Data::Location *>& SavefileIO::unexploredLocations = CurrentState.unexploredLocations;
std::vector<Data::Hinox *>& SavefileIO::defeatedHinoxes = CurrentState.defeatedHinoxes;
std::vector<Data::Hinox *>& SavefileIO::undefeatedHinoxes = CurrentState.undefeatedHinoxes;
std::vector<Data::Talus *>& SavefileIO::defeatedTaluses = CurrentState.defeatedTaluses;
std::vector<Data::Talus *>& SavefileIO::undefeatedTaluses = CurrentState.undefeatedTaluses;
std::vector<Data::Molduga *>& SavefileIO::defeatedMoldugas = CurrentState.defeatedMoldugas;
std::vector<Data::Molduga *>& SavefileIO::undefeatedMoldugas = CurrentState.undefeatedMoldugas;
u64& SavefileIO::AccountUid1 = CurrentState.accountUid1;
u64& SavefileIO::AccountUid2 = CurrentState.accountUid2;
int& SavefileIO::MostRecentNormalModeFile = CurrentState.mostRecentNormalModeFile;
int& SavefileIO::MostRecentMasterModeFile = CurrentState.mostRecentMasterModeFile;
bool& SavefileIO::LoadedSavefile = CurrentState.loadedSavefile;
bool& SavefileIO::GameIsRunning = CurrentState.gameIsRunning;
bool& SavefileIO::LiveSynced = CurrentState.liveSynced;
bool& SavefileIO::NoSavefileForUser = CurrentState.noSavefileForUser;
bool& SavefileIO::MasterModeFileExists = CurrentState.masterModeFileExists;
bool& SavefileIO::MasterModeFileLoaded = CurrentState.masterModeFileLoaded;
bool& SavefileIO::HasDLC = CurrentState.hasDLC;
int& SavefileIO::MasterModeSlot = CurrentState.masterModeSlot;

bool SavefileIO::IsShrineCleared(uint32_t locationHash)
{
    const std::vector<uint32_t>& cleared = CurrentState.clearedShrines;
    return std::find(cleared.begin(), cleared.end(), locationHash) != cleared.end();
}

bool SavefileIO::HasAllShrineChests(uint32_t locationHash)
{
    const std::vector<uint32_t>& chests = CurrentState.allChestsShrines;
    return std::find(chests.begin(), chests.end(), locationHash) != chests.end();
}
