#include "CheatService.h"

#include <switch.h>

// Command ids and structures of dmnt:cht as documented by Atmosphere (dmnt_cheat_service.hpp).
enum
{
    CheatCommand_GetCheatProcessMetadata = 65002,
    CheatCommand_ForceOpenCheatProcess = 65003,
    CheatCommand_ReadCheatProcessMemory = 65102,
};

static Service g_cheatService;
static bool g_cheatServiceOpen = false;

bool cheatServiceExists(void)
{
    // Registering a name fails when a service already has it (or when this process may not register
    // services at all, in which case the cheat service of Atmosphere is assumed to be there).
    const SmServiceName name = smEncodeName("dmnt:cht");
    Handle handle = INVALID_HANDLE;
    if (R_FAILED(smRegisterService(&handle, name, false, 1)))
        return true;
    svcCloseHandle(handle);
    smUnregisterService(name);
    return false;
}

bool cheatServiceApplicationRunning(void)
{
    u64 processId = 0;
    if (R_FAILED(pmdmntInitialize()))
        return false;
    const Result result = pmdmntGetApplicationProcessId(&processId);
    pmdmntExit();
    return R_SUCCEEDED(result);
}

uint32_t cheatServiceOpen(void)
{
    if (g_cheatServiceOpen)
        return 0;
    const Result result = smGetService(&g_cheatService, "dmnt:cht");
    if (R_SUCCEEDED(result))
        g_cheatServiceOpen = true;
    return result;
}

void cheatServiceClose(void)
{
    if (!g_cheatServiceOpen)
        return;
    serviceClose(&g_cheatService);
    g_cheatServiceOpen = false;
}

uint32_t cheatServiceAttach(void)
{
    return serviceDispatch(&g_cheatService, CheatCommand_ForceOpenCheatProcess);
}

uint32_t cheatServiceGetMetadata(CheatServiceMetadata* out)
{
    return serviceDispatchOut(&g_cheatService, CheatCommand_GetCheatProcessMetadata, *out);
}

uint32_t cheatServiceRead(uint64_t address, void* buffer, size_t size)
{
    const struct
    {
        u64 address;
        u64 size;
    } in = {address, size};
    return serviceDispatchIn(&g_cheatService, CheatCommand_ReadCheatProcessMemory, in,
        .buffer_attrs = { SfBufferAttr_Out | SfBufferAttr_HipcMapAlias },
        .buffers = { { buffer, size } },
    );
}
