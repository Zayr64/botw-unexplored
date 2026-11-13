// Host side of the shim: scripted input, off-screen EGL, PNG screenshots.
//
// Script (HOSTVIEW_SCRIPT), one command per line:
//   wait N                 N frames without input
//   press A [B ...]        buttons pressed for one frame
//   hold ZR N              button held for N frames (pressed on the first)
//   lstick X Y N           left stick at X,Y (-1..1) for N frames
//   rstick X Y N           right stick at X,Y for N frames
//   touch X Y N            finger at screen pixel X,Y for N frames, then released for one
//   shot NAME              one settle frame, saved as <HOSTVIEW_OUT>/NAME.png
#include <switch.h>

#include "FreeMemory.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <glad/glad.h>
#include <png.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    struct Frame
    {
        u64 down = 0;
        u64 held = 0;
        float lx = 0, ly = 0, rx = 0, ry = 0;
        bool touching = false;
        int tx = 0, ty = 0;
        std::string shot;
    };

    std::vector<Frame> g_frames;
    long g_index = -1;
    bool g_loaded = false;
    const Frame g_idle;

    u64 ButtonByName(const std::string& name)
    {
        static const std::map<std::string, u64> names = {
            {"A", HidNpadButton_A}, {"B", HidNpadButton_B}, {"X", HidNpadButton_X}, {"Y", HidNpadButton_Y},
            {"LS", HidNpadButton_StickL}, {"RS", HidNpadButton_StickR}, {"L", HidNpadButton_L}, {"R", HidNpadButton_R},
            {"ZL", HidNpadButton_ZL}, {"ZR", HidNpadButton_ZR}, {"PLUS", HidNpadButton_Plus}, {"MINUS", HidNpadButton_Minus},
            {"LEFT", HidNpadButton_Left}, {"UP", HidNpadButton_Up}, {"RIGHT", HidNpadButton_Right}, {"DOWN", HidNpadButton_Down}};
        const auto it = names.find(name);
        if (it == names.end())
        {
            fprintf(stderr, "hostview: unknown button %s\n", name.c_str());
            exit(2);
        }
        return it->second;
    }

    void LoadScript()
    {
        if (g_loaded)
            return;
        g_loaded = true;
        const char* path = getenv("HOSTVIEW_SCRIPT");
        if (!path)
        {
            g_frames.resize(3);
            g_frames.back().shot = "frame";
            return;
        }
        std::ifstream file(path);
        if (!file.is_open())
        {
            fprintf(stderr, "hostview: cannot open script %s\n", path);
            exit(2);
        }
        std::string line;
        while (std::getline(file, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            std::istringstream in(line);
            std::string command;
            if (!(in >> command) || command[0] == '#')
                continue;
            if (command == "wait")
            {
                int n = 1;
                in >> n;
                g_frames.insert(g_frames.end(), n, Frame());
            }
            else if (command == "press")
            {
                Frame frame;
                std::string name;
                while (in >> name)
                    frame.down |= ButtonByName(name);
                frame.held = frame.down;
                g_frames.push_back(frame);
                g_frames.push_back(Frame());
            }
            else if (command == "hold")
            {
                std::string name;
                int n = 1;
                in >> name >> n;
                for (int i = 0; i < n; i++)
                {
                    Frame frame;
                    frame.held = ButtonByName(name);
                    frame.down = i == 0 ? frame.held : 0;
                    g_frames.push_back(frame);
                }
                g_frames.push_back(Frame());
            }
            else if (command == "lstick" || command == "rstick")
            {
                float x = 0, y = 0;
                int n = 1;
                in >> x >> y >> n;
                for (int i = 0; i < n; i++)
                {
                    Frame frame;
                    (command == "lstick" ? frame.lx : frame.rx) = x;
                    (command == "lstick" ? frame.ly : frame.ry) = y;
                    g_frames.push_back(frame);
                }
            }
            else if (command == "touch")
            {
                int x = 0, y = 0, n = 1;
                in >> x >> y >> n;
                for (int i = 0; i < n; i++)
                {
                    Frame frame;
                    frame.touching = true;
                    frame.tx = x;
                    frame.ty = y;
                    g_frames.push_back(frame);
                }
                g_frames.push_back(Frame());
            }
            else if (command == "shot")
            {
                Frame frame;
                in >> frame.shot;
                g_frames.push_back(frame);
            }
            else
            {
                fprintf(stderr, "hostview: unknown command %s\n", command.c_str());
                exit(2);
            }
        }
    }

    const Frame& Current()
    {
        if (g_index < 0 || g_index >= static_cast<long>(g_frames.size()))
            return g_idle;
        return g_frames[g_index];
    }

    void SavePng(const std::string& name)
    {
        const int width = 1280, height = 720;
        std::vector<unsigned char> pixels(width * height * 4);
        glFinish();
        glReadBuffer(GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        const char* out = getenv("HOSTVIEW_OUT");
        const std::string path = std::string(out ? out : ".") + "/" + name + ".png";
        FILE* file = fopen(path.c_str(), "wb");
        if (!file)
        {
            fprintf(stderr, "hostview: cannot write %s\n", path.c_str());
            return;
        }
        png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
        png_infop info = png_create_info_struct(png);
        png_init_io(png, file);
        png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                     PNG_FILTER_TYPE_DEFAULT);
        png_write_info(png, info);
        std::vector<unsigned char> row(width * 3);
        for (int y = height - 1; y >= 0; y--)
        {
            for (int x = 0; x < width; x++)
            {
                row[x * 3 + 0] = pixels[(y * width + x) * 4 + 0];
                row[x * 3 + 1] = pixels[(y * width + x) * 4 + 1];
                row[x * 3 + 2] = pixels[(y * width + x) * 4 + 2];
            }
            png_write_row(png, row.data());
        }
        png_write_end(png, nullptr);
        png_destroy_write_struct(&png, &info);
        fclose(file);
        printf("hostview: wrote %s\n", path.c_str());
    }

    bool EnvOn(const char* name)
    {
        const char* value = getenv(name);
        return value && value[0] == '1';
    }
}

