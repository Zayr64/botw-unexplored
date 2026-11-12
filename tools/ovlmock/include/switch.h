// Host fake of the libnx pieces the overlay uses (to look at its layout on a PC).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sys/types.h>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef u32 Result;
typedef u32 Handle;
#define R_FAILED(rc) ((rc) != 0)
#define R_SUCCEEDED(rc) ((rc) == 0)
#define BITL(n) (1UL << (n))

enum : u64 {
    KEY_A = BITL(0), KEY_B = BITL(1), KEY_X = BITL(2), KEY_Y = BITL(3), KEY_LSTICK = BITL(4), KEY_RSTICK = BITL(5),
    KEY_L = BITL(6), KEY_R = BITL(7), KEY_ZL = BITL(8), KEY_ZR = BITL(9), KEY_PLUS = BITL(10), KEY_MINUS = BITL(11),
    KEY_LEFT = BITL(12), KEY_UP = BITL(13), KEY_RIGHT = BITL(14), KEY_DOWN = BITL(15),
};

struct HidAnalogStickState { s32 x; s32 y; };
struct HidTouchState { u64 delta_time; u32 attributes; u32 finger_id; u32 x; u32 y; u32 diameter_x; u32 diameter_y; u32 rotation_angle; u32 reserved; };
struct PadState { int unused; };
void padInitializeAny(PadState* pad);
void padUpdate(PadState* pad);
u64 padGetButtons(const PadState* pad);

struct Service { int unused; };
struct Event { int unused; };
struct MemoryInfo { u64 addr; u64 size; u32 type; u32 attr; u32 perm; u32 ipc_refcount; u32 device_refcount; u32 padding; };
struct SmServiceName { char name[8]; };
static inline SmServiceName smEncodeName(const char* name)
{
    SmServiceName encoded = {};
    std::strncpy(encoded.name, name, 8);
    return encoded;
}
Result smRegisterService(Handle* handle, SmServiceName name, bool isLight, s32 maxSessions);
Result smUnregisterService(SmServiceName name);
Result svcCloseHandle(Handle handle);
Result pmdmntGetApplicationProcessId(u64* pid);

u64 armGetSystemTick(void);
u64 armTicksToNs(u64 ticks);
