// BotW Unexplored overlay: the missing objects nearest to Link, while the game runs.
//
// Everything about the game is read (read only) through Atmosphere's cheat service (dmnt:cht):
// - Link's position, from the pointer chain [[main+2CA1140]+60]+398 (a sead::Matrix34f; translation at
//   +3A4/+3B4/+3C4), the one used by Pistonight/botw-save-state for BotW 1.6.0, checked on a console on 2026-11-07;
// - what was already found, from the game's own flags (source/LiveGameData, shared with the app).
// When the flags cannot be read, what is missing comes from the save backup the app copies to
// sdmc:/switch/botw-unexplored/saves/ and from the app's manual_progress.dat, which this overlay then also
// writes when an object is marked as found.

#define TESLA_INIT_IMPL
#include <exception_wrap.hpp>
#include <tesla.hpp>

#include <switch.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <vector>

#include "LiveGameData.h"
#include "ManualProgress.h"
#include "ObjectTypes.h"
#include "dmntcht.h"

namespace
{
    constexpr u64 BotwTitleId = 0x01007EF00011E000ULL;
    constexpr u64 PlayerPointer = 0x2CA1140;
    constexpr u64 ActorOffset = 0x60;
    constexpr u64 MatrixOffset = 0x398;
    // Camera position, [[[main+2CA5520]+20]+558]+CA4/CA8/CAC (same 1.6.0 pointer list as Link's position).
    constexpr u64 CameraPointer = 0x2CA5520;
    constexpr u64 CameraOffsets[2] = {0x20, 0x558};
    constexpr u64 CameraXOffset = 0xCA4;
    constexpr int GuideRefreshFrames = 5;   // about 12 reads per second in guide mode
    constexpr int GuideFlagFrames = 90;     // the target's flag is read every second and a half
    constexpr u64 GuideNextNs = 2500000000ULL; // "Found!" stays this long before the next target

    constexpr const char* SyncLogPath = "sdmc:/switch/botw-unexplored/overlay-sync.log";
    constexpr const char* SavesDir = "sdmc:/switch/botw-unexplored/saves";
    constexpr const char* ManualProgressPath = "sdmc:/switch/botw-unexplored/manual_progress.dat";
    constexpr u32 PlaytimeHash = 0x73c29681;
    constexpr u32 BalladOfHeroesReadyHash = 1186840637;
    constexpr size_t SaveHeaderSize = 0x0c;

    constexpr float Pi = 3.14159265f;
    constexpr int ShownCount = 5;
    constexpr int RefreshFrames = 15;      // about 4 position reads per second
    constexpr float ArrivedMeters = 12.0f;
    constexpr float HintMeters = 80.0f;     // the guide shows how to get the object from this distance

    enum class Filter { All, Koroks, Shrines, Locations, Bosses, Count };
    const char* FilterNames[] = {"Everything", "Koroks", "Shrines", "Locations", "Hinox, Talus, Molduga"};

    // Constant table generated from the app's Data.cpp (overlay/tools/gen_items.py). Data.cpp itself is not
    // compiled here: its global initializers need ~200 KB of stack and crashed the overlay loader.
    struct ItemSource
    {
        Data::ObjectType type;
        u32 hash;
        float x;
        float z;
        const char* name;
        const char* hint;
    };

    const ItemSource ItemTable[] = {
#include "items.inc"
    };

    struct Item
    {
        Data::ObjectType type;
        u32 hash;
        float x;
        float z;
        const char* name;
        const char* hint;
        bool found = false;
        // Where its flag is in the game's memory (0 = unknown), and whether it is a number instead of a yes/no.
        u64 flagAddress = 0;
        bool flagIsNumber = false;
    };

    struct Pose
    {
        float x = 0, y = 0, z = 0;
        float forwardX = 0, forwardZ = 1;  // "up" for the dials: the camera's view, or Link's facing
        bool fromCamera = false;
    };

    struct Nearby
    {
        int index;
        float distance;
        float bearing;  // degrees clockwise from north
    };

    std::vector<Item> g_items;
    std::unordered_map<u32, size_t> g_itemByHash;
    bool g_saveLoaded = false;
    std::string g_saveMessage;
    ManualProgress::Context g_context;

    bool g_dmntReady = false;
    bool g_attached = false;
    u64 g_mainBase = 0;
    std::string g_gameMessage;

    Pose g_pose;
    bool g_hasPose = false;
    Filter g_filter = Filter::All;
    std::vector<Nearby> g_nearby;
    int g_missingInFilter = 0;
    int g_selected = 0;
    std::string g_notice;
    bool g_noticeIsWarning = false;
    bool g_liveSynced = false;

    constexpr const char* SelfPath = "sdmc:/switch/.overlays/ovlBotwUnexplored.ovl";
    constexpr u64 HoldToMarkNs = 1000000000ULL;
    u32 g_holdHash = 0;
    u64 g_holdTick = 0;
    float g_holdProgress = 0.0f;

    // Guide (GPS) mode
    u32 g_guideHash = 0;
    bool g_guideFound = false;     // the target was collected
    u64 g_guideFoundTick = 0;
    bool g_guideNothingLeft = false;

    struct LastMark
    {
        Data::ObjectType type;
        u32 hash;
        const char* name;
    };
    LastMark g_lastMark{};
    bool g_hasLastMark = false;