void padConfigureInput(u32, u32) {}
void padInitializeDefault(PadState*) { LoadScript(); }
void padUpdate(PadState*) { g_index++; }
u64 padGetButtonsDown(const PadState*) { return Current().down; }
u64 padGetButtons(const PadState*) { return Current().held; }

HidAnalogStickState padGetStickPos(const PadState*, unsigned i)
{
    const Frame& frame = Current();
    HidAnalogStickState state;
    state.x = static_cast<s32>((i == 0 ? frame.lx : frame.rx) * JOYSTICK_MAX);
    state.y = static_cast<s32>((i == 0 ? frame.ly : frame.ry) * JOYSTICK_MAX);
    return state;
}

size_t hidGetTouchScreenStates(HidTouchScreenState* states, size_t count)
{
    if (count == 0)
        return 0;
    const Frame& frame = Current();
    memset(states, 0, sizeof(*states));
    states->count = frame.touching ? 1 : 0;
    states->touches[0].x = frame.tx;
    states->touches[0].y = frame.ty;
    return 1;
}

bool appletMainLoop(void)
{
    LoadScript();
    return g_index + 1 < static_cast<long>(g_frames.size());
}

Result appletLockExit(void) { return 0; }
Result appletUnlockExit(void) { return 0; }
AppletType appletGetAppletType(void) { return EnvOn("HOSTVIEW_APPLET") ? AppletType_LibraryApplet : AppletType_Application; }

// The memory the app believes it has left: HOSTVIEW_FREE_MB megabytes, or unknown without the variable.
uint64_t FreeMemory::Bytes()
{
    const char* megabytes = getenv("HOSTVIEW_FREE_MB");
    if (!megabytes)
        return FreeMemory::Unknown;
    return static_cast<uint64_t>(atoll(megabytes)) * 1024 * 1024;
}

Result accountInitialize(AccountServiceType) { return 0; }
void accountExit(void) {}
Result accountGetLastOpenedUser(AccountUid* uid)
{
    uid->uid[0] = 4369;
    uid->uid[1] = 8738;
    return 0;
}
Result accountGetPreselectedUser(AccountUid* uid) { return accountGetLastOpenedUser(uid); }
Result pselShowUserSelector(AccountUid* uid, const PselUserSelectionSettings*) { return accountGetLastOpenedUser(uid); }

Result fsOpenSaveDataInfoReader(FsSaveDataInfoReader* reader, int)
{
    reader->position = 0;
    return 0;
}

Result fsSaveDataInfoReaderRead(FsSaveDataInfoReader* reader, FsSaveDataInfo* buf, size_t, s64* total)
{
    if (reader->position++ > 0 || EnvOn("HOSTVIEW_NO_SAVE"))
    {
        *total = 0;
        return 0;
    }
    memset(buf, 0, sizeof(*buf));
    buf->save_data_type = FsSaveDataType_Account;
    buf->application_id = 0x01007ef00011e000ULL;
    accountGetLastOpenedUser(&buf->uid);
    *total = 1;
    return 0;
}

void fsSaveDataInfoReaderClose(FsSaveDataInfoReader*) {}

Result fsdevMountSaveDataReadOnly(const char*, u64, AccountUid)
{
    // The save is "mounted" when the working directory has a "save:" folder and the game is not "running".
    if (EnvOn("HOSTVIEW_GAME_RUNNING"))
        return 0x7D402; // ResultTargetLocked
    struct stat info;
    return stat("save:", &info) == 0 ? 0 : 0x7D402;
}

int fsdevUnmountDevice(const char*) { return 0; }
Result romfsInit(void) { return 0; }
Result romfsExit(void) { return 0; }
Result socketInitializeDefault(void) { return 1; }
void socketExit(void) {}
int nxlinkStdio(void) { return -1; }
NWindow* nwindowGetDefault(void)
{
    static NWindow window;
    return &window;
}

bool HostInitEgl(EGLDisplay* display, EGLContext* context, EGLSurface* surface)
{
    *display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    if (!eglInitialize(*display, nullptr, nullptr))
        return false;
    eglBindAPI(EGL_OPENGL_API);
    const EGLint configAttributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                                       EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                       EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE};
    EGLConfig config;
    EGLint count = 0;
    eglChooseConfig(*display, configAttributes, &config, 1, &count);
    if (count == 0)
        return false;
    const EGLint surfaceAttributes[] = {EGL_WIDTH, 1280, EGL_HEIGHT, 720, EGL_NONE};
    *surface = eglCreatePbufferSurface(*display, config, surfaceAttributes);
    const EGLint contextAttributes[] = {EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
                                        EGL_CONTEXT_MAJOR_VERSION_KHR, 4, EGL_CONTEXT_MINOR_VERSION_KHR, 3, EGL_NONE};
    *context = eglCreateContext(*display, config, EGL_NO_CONTEXT, contextAttributes);
    if (!*context || !*surface)
        return false;
    return eglMakeCurrent(*display, *surface, *surface, *context) == EGL_TRUE;
}

void HostDeinitEgl(EGLDisplay display, EGLContext context, EGLSurface surface)
{
    if (!display)
        return;
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (context)
        eglDestroyContext(display, context);
    if (surface)
        eglDestroySurface(display, surface);
    eglTerminate(display);
}

void HostSwapBuffers(void)
{
    const Frame& frame = Current();
    if (!frame.shot.empty())
        SavePng(frame.shot);
}
