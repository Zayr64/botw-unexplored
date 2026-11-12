// Host shim of the libnx API surface used by BotW Unexplored (for off-screen UI screenshots on a PC).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef u32 Result;
typedef u32 Handle;
#define R_FAILED(rc) ((rc) != 0)
#define R_SUCCEEDED(rc) ((rc) == 0)
#ifndef BIT
#define BIT(n) (1U << (n))
#endif
#define BITL(n) (1UL << (n))
#define JOYSTICK_MAX 32767

enum : u64 {
    HidNpadButton_A = BITL(0), HidNpadButton_B = BITL(1), HidNpadButton_X = BITL(2), HidNpadButton_Y = BITL(3),
    HidNpadButton_StickL = BITL(4), HidNpadButton_StickR = BITL(5), HidNpadButton_L = BITL(6), HidNpadButton_R = BITL(7),
    HidNpadButton_ZL = BITL(8), HidNpadButton_ZR = BITL(9), HidNpadButton_Plus = BITL(10), HidNpadButton_Minus = BITL(11),
    HidNpadButton_Left = BITL(12), HidNpadButton_Up = BITL(13), HidNpadButton_Right = BITL(14), HidNpadButton_Down = BITL(15),
};
enum { HidNpadStyleSet_NpadStandard = 0x1F };

struct HidAnalogStickState { s32 x; s32 y; };
struct HidTouchState { u64 delta_time; u32 attributes; u32 finger_id; u32 x; u32 y; u32 diameter_x; u32 diameter_y; u32 rotation_angle; u32 reserved; };
struct HidTouchScreenState { u64 sampling_number; s32 count; u32 reserved; HidTouchState touches[16]; };
struct PadState { int unused; };

void padConfigureInput(u32 maxPlayers, u32 styleSet);
void padInitializeDefault(PadState* pad);
void padUpdate(PadState* pad);
u64 padGetButtonsDown(const PadState* pad);
u64 padGetButtons(const PadState* pad);
HidAnalogStickState padGetStickPos(const PadState* pad, unsigned i);
size_t hidGetTouchScreenStates(HidTouchScreenState* states, size_t count);

enum AppletType { AppletType_None = -2, AppletType_Default = -1, AppletType_Application = 0, AppletType_SystemApplet = 1,
                  AppletType_LibraryApplet = 2, AppletType_OverlayApplet = 3, AppletType_SystemApplication = 4 };
bool appletMainLoop(void);
Result appletLockExit(void);
Result appletUnlockExit(void);
AppletType appletGetAppletType(void);

struct AccountUid { u64 uid[2]; };
enum AccountServiceType { AccountServiceType_Application, AccountServiceType_System, AccountServiceType_Administrator };
static inline bool accountUidIsValid(const AccountUid* uid) { return uid->uid[0] != 0 || uid->uid[1] != 0; }
Result accountInitialize(AccountServiceType type);
void accountExit(void);
Result accountGetLastOpenedUser(AccountUid* uid);
Result accountGetPreselectedUser(AccountUid* uid);
struct PselUserSelectionSettings { u8 raw[0xA0]; };
Result pselShowUserSelector(AccountUid* uid, const PselUserSelectionSettings* settings);

enum { FsSaveDataSpaceId_User = 1 };
enum { FsSaveDataType_Account = 1 };
struct FsSaveDataInfoReader { int position; };
struct FsSaveDataInfo { u64 save_data_id; u8 save_data_space_id; u8 save_data_type; u8 pad[6]; AccountUid uid;
                        u64 system_save_data_id; u64 application_id; u64 size; u16 save_data_index; u8 save_data_rank; u8 unused[0x25]; };
Result fsOpenSaveDataInfoReader(FsSaveDataInfoReader* reader, int spaceId);
Result fsSaveDataInfoReaderRead(FsSaveDataInfoReader* reader, FsSaveDataInfo* buf, size_t max, s64* total);
void fsSaveDataInfoReaderClose(FsSaveDataInfoReader* reader);
Result fsdevMountSaveDataReadOnly(const char* name, u64 applicationId, AccountUid uid);
int fsdevUnmountDevice(const char* name);

Result romfsInit(void);
Result romfsExit(void);
Result socketInitializeDefault(void);
void socketExit(void);
int nxlinkStdio(void);

struct NWindow { int unused; };
NWindow* nwindowGetDefault(void);