    // The hint of the selected object, broken into lines for the width of the overlay.
    u32 g_wrappedHash = 0;
    s32 g_wrappedWidth = 0;
    std::vector<std::string> g_wrappedLines;

    std::string Shorten(const std::string& text, size_t maxChars)
    {
        std::string line = text.substr(0, text.find('\n'));
        if (line.size() <= maxChars)
            return line;
        size_t cut = maxChars - 3;
        while (cut > 0 && (static_cast<unsigned char>(line[cut]) & 0xC0) == 0x80)
            --cut;
        return line.substr(0, cut) + "...";
    }

    // Breaks a text into at most maxLines lines no wider than maxWidth pixels; what does not fit ends in "...".
    void WrapText(tsl::gfx::Renderer* renderer, const std::string& text, u32 fontSize, s32 maxWidth, size_t maxLines,
                  std::vector<std::string>& lines)
    {
        lines.clear();
        const auto width = [&](const std::string& part) {
            return static_cast<s32>(renderer->getTextDimensions(part, false, fontSize).first);
        };
        std::string line;
        size_t start = 0;
        while (start < text.size())
        {
            size_t end = text.find(' ', start);
            if (end == std::string::npos)
                end = text.size();
            const std::string word = text.substr(start, end - start);
            start = end + 1;
            if (word.empty())
                continue;
            const std::string candidate = line.empty() ? word : line + " " + word;
            if (line.empty() || width(candidate) <= maxWidth)
            {
                line = candidate;
                continue;
            }
            if (lines.size() + 1 == maxLines)
            {
                // No room for another line: this one ends in "..."
                while (!line.empty() && width(line + "...") > maxWidth)
                {
                    const size_t space = line.rfind(' ');
                    line = space == std::string::npos ? std::string() : line.substr(0, space);
                }
                lines.push_back(line + "...");
                return;
            }
            lines.push_back(line);
            line = word;
        }
        if (!line.empty())
            lines.push_back(line);
    }

    bool InFilter(Data::ObjectType type)
    {
        switch (g_filter)
        {
        case Filter::Koroks: return type == Data::ObjectType::Korok;
        case Filter::Shrines: return type == Data::ObjectType::Shrine || type == Data::ObjectType::DLCShrine;
        case Filter::Locations: return type == Data::ObjectType::Location;
        case Filter::Bosses:
            return type == Data::ObjectType::Hinox || type == Data::ObjectType::Talus ||
                   type == Data::ObjectType::Molduga;
        default: return true;
        }
    }

    void BuildItems()
    {
        g_items.clear();
        g_itemByHash.clear();
        g_items.reserve(sizeof(ItemTable) / sizeof(ItemTable[0]));
        for (const ItemSource& source : ItemTable)
        {
            g_itemByHash[source.hash] = g_items.size();
            g_items.push_back(Item{source.type, source.hash, source.x, source.z, source.name, source.hint});
        }
    }

    // Streams game_data.sav (hash/value records after a 0x0c header) without holding the whole file in memory.
    // With apply == false it only reports the playtime.
    bool ScanSave(const std::string& path, bool apply, u32& playtime, bool& hasDLC)
    {
        FILE* file = fopen(path.c_str(), "rb");
        if (file == nullptr)
            return false;
        playtime = 0;
        hasDLC = false;
        static u8 buffer[0x10000];
        bool ok = fseek(file, SaveHeaderSize, SEEK_SET) == 0;
        size_t carry = 0;
        while (ok)
        {
            const size_t read = fread(buffer + carry, 1, sizeof(buffer) - carry, file);
            const size_t available = carry + read;
            const size_t whole = available - available % 8;
            for (size_t offset = 0; offset < whole; offset += 8)
            {
                u32 hash, value;
                std::memcpy(&hash, buffer + offset, 4);
                std::memcpy(&value, buffer + offset + 4, 4);
                if (hash == PlaytimeHash && value != 0)
                    playtime = value;
                else if (hash == BalladOfHeroesReadyHash)
                    hasDLC = value == 1;
                if (apply)
                {
                    const auto it = g_itemByHash.find(hash);
                    if (it != g_itemByHash.end())
                        g_items[it->second].found = value != 0;
                }
            }
            carry = available - whole;
            if (carry > 0)
                std::memmove(buffer, buffer + whole, carry);
            if (read == 0)
                break;
        }
        fclose(file);
        return ok && playtime != 0;
    }

    bool ParseProfileFolder(const char* name, u64& uid1, u64& uid2)
    {
        char* end = nullptr;
        uid1 = std::strtoull(name, &end, 10);
        if (end == name || std::strncmp(end, " - ", 3) != 0)
            return false;
        const char* second = end + 3;
        uid2 = std::strtoull(second, &end, 10);
        return end != second && *end == '\0';
    }

