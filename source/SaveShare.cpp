#include "SaveShare.h"

#include <cstdio>
#include <utility>

namespace
{
    SaveShare::Shared s_Shared;
}

void SaveShare::Clear()
{
    s_Shared = Shared();
}

void SaveShare::SetSave(std::vector<uint8_t> bytes, int slot, uint32_t saveTime, const std::string& profile)
{
    s_Shared = Shared();
    s_Shared.from = From::Save;
    s_Shared.bytes = std::move(bytes);
    s_Shared.slot = slot;
    s_Shared.saveTime = saveTime;
    s_Shared.profile = profile;
}

void SaveShare::SetLive(std::vector<uint8_t> bytes, const std::string& profile)
{
    s_Shared = Shared();
    s_Shared.from = From::Live;
    s_Shared.bytes = std::move(bytes);
    s_Shared.profile = profile;
}

void SaveShare::SetLiveFailed(const std::string& message, const std::string& profile)
{
    s_Shared = Shared();
    s_Shared.from = From::LiveFailed;
    s_Shared.message = message;
    s_Shared.profile = profile;
}

const SaveShare::Shared& SaveShare::Get()
{
    return s_Shared;
}

std::string SaveShare::ProfileName(uint64_t uid1, uint64_t uid2)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%llu - %llu", static_cast<unsigned long long>(uid1),
                  static_cast<unsigned long long>(uid2));
    return buffer;
}
