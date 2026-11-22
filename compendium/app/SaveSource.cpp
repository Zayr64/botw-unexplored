#include "SaveSource.h"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#include <switch.h>

#include "Log.h"

namespace SaveSource
{
    namespace
    {
        const u64 BotwId = 0x01007ef00011e000ULL;
        const char* AppFolder = "sdmc:/switch/botw-compendium";
        const char* CopyFolder = "sdmc:/switch/botw-compendium/save";

        bool ReadFile(const std::string& path, std::vector<uint8_t>& out)
        {
            FILE* f = fopen(path.c_str(), "rb");
            if (!f)
                return false;
            out.clear();
            uint8_t buffer[16384];
            size_t n;
            while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0)
                out.insert(out.end(), buffer, buffer + n);
            fclose(f);
            return !out.empty();
        }

        bool WriteFile(const std::string& path, const std::vector<uint8_t>& bytes)
        {
            FILE* f = fopen(path.c_str(), "wb");
            if (!f)
                return false;
            const bool ok = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
            return fclose(f) == 0 && ok;
        }

        // caption.sav keeps the time of the save as a u32 at 0x10.
        uint32_t CaptionTime(const std::string& path)
        {
            std::vector<uint8_t> caption;
            if (!ReadFile(path, caption) || caption.size() < 0x14)
                return 0;
            uint32_t t;
            memcpy(&t, caption.data() + 0x10, sizeof(t));
            return t;
        }

        uint32_t FileTime(const std::string& path)
        {
            struct stat info;
            return stat(path.c_str(), &info) == 0 ? static_cast<uint32_t>(info.st_mtime) : 0;
        }

        bool FindUser(AccountUid& uid)
        {
            if (R_FAILED(accountInitialize(AccountServiceType_Administrator)))
            {
                Log("accountInitialize() failed");
                return false;
            }
            bool ok = R_SUCCEEDED(accountGetLastOpenedUser(&uid)) && accountUidIsValid(&uid);
            if (!ok)
                ok = R_SUCCEEDED(accountGetPreselectedUser(&uid)) && accountUidIsValid(&uid);
            if (!ok)
            {
                PselUserSelectionSettings settings;
                memset(&settings, 0, sizeof(settings));
                ok = R_SUCCEEDED(pselShowUserSelector(&uid, &settings)) && accountUidIsValid(&uid);
            }
            accountExit();
            return ok;
        }

        std::string ProfileFolder(const AccountUid& uid)
        {
            // Same naming as BotW Unexplored's backups.
            char buf[64];
            snprintf(buf, sizeof(buf), "%llu - %llu", static_cast<unsigned long long>(uid.uid[0]),
                     static_cast<unsigned long long>(uid.uid[1]));
            return buf;
        }

        void KeepCopy(const Loaded& loaded, const std::vector<uint8_t>& caption, const std::string& profile)
        {
            mkdir("sdmc:/switch", 0777);
            mkdir(AppFolder, 0777);
            mkdir(CopyFolder, 0777);
            const std::string folder = std::string(CopyFolder) + "/" + profile;
            mkdir(folder.c_str(), 0777);
            if (!WriteFile(folder + "/game_data.sav", loaded.bytes))
                Log("Could not keep a copy of the save");
            if (!caption.empty() && !WriteFile(folder + "/caption.sav", caption))
                Log("Could not keep a copy of caption.sav");
        }

        // The newest copy among this app's and BotW Unexplored's (any slot).
        void LoadCopy(const std::string& profile, Loaded& out)
        {
            struct Candidate
            {
                std::string save;
                std::string caption;
                Kind kind;
                int slot;
            };
            std::vector<Candidate> candidates;
            candidates.push_back({std::string(CopyFolder) + "/" + profile + "/game_data.sav",
                                  std::string(CopyFolder) + "/" + profile + "/caption.sav", Kind::Copy, -1});
            for (int slot = 0; slot <= 5; slot++)
            {
                const std::string dir = "sdmc:/switch/botw-unexplored/saves/" + profile + "/" + std::to_string(slot);
                candidates.push_back({dir + "/game_data.sav", dir + "/caption.sav", Kind::UnexploredCopy, slot});
            }
            uint32_t best = 0;
            for (const Candidate& c : candidates)
            {
                if (FileTime(c.save) == 0)
                    continue;
                uint32_t t = CaptionTime(c.caption);
                const bool fromCaption = t != 0;
                if (!fromCaption)
                    t = FileTime(c.save);
                if (t <= best)
                    continue;
                std::vector<uint8_t> bytes;
                if (!ReadFile(c.save, bytes))
                    continue;
                best = t;
                out.bytes.swap(bytes);
                out.kind = c.kind;
                out.slot = c.slot;
                out.saveTime = fromCaption ? t : 0;
            }
        }
    }

    const char* KindText(Kind kind)
    {
        switch (kind)
        {
        case Kind::Game: return "Save read from the game";
        case Kind::Copy: return "Copy of the save on the SD card";
        case Kind::UnexploredCopy: return "BotW Unexplored's copy of the save";
        default: return "No save";
        }
    }

    Loaded Load()
    {
        Loaded out;
        AccountUid uid;
        memset(&uid, 0, sizeof(uid));
        if (!FindUser(uid))
        {
            out.message = "No user profile selected";
            Log(out.message);
            return out;
        }
        const std::string profile = ProfileFolder(uid);

        if (R_SUCCEEDED(fsdevMountSaveDataReadOnly("save", BotwId, uid)))
        {
            uint32_t newest = 0;
            int slot = -1;
            for (int i = 0; i <= 5; i++)
            {
                const std::string dir = "save:/" + std::to_string(i);
                if (FileTime(dir + "/game_data.sav") == 0 && CaptionTime(dir + "/caption.sav") == 0)
                {
                    // stat() may not report times on the save device: check the file opens instead.
                    FILE* f = fopen((dir + "/game_data.sav").c_str(), "rb");
                    if (!f)
                        continue;
                    fclose(f);
                }
                const uint32_t t = CaptionTime(dir + "/caption.sav");
                if (slot == -1 || t > newest)
                {
                    newest = t;
                    slot = i;
                }
            }
            std::vector<uint8_t> caption;
            if (slot >= 0)
            {
                const std::string dir = "save:/" + std::to_string(slot);
                if (ReadFile(dir + "/game_data.sav", out.bytes))
                {
                    ReadFile(dir + "/caption.sav", caption);
                    out.kind = Kind::Game;
                    out.slot = slot;
                    out.saveTime = newest;
                }
            }
            fsdevUnmountDevice("save");
            if (out.kind == Kind::Game)
            {
                Log("Read the save from the game, slot", slot);
                KeepCopy(out, caption, profile);
                return out;
            }
            out.message = "The user has no BotW save in Normal Mode";
            Log(out.message);
            return out;
        }

        // The save is locked while BotW runs (or the user has none): use a copy.
        out.gameRunning = true;
        LoadCopy(profile, out);
        if (out.kind == Kind::None)
        {
            out.message = "BotW is open and there is no copy of the save yet: open this app once with the game closed";
            Log(out.message);
        }
        else
        {
            Log("Using a copy of the save:", KindText(out.kind));
        }
        return out;
    }
}
