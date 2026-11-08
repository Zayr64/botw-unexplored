// BotW Unexplored overlay: the missing objects nearest to Link, while the game runs.
//
// Link's position is read (read only) through Atmosphere's cheat service (dmnt:cht), from the pointer chain
// [[main+2CA1140]+60]+398 (a sead::Matrix34f; translation at +3A4/+3B4/+3C4). The chain is the one used by
// Pistonight/botw-save-state for BotW 1.6.0 and was checked on this console on 2026-11-07.
// What is missing comes from the save backup the app copies to sdmc:/switch/botw-unexplored/saves/ and from the
// app's manual_progress.dat, which this overlay also writes when an object is marked as found.

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

#include "ManualProgress.h"
#include "ObjectTypes.h"
#include "dmntcht.h"

namespace
{
    constexpr u64 BotwTitleId = 0x01007EF00011E000ULL;
    constexpr u8 BotwBuildId[8] = {0x8E, 0x99, 0x78, 0xD5, 0x0B, 0xDD, 0x20, 0xB4}; // 1.6.0
    constexpr u64 PlayerPointer = 0x2CA1140;
    constexpr u64 ActorOffset = 0x60;
    constexpr u64 MatrixOffset = 0x398;
    // Camera position, [[[main+2CA5520]+20]+558]+CA4/CA8/CAC (same 1.6.0 pointer list as Link's position).
    constexpr u64 CameraPointer = 0x2CA5520;
    constexpr u64 CameraOffsets[2] = {0x20, 0x558};
    constexpr u64 CameraXOffset = 0xCA4;
    constexpr int GuideRefreshFrames = 5;   // about 12 reads per second in guide mode

    constexpr const char* SavesDir = "sdmc:/switch/botw-unexplored/saves";
    constexpr const char* ManualProgressPath = "sdmc:/switch/botw-unexplored/manual_progress.dat";
    constexpr u32 PlaytimeHash = 0x73c29681;
    constexpr u32 BalladOfHeroesReadyHash = 1186840637;
    constexpr size_t SaveHeaderSize = 0x0c;

    constexpr float Pi = 3.14159265f;
    constexpr int ShownCount = 5;
    constexpr int RefreshFrames = 15;      // about 4 position reads per second

    enum class Filter { All, Koroks, Shrines, Locations, Bosses, Count };
    const char* FilterNames[] = {"All", "Koroks", "Shrines", "Locations", "Hinox, Talus, Molduga"};

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

    constexpr const char* SelfPath = "sdmc:/switch/.overlays/ovlBotwUnexplored.ovl";
    constexpr u64 HoldToMarkNs = 1000000000ULL;
    u32 g_holdHash = 0;
    u64 g_holdTick = 0;
    float g_holdProgress = 0.0f;
    u32 g_guideHash = 0;

