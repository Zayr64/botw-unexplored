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

    constexpr const char* SavesDir = "sdmc:/switch/botw-unexplored/saves";
    constexpr const char* ManualProgressPath = "sdmc:/switch/botw-unexplored/manual_progress.dat";
    constexpr u32 PlaytimeHash = 0x73c29681;
    constexpr u32 BalladOfHeroesReadyHash = 1186840637;
    constexpr size_t SaveHeaderSize = 0x0c;

    constexpr float Pi = 3.14159265f;
    constexpr int ShownCount = 5;
    constexpr int RefreshFrames = 15;      // about 4 position reads per second
    constexpr u64 ConfirmTimeoutNs = 3000000000ULL;

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
        float forwardX = 0, forwardZ = 1;
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
    u32 g_confirmHash = 0;
    u64 g_confirmTick = 0;
    std::string g_notice;

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
        for (float value : {pose.x, pose.y, pose.z})
            if (!std::isfinite(value))
                return false;
        return pose.x != 0.0f || pose.y != 0.0f || pose.z != 0.0f;
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

    void MarkSelectedFound()
    {
        if (g_selected >= static_cast<int>(g_nearby.size()))
            return;
        Item& item = g_items[g_nearby[g_selected].index];
        // Reload first so marks made by the app since the overlay opened are kept.
        ManualProgress::Clear();
        std::ifstream input(ManualProgressPath);
        if (input.is_open())
            ManualProgress::Load(input);
        input.close();
        if (ManualProgress::MarkFound(g_context, item.type, item.hash) && ManualProgress::Flush())
            g_notice = std::string("Marked as found: ") + item.name;
        else
            g_notice = "Could not save the mark.";
        g_confirmHash = 0;
        UpdateNearby();
    }

    void DrawDial(tsl::gfx::Renderer* renderer, s32 cx, s32 cy, float relativeDegrees)
    {
        renderer->drawCircle(cx, cy, 22, true, tsl::Color(2, 3, 4, 13));
        renderer->drawCircle(cx, cy, 3, true, tsl::Color(8, 8, 8, 15));
        const float radians = relativeDegrees * Pi / 180.0f;
        const float sx = std::sin(radians), sy = -std::cos(radians);
        const float steps[3] = {7.0f, 12.0f, 17.0f};
        const u16 sizes[3] = {2, 3, 4};
        for (int i = 0; i < 3; i++)
            renderer->drawCircle(cx + static_cast<s32>(sx * steps[i]), cy + static_cast<s32>(sy * steps[i]), sizes[i],
                                 true, tsl::Color(0, 13, 15, 15));
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
            std::snprintf(buffer, sizeof(buffer), "Link  X %.0f  Z %.0f  height %.0f m  facing %s", g_pose.x, g_pose.z,
                          g_pose.y, CompassName(BearingDegrees(g_pose.forwardX, g_pose.forwardZ)));
            renderer->drawString(buffer, false, x + 20, line, 14, grey);
        }
        line += 18;

        const float facing = BearingDegrees(g_pose.forwardX, g_pose.forwardZ);
        const s32 rowHeight = 92;
        for (size_t i = 0; i < g_nearby.size(); i++)
        {
            const Nearby& near = g_nearby[i];
            const Item& item = g_items[near.index];
            const s32 top = line + static_cast<s32>(i) * rowHeight;
            if (static_cast<int>(i) == g_selected)
                renderer->drawRect(x + 8, top, w - 16, rowHeight - 6, tsl::Color(3, 5, 8, 10));
            DrawDial(renderer, x + 42, top + 40, near.bearing - facing);
            renderer->drawString(Shorten(item.name, 34), false, x + 76, top + 26, 18, white);
            std::snprintf(buffer, sizeof(buffer), "%.0f m  %s", near.distance, CompassName(near.bearing));
            renderer->drawString(buffer, false, x + 76, top + 50, 16, accent);
            if (*item.hint)
                renderer->drawString(Shorten(item.hint, 48), false, x + 76, top + 72, 13, grey);
        }

        s32 bottom = y + h - 44;
        if (g_confirmHash != 0)
            renderer->drawString("Press A again to mark it as found", false, x + 20, bottom, 15, warn);
        else if (!g_notice.empty())
            renderer->drawString(g_notice, false, x + 20, bottom, 15, accent);
        renderer->drawString("Up/Down: select   A: found   Y: filter   X: reload save", false, x + 20, bottom + 24,
                             13, grey);
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
                LoadSave();
                m_loaded = true;
                m_frames = RefreshFrames;
            }
            if (++m_frames >= RefreshFrames)
            {
                m_frames = 0;
                Refresh();
            }
            if (g_confirmHash != 0 && armGetSystemTick() - g_confirmTick > armNsToTicks(ConfirmTimeoutNs))
                g_confirmHash = 0;
        }

        bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState& touchPos,
                         HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override
        {
            const int shown = static_cast<int>(g_nearby.size());
            if (keysDown & KEY_UP)
            {
                if (shown > 0)
                    g_selected = (g_selected + shown - 1) % shown;
                g_confirmHash = 0;
                return true;
            }
            if (keysDown & KEY_DOWN)
            {
                if (shown > 0)
                    g_selected = (g_selected + 1) % shown;
                g_confirmHash = 0;
                return true;
            }
            if (keysDown & KEY_Y)
            {
                g_filter = static_cast<Filter>((static_cast<int>(g_filter) + 1) % static_cast<int>(Filter::Count));
                g_selected = 0;
                g_confirmHash = 0;
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
            if ((keysDown & KEY_A) && g_selected < shown)
            {
                const u32 hash = g_items[g_nearby[g_selected].index].hash;
                if (g_confirmHash == hash)
                    MarkSelectedFound();
                else
                {
                    g_confirmHash = hash;
                    g_confirmTick = armGetSystemTick();
                    g_notice.clear();
                }
                return true;
            }
            return false;
        }

    private:
        bool m_loaded = false;
        int m_frames = 0;
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
}

int main(int argc, char** argv)
{
    return tsl::loop<NearbyOverlay>(argc, argv);
}
