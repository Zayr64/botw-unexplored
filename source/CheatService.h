#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The few calls of Atmosphere's cheat service (dmnt:cht) needed to read the memory of the running game.
// Nothing here writes to the game. Every function returns a libnx Result (0 = success) unless noted.
#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint64_t base;
    uint64_t size;
} CheatServiceExtents;

typedef struct
{
    uint64_t process_id;
    uint64_t title_id;
    CheatServiceExtents main_nso;
    CheatServiceExtents heap;
    CheatServiceExtents alias;
    CheatServiceExtents address_space;
    uint8_t main_nso_build_id[0x20];
} CheatServiceMetadata;

// Whether dmnt:cht is registered. Asking for a service that does not exist would wait forever.
bool cheatServiceExists(void);
// Whether a game (or an app started over a game's title) is running.
bool cheatServiceApplicationRunning(void);

uint32_t cheatServiceOpen(void);
void cheatServiceClose(void);
// Makes dmnt attach to the running game, as it does by itself for games that have cheats.
uint32_t cheatServiceAttach(void);
uint32_t cheatServiceGetMetadata(CheatServiceMetadata* out);
uint32_t cheatServiceRead(uint64_t address, void* buffer, size_t size);

#ifdef __cplusplus
}
#endif