    struct LastMark
    {
        Data::ObjectType type;
        u32 hash;
        const char* name;
    };
    LastMark g_lastMark{};
    bool g_hasLastMark = false;

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
            g_saveMessage = "No save backup. Open BotW Unexplored once with the game closed.";
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
            g_saveMessage = "No save backup. Open BotW Unexplored once with the game closed.";
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
            g_saveMessage = "The save backup could not be read.";
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
        g_saveMessage = std::string("Save backup from ") + when + ", slot " + std::to_string(bestSlot);
        g_saveLoaded = true;
    }

    bool IsMissing(const Item& item)
    {
        return !item.found && !ManualProgress::IsMarkedFound(g_context, item.type, item.hash);
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
            g_gameMessage = "Atmosphere cheat service (dmnt) is not available.";
            return false;
        }
        u64 pid = 0;
        if (R_FAILED(pmdmntGetApplicationProcessId(&pid)))
        {
            g_gameMessage = "Start BotW first.";
            return false;
        }
        dmntchtForceOpenCheatProcess();
        DmntCheatProcessMetadata metadata = {};
        if (R_FAILED(dmntchtGetCheatProcessMetadata(&metadata)))
        {
            g_gameMessage = "Could not attach to the game.";
            return false;
        }
        if (metadata.title_id != BotwTitleId)
        {
            g_gameMessage = "The running game is not BotW.";
            return false;
        }
        if (std::memcmp(metadata.main_nso_build_id, BotwBuildId, sizeof(BotwBuildId)) != 0)
        {
            char bid[24];
            std::snprintf(bid, sizeof(bid), "%02X%02X%02X%02X%02X%02X%02X%02X", metadata.main_nso_build_id[0],
                          metadata.main_nso_build_id[1], metadata.main_nso_build_id[2],
                          metadata.main_nso_build_id[3], metadata.main_nso_build_id[4],
                          metadata.main_nso_build_id[5], metadata.main_nso_build_id[6],
                          metadata.main_nso_build_id[7]);
            g_gameMessage = std::string("Only BotW 1.6.0 is supported (build ") + bid + ").";
            return false;
        }
        g_mainBase = metadata.main_nso_extents.base;
        g_gameMessage.clear();
        return true;
    }

    bool ReadPose(Pose& pose)
    {
        u64 player = 0, actor = 0;
        float matrix[12];
        if (R_FAILED(dmntchtReadCheatProcessMemory(g_mainBase + PlayerPointer, &player, sizeof(player))) || player == 0)
            return false;
        if (R_FAILED(dmntchtReadCheatProcessMemory(player + ActorOffset, &actor, sizeof(actor))) || actor == 0)
            return false;
        if (R_FAILED(dmntchtReadCheatProcessMemory(actor + MatrixOffset, matrix, sizeof(matrix))))
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
        bool ok = R_SUCCEEDED(dmntchtReadCheatProcessMemory(g_mainBase + CameraPointer, &camera, sizeof(camera))) &&
                  camera != 0;
        for (u64 offset : CameraOffsets)
            ok = ok && R_SUCCEEDED(dmntchtReadCheatProcessMemory(camera + offset, &camera, sizeof(camera))) &&
                 camera != 0;
        float cameraPos[3];
        if (ok && R_SUCCEEDED(dmntchtReadCheatProcessMemory(camera + CameraXOffset, cameraPos, sizeof(cameraPos))))
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

    void Refresh()
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
        UpdateNearby();
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
            g_notice = std::string("Marked: ") + item.name + "   (-: undo)";
        }
        else
            g_notice = "Could not save the mark.";
        UpdateNearby();
    }

    void UndoLastMark()
    {
        if (!g_hasLastMark)
        {
            g_notice = "Nothing to undo.";
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
            g_notice = "The last mark is already gone.";
            return;
        }
        state.entries.erase(it);
        ManualProgress::RestoreState(state);
        std::ofstream output(ManualProgressPath);
        if (output.is_open() && ManualProgress::Save(output))
            g_notice = std::string("Undone: ") + g_lastMark.name;
        else
            g_notice = "Could not undo the mark.";
        UpdateNearby();
    }

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
                                 static_cast<u16>(sizes[i] * radius / 22.0f), true, tsl::Color(0, 13, 15, 15));
        }
    }

    void Draw(tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32 h)
    {
        const tsl::Color white(15, 15, 15, 15), grey(10, 10, 10, 15), accent(0, 13, 15, 15), warn(15, 11, 4, 15);
        s32 line = y + 26;

        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), "%s: %d missing   (Y: filter)", FilterNames[static_cast<int>(g_filter)],
                      g_missingInFilter);
        renderer->drawString(buffer, false, x + 20, line, 18, white);
        line += 26;
        renderer->drawString(g_saveMessage, false, x + 20, line, 14, g_saveLoaded ? grey : warn);
        line += 22;

        if (!g_gameMessage.empty())
            renderer->drawString(g_gameMessage, false, x + 20, line, 15, warn);
        else if (!g_hasPose)
            renderer->drawString("Loading... (Link's position is not available yet)", false, x + 20, line, 15, warn);
        else
        {
            std::snprintf(buffer, sizeof(buffer), "Link  X %.0f  Z %.0f  height %.0f m  %s %s", g_pose.x, g_pose.z,
                          g_pose.y, g_pose.fromCamera ? "camera" : "facing",
                          CompassName(BearingDegrees(g_pose.forwardX, g_pose.forwardZ)));
            renderer->drawString(buffer, false, x + 20, line, 14, grey);
        }
        line += 18;

        const float facing = BearingDegrees(g_pose.forwardX, g_pose.forwardZ);
        const s32 rowHeight = 88;
        for (size_t i = 0; i < g_nearby.size(); i++)
        {
            const Nearby& near = g_nearby[i];
            const Item& item = g_items[near.index];
            const s32 top = line + static_cast<s32>(i) * rowHeight;
            if (static_cast<int>(i) == g_selected)
                renderer->drawRect(x + 8, top, w - 16, rowHeight - 6, tsl::Color(3, 5, 8, 10));
            DrawDial(renderer, x + 42, top + 38, near.bearing - facing, 22.0f);
            renderer->drawString(Shorten(item.name, 34), false, x + 76, top + 26, 18, white);
            std::snprintf(buffer, sizeof(buffer), "%.0f m  %s", near.distance, CompassName(near.bearing));
            renderer->drawString(buffer, false, x + 76, top + 50, 16, accent);
            if (*item.hint)
                renderer->drawString(Shorten(item.hint, 48), false, x + 76, top + 72, 13, grey);
        }

        const s32 bottom = y + h - 60;
        if (g_holdHash != 0)
        {
            renderer->drawString("Keep holding A to mark it as found", false, x + 20, bottom, 15, warn);
            renderer->drawRect(x + 20, bottom + 6, static_cast<s32>((w - 40) * g_holdProgress), 4, warn);
        }
        else if (!g_notice.empty())
            renderer->drawString(g_notice, false, x + 20, bottom, 15, accent);
        renderer->drawString("ZR: guide (GPS)   hold A: found   -: undo", false, x + 20, bottom + 26, 13, grey);
        renderer->drawString("Up/Down: select   Y: filter   X: reload save", false, x + 20, bottom + 44, 13, grey);
    }

    // Guide (GPS) mode: a small box that stays on screen while the game keeps the controls.
    void DrawGuide(tsl::gfx::Renderer* renderer)
    {
        const tsl::Color white(15, 15, 15, 15), grey(11, 11, 11, 15), accent(0, 13, 15, 15), warn(15, 11, 4, 15);
        const s32 bw = 340, bh = 96, bx = static_cast<s32>(tsl::cfg::FramebufferWidth) - bw - 12, by = 16;
        renderer->drawRect(bx, by, bw, bh, tsl::Color(0, 0, 0, 9));
        const auto found = g_itemByHash.find(g_guideHash);
        if (found == g_itemByHash.end())
        {
            renderer->drawString("Guide: unknown object", false, bx + 12, by + 34, 16, warn);
            return;
        }
        const Item& item = g_items[found->second];
        if (!g_gameMessage.empty() || !g_hasPose)
        {
            renderer->drawString(Shorten(item.name, 28), false, bx + 12, by + 30, 17, white);
            renderer->drawString(g_gameMessage.empty() ? std::string("Loading...") : g_gameMessage, false, bx + 12,
                                 by + 58, 14, warn);
            return;
        }
        const float dx = item.x - g_pose.x, dz = item.z - g_pose.z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        const float bearing = BearingDegrees(dx, dz);
        const float facing = BearingDegrees(g_pose.forwardX, g_pose.forwardZ);
        DrawDial(renderer, bx + 48, by + bh / 2, bearing - facing, 36.0f);
        renderer->drawString(Shorten(item.name, 26), false, bx + 96, by + 30, 17, white);
        char buffer[64];
        const bool arrived = distance < 12.0f;
        if (arrived)
            std::snprintf(buffer, sizeof(buffer), "You are there");
        else
            std::snprintf(buffer, sizeof(buffer), "%.0f m  %s", distance, CompassName(bearing));
        renderer->drawString(buffer, false, bx + 96, by + 60, 21, arrived ? warn : accent);
        renderer->drawString(g_pose.fromCamera ? "Up = camera view   Combo: list" : "Up = Link facing   Combo: list",
                             false, bx + 96, by + 84, 12, grey);
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
                LoadSave();
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
                LoadSave();
                g_notice = "Save backup reloaded.";
                UpdateNearby();
                return true;
            }
            if (keysDown & KEY_MINUS)
            {
                UndoLastMark();
                return true;
            }
            if ((keysDown & KEY_ZR) && g_selected < shown)
            {
                const u32 hash = g_items[g_nearby[g_selected].index].hash;
                tsl::setNextOverlay(SelfPath, "-guide " + std::to_string(hash));
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
                tsl::setNextOverlay(SelfPath, "--direct");
                launchComboHasTriggered.store(true, std::memory_order_release);
                tsl::Overlay::get()->close();
                return;
            }
            if (!m_placed)
            {
                tsl::gfx::Renderer::get().setLayerPos(tsl::cfg::ScreenWidth - tsl::cfg::LayerWidth, 0);
                m_placed = true;
            }
            if (++m_frames >= GuideRefreshFrames)
            {
                m_frames = 0;
                RefreshPose();
            }
        }

    private:
        int m_frames = GuideRefreshFrames;
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
        if (std::strcmp(argv[i], "-guide") == 0)
            g_guideHash = static_cast<u32>(std::strtoul(argv[i + 1], nullptr, 10));
    if (g_guideHash != 0)
    {
        FullMode = false;
        deactivateOriginalFooter = true;
        return tsl::loop<GuideOverlay>(argc, argv);
    }
    return tsl::loop<NearbyOverlay>(argc, argv);
}
