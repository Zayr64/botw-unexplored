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
#include "Log.h"
#include "SaveLoadDecision.h"
#include "SaveParser.h"
#include "ShrineFlags.h"

#include <algorithm>
#include <unordered_map>

namespace
{
    bool saveMounted = false;

    bool ParseFileIntoState(const char* filepath, SavefileIO::ParsedSaveState& parsedState);

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

bool SavefileIO::LoadGamesave(bool loadMasterMode, bool chooseProfile)
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
        Log("Game is running. Loading backup...");

        const bool loadedBackupSuccess = LoadBackup(loadMasterMode, candidate);

        if (!loadedBackupSuccess) 
        {
            Map::m_GameRunningDialog->SetOpen(true);
            return false;
        }
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

        Log("Copied savefile " + savefileFolders[i] + " to " + target);
    }

}

s32 SavefileIO::CopyFile(const std::string &srcPath, const std::string &dstPath)
{
    FILE *src = fopen(srcPath.c_str(), "rb");
    FILE *dst = fopen(dstPath.c_str(), "wb+");

    if (src == nullptr || dst == nullptr)
    {
        if (src) fclose(src);
        if (dst) fclose(dst);
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

bool ParseFileIntoState(const char *filepath, SavefileIO::ParsedSaveState& parsedState)
{
    Log("Opening savefile", filepath);

    std::vector<uint8_t> buffer;
    SaveParser::ParsedSaveState bufferState;
    if (!ReadFileBuffer(filepath, buffer) || !SaveParser::ParseSaveBuffer(buffer.data(), buffer.size(), bufferState))
        return false;

    const std::unordered_map<uint32_t, ShrineFlagTarget>& shrineFlags = GetShrineFlagTable();
    for (const SaveParser::Record& record : bufferState.records)
    {
        const uint32_t hashValue = record.hash;
        const uint32_t value = record.value;

        const auto shrineFlag = shrineFlags.find(hashValue);
        if (shrineFlag != shrineFlags.end() && value != 0)
            (shrineFlag->second.allChests ? parsedState.allChestsShrines : parsedState.clearedShrines)
                .push_back(shrineFlag->second.locationHash);

        // Check if a korok with the hash exists
        Data::Korok *korok = Data::KorokExists(hashValue);
        if (korok)
        {
            // Read the 4 bytes after the hash. If it's not 0, then the seed has been found.
            bool found = value != 0;

            found ? parsedState.foundKoroks.push_back(korok) : parsedState.missingKoroks.push_back(korok);
        }
        

        // Check if a shrine with the hash exists
        Data::Shrine *shrine = Data::ShrineExists(hashValue);
        if (shrine)
        {
            // Read the 4 bytes after the hash. If it is not 0, then the shrine has been found.
            bool defeated = value != 0;

            defeated ? parsedState.foundShrines.push_back(shrine) : parsedState.missingShrines.push_back(shrine);
        }

        // Check for dlc shrines
        Data::DLCShrine *dlcShrine = Data::DLCShrineExists(hashValue);
        if (dlcShrine)
        {
            // Read the 4 bytes after the hash. If it is not 0, then the shrine has been found.
            bool defeated = value != 0;

            defeated ? parsedState.foundDLCShrines.push_back(dlcShrine) : parsedState.missingDLCShrines.push_back(dlcShrine);
        }

        // Check if a location with the hash exists
        Data::Location *location = Data::LocationExists(hashValue);
        if (location)
        {
            // Read the 4 bytes after the hash. If it is not 0, then the location has been visited.
            bool visited = value != 0;

            visited ? parsedState.visitedLocations.push_back(location) : parsedState.unexploredLocations.push_back(location);
        }

        // Check if a hinox with the hash exists
        Data::Hinox *hinox = Data::HinoxExists(hashValue);
        if (hinox)
        {
            // Read the 4 bytes after the hash. If it is not 0, then the hinox has been defeated.
            bool defeated = value != 0;

            defeated ? parsedState.defeatedHinoxes.push_back(hinox) : parsedState.undefeatedHinoxes.push_back(hinox);
        }

        // Check if a talus with the hash exists
        Data::Talus *talus = Data::TalusExists(hashValue);
        if (talus)
        {
            // Read the 4 bytes after the hash. If it is not 0, then the talus has been defeated.
            bool defeated = value != 0;

            defeated ? parsedState.defeatedTaluses.push_back(talus) : parsedState.undefeatedTaluses.push_back(talus);
        }

        // Check if a molduga with the hash exists
        Data::Molduga *molduga = Data::MoldugaExists(hashValue);
        if (molduga)
        {
            // Read the 4 bytes after the hash. If it is not 0, then the molduga has been defeated.
            bool defeated = value != 0;

            defeated ? parsedState.defeatedMoldugas.push_back(molduga) : parsedState.undefeatedMoldugas.push_back(molduga);
        }

    }

    parsedState.hasDLC = bufferState.hasDLC;
    parsedState.hasPlayerPosition = bufferState.hasPlayerPosition;
    parsedState.playerX = bufferState.playerPosition[0];
    parsedState.playerZ = bufferState.playerPosition[2];
    Log("User has DLC:", parsedState.hasDLC ? "true" : "false");

    Log("Successfully parsed file", filepath);

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