    // The newest backup folder wins (the app copies every slot of a profile at once); inside it, the slot with the
    // most playtime, which is how the app itself picks a backup. Normal Mode only (slots 0-5).
    void LoadSave()
    {
        g_saveLoaded = false;
        BuildItems();

        DIR* dir = opendir(SavesDir);
        if (dir == nullptr)
        {
            g_saveMessage = "No save backup yet";
            return;
        }
        std::string bestFolder;
        time_t bestTime = 0;
        u64 bestUid1 = 0, bestUid2 = 0;
        while (dirent* entry = readdir(dir))
        {
            u64 uid1, uid2;
            if (!ParseProfileFolder(entry->d_name, uid1, uid2))
                continue;
            const std::string folder = std::string(SavesDir) + "/" + entry->d_name;
            for (int slot = 0; slot <= 5; slot++)
            {
                struct stat info;
                const std::string path = folder + "/" + std::to_string(slot) + "/game_data.sav";
                if (stat(path.c_str(), &info) == 0 && (bestFolder.empty() || info.st_mtime > bestTime))
                {
                    bestFolder = folder;
                    bestTime = info.st_mtime;
                    bestUid1 = uid1;
                    bestUid2 = uid2;
                }
            }
        }
        closedir(dir);
        if (bestFolder.empty())
        {
            g_saveMessage = "No save backup yet";
            return;
        }

        int bestSlot = -1;
        u32 bestPlaytime = 0;
        for (int slot = 0; slot <= 5; slot++)
        {
            u32 playtime;
            bool dlc;
            if (ScanSave(bestFolder + "/" + std::to_string(slot) + "/game_data.sav", false, playtime, dlc) &&
                playtime > bestPlaytime)
            {
                bestPlaytime = playtime;
                bestSlot = slot;
            }
        }
        u32 playtime;
        bool hasDLC;
        if (bestSlot < 0 ||
            !ScanSave(bestFolder + "/" + std::to_string(bestSlot) + "/game_data.sav", true, playtime, hasDLC))
        {
            g_saveMessage = "The save backup could not be read";
            return;
        }
        if (!hasDLC)
            for (Item& item : g_items)
                if (item.type == Data::ObjectType::DLCShrine)
                    item.found = true;

        g_context.profileId1 = bestUid1;
        g_context.profileId2 = bestUid2;
        g_context.masterMode = false;

        ManualProgress::Clear();
        std::ifstream manual(ManualProgressPath);
        if (manual.is_open())
            ManualProgress::Load(manual);

        char when[32] = "";
        std::strftime(when, sizeof(when), "%d/%m %H:%M", std::localtime(&bestTime));
        g_saveMessage = std::string("Save backup from ") + when;
        g_saveLoaded = true;
    }

    // The game's own flags need no marks made by hand; a save backup may be old.
    bool IsMissing(const Item& item)
    {
        if (item.found)
            return false;
        return g_liveSynced || !ManualProgress::IsMarkedFound(g_context, item.type, item.hash);
    }

    bool CheatServiceRunning()
    {
        Handle handle;
        const SmServiceName name = smEncodeName("dmnt:cht");
        const bool running = R_FAILED(smRegisterService(&handle, name, false, 1));
        svcCloseHandle(handle);
        if (!running)
            smUnregisterService(name);
        return running;
    }

    bool Attach()
    {
        if (!g_dmntReady)
        {
            g_gameMessage = "Atmosphere's cheat service (dmnt) is not available";
            return false;
        }
        u64 pid = 0;
        if (R_FAILED(pmdmntGetApplicationProcessId(&pid)))
        {
            g_gameMessage = "Start BotW first";
            return false;
        }
        dmntchtForceOpenCheatProcess();
        DmntCheatProcessMetadata metadata = {};
        if (R_FAILED(dmntchtGetCheatProcessMetadata(&metadata)))
        {
            g_gameMessage = "Could not attach to the game";
            return false;
        }
        if (metadata.title_id != BotwTitleId)
        {
            g_gameMessage = "The running game is not BotW";
            return false;
        }
        if (std::memcmp(metadata.main_nso_build_id, LiveGameData::BotwBuildId160, sizeof(LiveGameData::BotwBuildId160)) != 0)
        {
            g_gameMessage = "Only BotW 1.6.0 is supported";
            return false;
        }
        g_mainBase = metadata.main_nso_extents.base;
        g_gameMessage.clear();
        return true;
    }

    bool ReadGame(u64 address, void* out, size_t size)
    {
        return R_SUCCEEDED(dmntchtReadCheatProcessMemory(address, out, size));
    }

    bool ReadPose(Pose& pose)
    {
        u64 player = 0, actor = 0;
        float matrix[12];
        if (!ReadGame(g_mainBase + PlayerPointer, &player, sizeof(player)) || player == 0)
            return false;
        if (!ReadGame(player + ActorOffset, &actor, sizeof(actor)) || actor == 0)
            return false;
        if (!ReadGame(actor + MatrixOffset, matrix, sizeof(matrix)))
            return false;
        // Row-major 3x4: translation in the last column, Link's forward (local +Z) in the third one.
        pose.x = matrix[3];
        pose.y = matrix[7];
        pose.z = matrix[11];
        pose.forwardX = matrix[2];
        pose.forwardZ = matrix[10];
        pose.fromCamera = false;
        for (float value : {pose.x, pose.y, pose.z})
            if (!std::isfinite(value))
                return false;
        if (pose.x == 0.0f && pose.y == 0.0f && pose.z == 0.0f)
            return false;

        u64 camera = 0;
        bool ok = ReadGame(g_mainBase + CameraPointer, &camera, sizeof(camera)) && camera != 0;
        for (u64 offset : CameraOffsets)
            ok = ok && ReadGame(camera + offset, &camera, sizeof(camera)) && camera != 0;
        float cameraPos[3];
        if (ok && ReadGame(camera + CameraXOffset, cameraPos, sizeof(cameraPos)))
        {
            const float dx = pose.x - cameraPos[0], dz = pose.z - cameraPos[2];
            const float distance = std::sqrt(dx * dx + dz * dz);
            // A real camera sits a few metres from Link; anything else means the pointer is not valid right now.
            if (std::isfinite(distance) && distance > 0.3f && distance < 60.0f)
            {
                pose.forwardX = dx;
                pose.forwardZ = dz;
                pose.fromCamera = true;
            }
        }
        return true;
    }

    float BearingDegrees(float dx, float dz)
    {
        // Map north is -Z; bearings grow clockwise.
        float degrees = std::atan2(dx, -dz) * 180.0f / Pi;
        return degrees < 0.0f ? degrees + 360.0f : degrees;
    }

    const char* CompassName(float bearing)
    {
        static const char* names[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
        return names[static_cast<int>((bearing + 22.5f) / 45.0f) % 8];
    }

    void UpdateNearby()
    {
        g_nearby.clear();
        g_missingInFilter = 0;
        if (!g_saveLoaded)
            return;
        for (size_t i = 0; i < g_items.size(); i++)
        {
            const Item& item = g_items[i];
            if (!InFilter(item.type) || !IsMissing(item))
                continue;
            g_missingInFilter++;
            if (!g_hasPose)
                continue;
            const float dx = item.x - g_pose.x;
            const float dz = item.z - g_pose.z;
            g_nearby.push_back(Nearby{static_cast<int>(i), std::sqrt(dx * dx + dz * dz), BearingDegrees(dx, dz)});
        }
        const size_t shown = std::min<size_t>(ShownCount, g_nearby.size());
        std::partial_sort(g_nearby.begin(), g_nearby.begin() + shown, g_nearby.end(),
                          [](const Nearby& a, const Nearby& b) { return a.distance < b.distance; });
        g_nearby.resize(shown);
        if (g_selected >= static_cast<int>(shown))
            g_selected = shown > 0 ? static_cast<int>(shown) - 1 : 0;
    }

    void RefreshPose()
    {
        if (!g_attached)
            g_attached = Attach();
        if (g_attached)
        {
            Pose pose;
            g_hasPose = ReadPose(pose);
            if (g_hasPose)
                g_pose = pose;
        }
    }

    void Refresh()
    {
        RefreshPose();
        UpdateNearby();
    }

    class DmntMemory : public LiveGameData::Memory
    {
    public:
        bool Read(uint64_t address, void* out, size_t size) override { return ReadGame(address, out, size); }
    };

    // Keeps, for the objects of the item table, whether the game has them as found and where their flag is.
    class ItemVisitor : public LiveGameData::Visitor
    {
    public:
        std::vector<s8> live;          // -1: flag not seen, 0: not found, 1: found
        std::vector<u64> addresses;
        std::vector<bool> isNumber;
        s32 playtime = 0;
        bool dlc = false;

        explicit ItemVisitor(size_t count) : live(count, -1), addresses(count, 0), isNumber(count, false) {}

        void OnFlag(LiveGameData::FlagKind kind, uint32_t hash, const uint8_t* value, uint64_t address) override
        {
            s32 number;
            if (kind == LiveGameData::FlagKind::Bool)
                number = value[0] & 1;
            else if (kind == LiveGameData::FlagKind::S32)
                std::memcpy(&number, value, sizeof(number));
            else
                return;

            if (hash == PlaytimeHash && number > playtime)
                playtime = number;
            else if (hash == BalladOfHeroesReadyHash && number == 1)
                dlc = true;

            const auto it = g_itemByHash.find(hash);
            if (it == g_itemByHash.end())
                return;
            // The same flag twice: found in either one counts.
            if (number != 0 || live[it->second] < 0)
            {
                live[it->second] = number != 0;
                addresses[it->second] = address;
                isNumber[it->second] = kind == LiveGameData::FlagKind::S32;
            }
        }
    };

    // Reads the game's flags and applies the ones the item table knows. On any doubt it changes nothing and the
    // save backup stays in use. A short report goes to SyncLogPath (overwritten each time).
    bool SyncFromGame(std::string& error)
    {
        g_liveSynced = false;
        if (!g_attached)
            g_attached = Attach();
        if (!g_attached)
        {
            error = g_gameMessage;
            return false;
        }

        DmntMemory memory;
        ItemVisitor visitor(g_items.size());
        LiveGameData::Report report;
        const bool scanned = LiveGameData::Scan(memory, g_mainBase, LiveGameData::Botw160(), visitor, report);

        int matched = 0;
        for (s8 value : visitor.live)
            matched += value >= 0;
        if (FILE* file = fopen(SyncLogPath, "w"))
        {
            fprintf(file, "BotW overlay " APP_VERSION " live sync\n%sitems %d of %d, playtime %d, dlc %d\n",
                    report.details.c_str(), matched, static_cast<int>(g_items.size()), visitor.playtime, visitor.dlc);
            fclose(file);
        }

        if (!scanned)
        {
            error = report.error;
            return false;
        }
        if (visitor.playtime == 0)
        {
            error = "the game has not loaded a save yet";
            return false;
        }
        // Every object of the table has a flag in the game.
        if (matched < static_cast<int>(g_items.size()))
        {
            error = "the game's flags do not match this overlay";
            return false;
        }

        for (size_t i = 0; i < g_items.size(); i++)
        {
            g_items[i].found = visitor.live[i] > 0;
            g_items[i].flagAddress = visitor.addresses[i];
            g_items[i].flagIsNumber = visitor.isNumber[i];
        }
        if (!visitor.dlc)
            for (Item& item : g_items)
                if (item.type == Data::ObjectType::DLCShrine)
                    item.found = true;

        g_saveLoaded = true;
        g_liveSynced = true;
        return true;
    }

    // Save backup first (profile for the manual marks, fallback values), then the game's flags on top.
    void LoadProgress(bool announce)
    {
        LoadSave();
        std::string error;
        if (SyncFromGame(error))
        {
            if (announce)
            {
                g_notice = "Synced with the game";
                g_noticeIsWarning = false;
            }
        }
        else
        {
            g_notice = "Not live: " + error; // short enough for one line; the details are in the log
            g_noticeIsWarning = true;
        }
    }

    // Whether the game has this object's flag set right now (one small read).
    bool FlagIsSet(const Item& item)
    {
        if (item.flagAddress == 0)
            return false;
        if (item.flagIsNumber)
        {
            s32 number = 0;
            return ReadGame(item.flagAddress, &number, sizeof(number)) && number != 0;
        }
        u8 byte = 0;
        return ReadGame(item.flagAddress, &byte, sizeof(byte)) && (byte & 1) != 0;
    }

    void ReloadManualProgress()
    {
        ManualProgress::Clear();
        std::ifstream input(ManualProgressPath);
        if (input.is_open())
            ManualProgress::Load(input);
    }

    void MarkSelectedFound()
    {
        if (g_selected >= static_cast<int>(g_nearby.size()))
            return;
        const Item& item = g_items[g_nearby[g_selected].index];
        // Reload first so marks made by the app since the overlay opened are kept.
        ReloadManualProgress();
        if (ManualProgress::MarkFound(g_context, item.type, item.hash) && ManualProgress::Flush())
        {
            g_lastMark = LastMark{item.type, item.hash, item.name};
            g_hasLastMark = true;
            g_notice = std::string("Marked as found: ") + item.name;
            g_noticeIsWarning = false;
        }
        else
        {
            g_notice = "Could not save the mark";
            g_noticeIsWarning = true;
        }
        UpdateNearby();
    }

    void UndoLastMark()
    {
        g_noticeIsWarning = true;
        if (!g_hasLastMark)
        {
            g_notice = "Nothing to undo";
            return;
        }
        g_hasLastMark = false;
        ReloadManualProgress();
        ManualProgress::State state = ManualProgress::CaptureState();
        const auto it = std::find_if(state.entries.begin(), state.entries.end(), [](const ManualProgress::Entry& e) {
            return e.type == g_lastMark.type && e.completionHash == g_lastMark.hash &&
                   e.context.profileId1 == g_context.profileId1 && e.context.profileId2 == g_context.profileId2 &&
                   e.context.masterMode == g_context.masterMode;
        });
        if (it == state.entries.end())
        {
            g_notice = "The last mark is already gone";
            return;
        }
        state.entries.erase(it);
        ManualProgress::RestoreState(state);
        std::ofstream output(ManualProgressPath);
        if (output.is_open() && ManualProgress::Save(output))
        {
            g_notice = std::string("Mark undone: ") + g_lastMark.name;
            g_noticeIsWarning = false;
        }
        else
            g_notice = "Could not undo the mark";
        UpdateNearby();
    }

    const tsl::Color White(15, 15, 15, 15), Grey(10, 10, 10, 15), Accent(0, 13, 15, 15), Warn(15, 11, 4, 15),
                     Good(6, 14, 8, 15);

    // A compass rose: the dots point at the object, with "up" being where the camera looks.
    void DrawDial(tsl::gfx::Renderer* renderer, s32 cx, s32 cy, float relativeDegrees, float radius)
    {
        renderer->drawCircle(cx, cy, static_cast<u16>(radius), true, tsl::Color(2, 3, 4, 13));
        renderer->drawCircle(cx, cy, 3, true, tsl::Color(8, 8, 8, 15));
        const float radians = relativeDegrees * Pi / 180.0f;
        const float sx = std::sin(radians), sy = -std::cos(radians);
        const float steps[3] = {0.32f, 0.55f, 0.78f};
        const u16 sizes[3] = {2, 3, 4};
        for (int i = 0; i < 3; i++)
        {
            const float r = steps[i] * radius;
            renderer->drawCircle(cx + static_cast<s32>(sx * r), cy + static_cast<s32>(sy * r),
                                 static_cast<u16>(sizes[i] * radius / 22.0f), true, Accent);
        }
    }

    const char* TypeName(Data::ObjectType type)
    {
        switch (type)
        {
        case Data::ObjectType::Korok: return "Korok seed";
        case Data::ObjectType::Shrine: return "Shrine";
        case Data::ObjectType::DLCShrine: return "Shrine (DLC)";
        case Data::ObjectType::Location: return "Location";
        case Data::ObjectType::Hinox: return "Hinox";
        case Data::ObjectType::Talus: return "Stone Talus";
        case Data::ObjectType::Molduga: return "Molduga";
        default: return "";
        }
    }

    // Where the progress comes from, with a coloured dot.
    void DrawStatus(tsl::gfx::Renderer* renderer, s32 x, s32 baseline)
    {
        const bool live = g_liveSynced && g_gameMessage.empty();
        const std::string& text = !g_gameMessage.empty() ? g_gameMessage : live ? std::string("Live from the game") : g_saveMessage;
        renderer->drawCircle(x + 5, baseline - 5, 4, true, live ? Good : Warn);
        renderer->drawString(text, false, x + 16, baseline, 14, live ? Good : Warn);
    }

    void Draw(tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32 h)
    {
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), "%s: %d missing", FilterNames[static_cast<int>(g_filter)], g_missingInFilter);
        renderer->drawString(buffer, false, x + 20, y + 24, 20, White);
        DrawStatus(renderer, x + 20, y + 46);

        // The five nearest: a dial, the name and how far.
        const s32 rowsTop = y + 60;
        const s32 rowHeight = 56;
        const float facing = BearingDegrees(g_pose.forwardX, g_pose.forwardZ);
        if (g_nearby.empty())
        {
            const char* message = !g_gameMessage.empty() ? "" : !g_hasPose ? "Waiting for Link's position..."
                                : g_saveLoaded ? "Nothing missing here." : "Open BotW Unexplored once with the game closed.";
            renderer->drawString(message, false, x + 20, rowsTop + 30, 15, Grey);
        }
        for (size_t i = 0; i < g_nearby.size(); i++)
        {
            const Nearby& near = g_nearby[i];
            const Item& item = g_items[near.index];
            const s32 top = rowsTop + static_cast<s32>(i) * rowHeight;
            if (static_cast<int>(i) == g_selected)
                renderer->drawRect(x + 10, top, w - 20, rowHeight - 4, tsl::Color(3, 5, 8, 11));
            DrawDial(renderer, x + 40, top + 26, near.bearing - facing, 19.0f);
            renderer->drawString(Shorten(item.name, 30), false, x + 72, top + 22, 17, White);
            std::snprintf(buffer, sizeof(buffer), "%.0f m  %s", near.distance, CompassName(near.bearing));
            renderer->drawString(buffer, false, x + 72, top + 43, 15, Accent);
        }

        // The selected one in full: how to get it.
        const s32 detailTop = rowsTop + ShownCount * rowHeight + 6;
        const s32 detailBottom = y + h - 72;
        if (g_selected < static_cast<int>(g_nearby.size()) && detailBottom - detailTop >= 40)
        {
            const Item& item = g_items[g_nearby[g_selected].index];
            renderer->drawRect(x + 10, detailTop, w - 20, detailBottom - detailTop, tsl::Color(1, 2, 3, 10));
            const s32 textWidth = w - 44;
            const size_t maxLines = static_cast<size_t>(std::max<s32>(1, (detailBottom - detailTop - 12) / 19));
            if (g_wrappedHash != item.hash || g_wrappedWidth != textWidth)
            {
                const std::string text = *item.hint ? std::string(item.hint) : std::string(TypeName(item.type));
                WrapText(renderer, text, 14, textWidth, maxLines, g_wrappedLines);
                g_wrappedHash = item.hash;
                g_wrappedWidth = textWidth;
            }
            for (size_t line = 0; line < g_wrappedLines.size() && line < maxLines; line++)
                renderer->drawString(g_wrappedLines[line], false, x + 22, detailTop + 22 + static_cast<s32>(line) * 19, 14, White);
        }

        // Messages and what the buttons do
        const s32 bottom = y + h;
        if (g_holdHash != 0)
        {
            renderer->drawString("Keep holding A to mark it as found", false, x + 20, bottom - 52, 14, Warn);
            renderer->drawRect(x + 20, bottom - 46, static_cast<s32>((w - 40) * g_holdProgress), 4, Warn);
        }
        else if (!g_notice.empty())
            renderer->drawString(Shorten(g_notice, 52), false, x + 20, bottom - 52, 14, g_noticeIsWarning ? Warn : Good);
        renderer->drawString("ZR: guide me there   X: sync   Y: filter", false, x + 20, bottom - 30, 13, Grey);
        renderer->drawString(g_liveSynced ? "Up/Down: select" : "Up/Down: select   hold A: found   -: undo", false,
                             x + 20, bottom - 12, 13, Grey);
    }

    const Item* GuideTarget()
    {
        const auto found = g_itemByHash.find(g_guideHash);
        return found == g_itemByHash.end() ? nullptr : &g_items[found->second];
    }

    // The missing object of the filter nearest to Link, other than the current target. 0 when there is none.
    u32 NextGuideTarget()
    {
        u32 best = 0;
        float bestDistance = 0.0f;
        for (const Item& item : g_items)
        {
            if (item.hash == g_guideHash || !InFilter(item.type) || !IsMissing(item))
                continue;
            const float dx = item.x - g_pose.x, dz = item.z - g_pose.z;
            const float distance = dx * dx + dz * dz;
            if (best == 0 || distance < bestDistance)
            {
                best = item.hash;
                bestDistance = distance;
            }
        }
        return best;
    }

    // Guide (GPS) mode: a small box that stays on screen while the game keeps the controls.
    void DrawGuide(tsl::gfx::Renderer* renderer)
    {
        const s32 bw = 348, bh = 114, bx = static_cast<s32>(tsl::cfg::FramebufferWidth) - bw - 12, by = 16;
        renderer->drawRect(bx, by, bw, bh, tsl::Color(0, 0, 0, 10));
        const Item* item = GuideTarget();
        if (item == nullptr)
        {
            renderer->drawString("Guide: unknown object", false, bx + 12, by + 34, 16, Warn);
            return;
        }
        const s32 textX = bx + 92;
        if (!g_gameMessage.empty() || !g_hasPose)
        {
            renderer->drawString(Shorten(item->name, 26), false, bx + 12, by + 30, 17, White);
            renderer->drawString(g_gameMessage.empty() ? std::string("Waiting for Link's position...") : g_gameMessage,
                                 false, bx + 12, by + 58, 14, Warn);
            return;
        }
        const float dx = item->x - g_pose.x, dz = item->z - g_pose.z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        const float bearing = BearingDegrees(dx, dz);
        const float facing = BearingDegrees(g_pose.forwardX, g_pose.forwardZ);
        DrawDial(renderer, bx + 46, by + 50, bearing - facing, 34.0f);
        renderer->drawString(Shorten(item->name, 26), false, textX, by + 28, 17, White);

        char buffer[64];
        if (g_guideFound)
            renderer->drawString("Found!", false, textX, by + 56, 21, Good);
        else if (distance < ArrivedMeters)
            renderer->drawString("You are there", false, textX, by + 56, 21, Warn);
        else
        {
            std::snprintf(buffer, sizeof(buffer), "%.0f m  %s", distance, CompassName(bearing));
            renderer->drawString(buffer, false, textX, by + 56, 21, Accent);
        }

        // Two small lines: how to get it once Link is close, otherwise what the guide does.
        const s32 smallWidth = bw - 104;
        std::string first, second;
        if (g_guideFound)
            first = g_guideNothingLeft ? "Nothing else is missing here." : "Finding the next one...";
        else if (distance < HintMeters && *item->hint)
        {
            if (g_wrappedHash != item->hash || g_wrappedWidth != smallWidth)
            {
                WrapText(renderer, item->hint, 12, smallWidth, 2, g_wrappedLines);
                g_wrappedHash = item->hash;
                g_wrappedWidth = smallWidth;
            }
            if (!g_wrappedLines.empty()) first = g_wrappedLines[0];
            if (g_wrappedLines.size() > 1) second = g_wrappedLines[1];
        }
        else
        {
            first = g_pose.fromCamera ? "Up on the dial = where the camera looks" : "Up on the dial = where Link faces";
            second = g_liveSynced ? "Goes to the next one by itself. Combo: list" : "Ultrahand combo: back to the list";
        }
        renderer->drawString(first, false, textX, by + 78, 12, Grey);
        renderer->drawString(second, false, textX, by + 95, 12, Grey);
    }

    class NearbyGui : public tsl::Gui
    {
    public:
        tsl::elm::Element* createUI() override
        {
            auto* frame = new tsl::elm::OverlayFrame("BotW Unexplored", "Missing nearby");
            frame->setContent(new tsl::elm::CustomDrawer(
                [](tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32 h) { Draw(renderer, x, y, w, h); }));
            return frame;
        }

        void update() override
        {
            if (!m_loaded)
            {
                tsl::gfx::Renderer::get().setLayerPos(0, 0);
                LoadProgress(false);
                m_loaded = true;
                m_frames = RefreshFrames;
            }
            if (++m_frames >= RefreshFrames)
            {
                m_frames = 0;
                Refresh();
            }
        }

        bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState& touchPos,
                         HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override
        {
            const int shown = static_cast<int>(g_nearby.size());
            if (g_holdHash != 0)
            {
                if (!(keysHeld & KEY_A))
                {
                    g_holdHash = 0;
                    g_holdProgress = 0.0f;
                }
                else
                {
                    const u64 elapsed = armTicksToNs(armGetSystemTick() - g_holdTick);
                    g_holdProgress = std::min(1.0f, static_cast<float>(elapsed) / static_cast<float>(HoldToMarkNs));
                    if (elapsed >= HoldToMarkNs)
                    {
                        const bool same = g_selected < shown && g_items[g_nearby[g_selected].index].hash == g_holdHash;
                        g_holdHash = 0;
                        g_holdProgress = 0.0f;
                        if (same)
                            MarkSelectedFound();
                    }
                    return true;
                }
            }
            if (keysDown & KEY_UP)
            {
                if (shown > 0)
                    g_selected = (g_selected + shown - 1) % shown;
                return true;
            }
            if (keysDown & KEY_DOWN)
            {
                if (shown > 0)
                    g_selected = (g_selected + 1) % shown;
                return true;
            }
            if (keysDown & KEY_Y)
            {
                g_filter = static_cast<Filter>((static_cast<int>(g_filter) + 1) % static_cast<int>(Filter::Count));
                g_selected = 0;
                UpdateNearby();
                return true;
            }
            if (keysDown & KEY_X)
            {
                LoadProgress(true);
                UpdateNearby();
                return true;
            }
            if ((keysDown & KEY_MINUS) && !g_liveSynced)
            {
                UndoLastMark();
                return true;
            }
            // With the game's own flags there is nothing to mark by hand: A is the guide too.
            if ((keysDown & (g_liveSynced ? (KEY_ZR | KEY_A) : KEY_ZR)) && g_selected < shown)
            {
                const u32 hash = g_items[g_nearby[g_selected].index].hash;
                tsl::setNextOverlay(SelfPath, "-guide " + std::to_string(hash) + " -filter " +
                                                  std::to_string(static_cast<int>(g_filter)));
                tsl::Overlay::get()->close();
                return true;
            }
            if ((keysDown & KEY_A) && g_selected < shown)
            {
                g_holdHash = g_items[g_nearby[g_selected].index].hash;
                g_holdTick = armGetSystemTick();
                g_holdProgress = 0.0f;
                g_notice.clear();
                return true;
            }
            return false;
        }

    private:
        bool m_loaded = false;
        int m_frames = 0;
    };

    class GuideGui : public tsl::Gui
    {
    public:
        tsl::elm::Element* createUI() override
        {
            auto* frame = new tsl::elm::HeaderOverlayFrame("", "");
            frame->setContent(new tsl::elm::CustomDrawer(
                [](tsl::gfx::Renderer* renderer, s32, s32, s32, s32) { DrawGuide(renderer); }));
            return frame;
        }

        void update() override
        {
            // The game keeps the controls, so read the pad directly: the Ultrahand combo (once it has been released
            // after the guide opened) brings back the list, the same way libultrahand returns to an overlay's menu.
            if (!m_padReady)
            {
                padInitializeAny(&m_pad);
                m_padReady = true;
            }
            padUpdate(&m_pad);
            const u64 held = padGetButtons(&m_pad);
            const auto comboHeld = [held](u64 combo) { return combo != 0 && (held & combo) == combo; };
            if (!comboHeld(tsl::cfg::launchCombo) && !comboHeld(tsl::cfg::launchCombo2))
                m_armed = true;
            else if (m_armed)
            {
                m_armed = false;
                // Back to the list, with the same filter
                tsl::setNextOverlay(SelfPath, "--direct -filter " + std::to_string(static_cast<int>(g_filter)));
                launchComboHasTriggered.store(true, std::memory_order_release);
                tsl::Overlay::get()->close();
                return;
            }
            if (!m_placed)
            {
                tsl::gfx::Renderer::get().setLayerPos(tsl::cfg::ScreenWidth - tsl::cfg::LayerWidth, 0);
                // What is still missing, to pick the next target when this one is collected.
                std::string error;
                LoadSave();
                SyncFromGame(error);
                m_placed = true;
            }
            if (++m_frames >= GuideRefreshFrames)
            {
                m_frames = 0;
                RefreshPose();
            }
            if (g_liveSynced && g_hasPose)
                FollowTarget();
        }

    private:
        // Notices when the target is collected and, a moment later, moves on to the nearest one still missing.
        void FollowTarget()
        {
            if (!g_guideFound)
            {
                if (++m_flagFrames < GuideFlagFrames)
                    return;
                m_flagFrames = 0;
                const Item* item = GuideTarget();
                if (item != nullptr && FlagIsSet(*item))
                {
                    g_guideFound = true;
                    g_guideFoundTick = armGetSystemTick();
                }
                return;
            }
            if (g_guideNothingLeft || armTicksToNs(armGetSystemTick() - g_guideFoundTick) < GuideNextNs)
                return;
            // Read everything again: other objects may have been collected on the way.
            std::string error;
            if (!SyncFromGame(error))
            {
                g_guideNothingLeft = true;
                return;
            }
            const u32 next = NextGuideTarget();
            if (next == 0)
            {
                g_guideNothingLeft = true;
                return;
            }
            g_guideHash = next;
            g_guideFound = false;
            m_flagFrames = 0;
        }

        int m_frames = GuideRefreshFrames;
        int m_flagFrames = 0;
        bool m_placed = false;
        PadState m_pad{};
        bool m_padReady = false;
        bool m_armed = false;
    };

    class NearbyOverlay : public tsl::Overlay
    {
    public:
        void initServices() override
        {
            if (CheatServiceRunning())
                g_dmntReady = R_SUCCEEDED(dmntchtInitialize());
        }

        void exitServices() override
        {
            if (g_dmntReady)
                dmntchtExit();
        }

        void onShow() override
        {
            // The game may have been restarted while the overlay was hidden.
            g_attached = false;
        }

        std::unique_ptr<tsl::Gui> loadInitialGui() override
        {
            return initially<NearbyGui>();
        }
    };

    class GuideOverlay : public tsl::Overlay
    {
    public:
        void initServices() override
        {
            if (CheatServiceRunning())
                g_dmntReady = R_SUCCEEDED(dmntchtInitialize());
            BuildItems();
        }

        void exitServices() override
        {
            if (g_dmntReady)
                dmntchtExit();
        }

        void onShow() override
        {
            // Let the game keep the controls while the guide stays on screen (like Status Monitor's mini mode).
            tsl::hlp::requestForeground(false);
            g_attached = false;
        }

        std::unique_ptr<tsl::Gui> loadInitialGui() override
        {
            return initially<GuideGui>();
        }
    };
}

int main(int argc, char** argv)
{
    // The Ultrahand combo in guide mode brings back this overlay's list instead of the Ultrahand menu.
    comboReturnToSelfMenu.store(true, std::memory_order_release);
    for (int i = 1; i + 1 < argc; i++)
    {
        if (std::strcmp(argv[i], "-guide") == 0)
            g_guideHash = static_cast<u32>(std::strtoul(argv[i + 1], nullptr, 10));
        else if (std::strcmp(argv[i], "-filter") == 0)
        {
            const int filter = std::atoi(argv[i + 1]);
            if (filter >= 0 && filter < static_cast<int>(Filter::Count))
                g_filter = static_cast<Filter>(filter);
        }
    }
    if (g_guideHash != 0)
    {
        FullMode = false;
        deactivateOriginalFooter = true;
        return tsl::loop<GuideOverlay>(argc, argv);
    }
    return tsl::loop<NearbyOverlay>(argc, argv);
}
