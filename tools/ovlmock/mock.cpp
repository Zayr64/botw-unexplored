// Host side of the overlay mock: a software renderer for the Tesla drawing calls, a scripted loop,
// a fake cheat service over a game memory built from a save file, and PNG output.
//
//   OVLMOCK_SCRIPT     wait N | press KEY | hold KEY N | pos X Y Z | setflag HASH | shot NAME
//   OVLMOCK_OUT        output folder
//   OVLMOCK_FONT       a .ttf (the console uses Nintendo's own font; OVLMOCK_XSCALE stretches this one to match)
//   OVLMOCK_BACKDROP   1280x720 image under the overlay
//   OVLMOCK_LIVE_SAVE  game_data.sav with the state of the "running game" (unset: no game)
//   OVLMOCK_LIVE_BREAK 1: the game's flag objects look like something else
#include <tesla.hpp>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <png.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

#include "FakeGameMemory.h"
#include "LiveGameData.h"
#include "dmntcht.h"

namespace
{
    constexpr int FbWidth = 448, FbHeight = 720;
    std::vector<float> g_fb(FbWidth * FbHeight * 4, 0.0f); // straight alpha, 0..1
    int g_layerX = 0;

    FT_Library g_ft = nullptr;
    FT_Face g_face = nullptr;
    float g_xScale = 1.0f;

    struct Step
    {
        u64 down = 0, held = 0;
        std::string shot;
        bool hasPos = false;
        float pos[3] = {};
        u32 setFlag = 0;
    };
    std::vector<Step> g_steps;
    long g_frame = 0;
    u64 g_held = 0;
    bool g_closed = false;
    std::string g_nextArgs;
    tsl::Overlay* g_overlay = nullptr;
    FakeGame* g_game = nullptr;

    bool EnvOn(const char* name)
    {
        const char* value = getenv(name);
        return value && value[0] == '1';
    }

    void Blend(int x, int y, const tsl::Color& color, float coverage)
    {
        if (x < 0 || y < 0 || x >= FbWidth || y >= FbHeight)
            return;
        const float alpha = color.a / 15.0f * coverage;
        float* pixel = &g_fb[(y * FbWidth + x) * 4];
        const float source[3] = {color.r / 15.0f, color.g / 15.0f, color.b / 15.0f};
        const float outAlpha = alpha + pixel[3] * (1.0f - alpha);
        if (outAlpha <= 0.0f)
            return;
        for (int c = 0; c < 3; c++)
            pixel[c] = (source[c] * alpha + pixel[c] * pixel[3] * (1.0f - alpha)) / outAlpha;
        pixel[3] = outAlpha;
    }

    u64 KeyByName(const std::string& name)
    {
        static const std::map<std::string, u64> names = {
            {"A", KEY_A}, {"B", KEY_B}, {"X", KEY_X}, {"Y", KEY_Y}, {"ZL", KEY_ZL}, {"ZR", KEY_ZR}, {"L", KEY_L}, {"R", KEY_R},
            {"UP", KEY_UP}, {"DOWN", KEY_DOWN}, {"LEFT", KEY_LEFT}, {"RIGHT", KEY_RIGHT}, {"MINUS", KEY_MINUS}, {"PLUS", KEY_PLUS},
            {"COMBO", KEY_ZL | KEY_ZR | KEY_DOWN}};
        const auto it = names.find(name);
        if (it == names.end())
        {
            fprintf(stderr, "ovlmock: unknown key %s\n", name.c_str());
            exit(2);
        }
        return it->second;
    }

    void LoadScript()
    {
        const char* path = getenv("OVLMOCK_SCRIPT");
        std::ifstream file(path ? path : "");
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
                g_steps.insert(g_steps.end(), n, Step());
            }
            else if (command == "press")
            {
                Step step;
                std::string name;
                in >> name;
                step.down = step.held = KeyByName(name);
                g_steps.push_back(step);
                g_steps.push_back(Step());
            }
            else if (command == "hold")
            {
                std::string name;
                int n = 1;
                in >> name >> n;
                for (int i = 0; i < n; i++)
                {
                    Step step;
                    step.held = KeyByName(name);
                    step.down = i == 0 ? step.held : 0;
                    g_steps.push_back(step);
                }
                g_steps.push_back(Step());
            }
            else if (command == "pos")
            {
                Step step;
                step.hasPos = true;
                in >> step.pos[0] >> step.pos[1] >> step.pos[2];
                g_steps.push_back(step);
            }
            else if (command == "setflag")
            {
                Step step;
                in >> step.setFlag;
                g_steps.push_back(step);
            }
            else if (command == "shot")
            {
                Step step;
                in >> step.shot;
                g_steps.push_back(step);
            }
        }
    }

    FakeGame* Game()
    {
        if (g_game)
            return g_game;
        const char* path = getenv("OVLMOCK_LIVE_SAVE");
        if (!path)
            return nullptr;
        std::ifstream file(path, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::map<uint32_t, std::vector<uint32_t>> values;
        for (size_t offset = 12; offset + 8 <= bytes.size(); offset += 8)
        {
            uint32_t hash, value;
            std::memcpy(&hash, bytes.data() + offset, 4);
            std::memcpy(&value, bytes.data() + offset + 4, 4);
            values[hash].push_back(value);
        }
        std::vector<FakeFlag> flags;
        for (const auto& entry : values)
        {
            FakeFlag flag = {LiveGameData::FlagKind::Bool, entry.first, {}};
            const std::vector<uint32_t>& list = entry.second;
            if (list.size() == 1 && list[0] <= 1)
                flag.value[0] = static_cast<uint8_t>(list[0] | 0x20);
            else if (list.size() == 1)
            {
                flag.kind = LiveGameData::FlagKind::S32;
                std::memcpy(flag.value, &list[0], 4);
            }
            else if (list.size() == 8)
            {
                flag.kind = LiveGameData::FlagKind::String32;
                std::memcpy(flag.value, list.data(), 32);
            }
            else if (list.size() == 3)
            {
                flag.kind = LiveGameData::FlagKind::Vector3f;
                std::memcpy(flag.value, list.data(), 12);
            }
            else
                continue;
            flags.push_back(flag);
        }
        g_game = new FakeGame();
        g_game->Build(LiveGameData::Botw160(), flags, 4096, EnvOn("OVLMOCK_LIVE_BREAK") ? 0x40 : 0);
        return g_game;
    }

    // Finds a flag through the same scan the overlay uses and sets it in the fake memory.
    struct FlagSetter : LiveGameData::Visitor
    {
        u32 wanted = 0;
        void OnFlag(LiveGameData::FlagKind kind, uint32_t hash, const uint8_t* value, uint64_t address) override
        {
            if (hash != wanted)
                return;
            if (kind == LiveGameData::FlagKind::Bool)
                g_game->memory.Put<uint8_t>(address, static_cast<uint8_t>(value[0] | 1));
            else if (kind == LiveGameData::FlagKind::S32)
                g_game->memory.Put<int32_t>(address, 1);
        }
    };

    void SavePng(const std::string& name)
    {
        const int width = 1280, height = 720;
        std::vector<unsigned char> image(width * height * 3, 40);
        if (const char* backdrop = getenv("OVLMOCK_BACKDROP"))
        {
            int w = 0, h = 0, channels = 0;
            if (unsigned char* data = stbi_load(backdrop, &w, &h, &channels, 3))
            {
                for (int y = 0; y < height && y < h; y++)
                    for (int x = 0; x < width && x < w; x++)
                        std::memcpy(&image[(y * width + x) * 3], &data[(y * w + x) * 3], 3);
                stbi_image_free(data);
            }
        }
        const int offset = g_layerX * 1280 / 1920;
        for (int y = 0; y < FbHeight; y++)
            for (int x = 0; x < FbWidth; x++)
            {
                const float* pixel = &g_fb[(y * FbWidth + x) * 4];
                const int sx = x + offset;
                if (sx < 0 || sx >= width || pixel[3] <= 0.0f)
                    continue;
                for (int c = 0; c < 3; c++)
                {
                    unsigned char& out = image[(y * width + sx) * 3 + c];
                    out = static_cast<unsigned char>(pixel[c] * 255.0f * pixel[3] + out * (1.0f - pixel[3]) + 0.5f);
                }
            }

        const char* dir = getenv("OVLMOCK_OUT");
        const std::string path = std::string(dir ? dir : ".") + "/" + name + ".png";
        FILE* file = fopen(path.c_str(), "wb");
        if (!file)
            return;
        png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
        png_infop info = png_create_info_struct(png);
        png_init_io(png, file);
        png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                     PNG_FILTER_TYPE_DEFAULT);
        png_write_info(png, info);
        for (int y = 0; y < height; y++)
            png_write_row(png, &image[y * width * 3]);
        png_write_end(png, nullptr);
        png_destroy_write_struct(&png, &info);
        fclose(file);
        printf("ovlmock: wrote %s\n", path.c_str());
    }
}

// ---- libnx ----
void padInitializeAny(PadState*) {}
void padUpdate(PadState*) {}
u64 padGetButtons(const PadState*) { return g_held; }
Result smRegisterService(Handle*, SmServiceName, bool, s32) { return 1; } // "already registered": the service exists
Result smUnregisterService(SmServiceName) { return 0; }
Result svcCloseHandle(Handle) { return 0; }
Result pmdmntGetApplicationProcessId(u64* pid)
{
    *pid = 1;
    return Game() ? 0 : 1;
}
u64 armGetSystemTick(void) { return static_cast<u64>(g_frame) * 16666667ULL; }
u64 armTicksToNs(u64 ticks) { return ticks; }

// ---- dmnt:cht ----
Result dmntchtInitialize(void) { return 0; }
void dmntchtExit(void) {}
Result dmntchtForceOpenCheatProcess(void) { return 0; }
Result dmntchtGetCheatProcessMetadata(DmntCheatProcessMetadata* out)
{
    FakeGame* game = Game();
    if (!game)
        return 1;
    std::memset(out, 0, sizeof(*out));
    out->title_id = 0x01007EF00011E000ULL;
    out->main_nso_extents.base = game->mainBase;
    std::memcpy(out->main_nso_build_id, LiveGameData::BotwBuildId160, 8);
    return 0;
}
Result dmntchtReadCheatProcessMemory(u64 address, void* buffer, size_t size)
{
    FakeGame* game = Game();
    return game && game->memory.Read(address, buffer, size) ? 0 : 1;
}

// ---- Tesla ----
tsl::gfx::Renderer& tsl::gfx::Renderer::get()
{
    static Renderer renderer;
    return renderer;
}

void tsl::gfx::Renderer::setLayerPos(u32 x, u32) { g_layerX = static_cast<int>(x); }

void tsl::gfx::Renderer::drawRect(s32 x, s32 y, s32 w, s32 h, const Color& color)
{
    for (s32 py = y; py < y + h; py++)
        for (s32 px = x; px < x + w; px++)
            Blend(px, py, color, 1.0f);
}

void tsl::gfx::Renderer::drawCircle(s32 centerX, s32 centerY, u16 radius, bool, const Color& color)
{
    const int r = radius;
    for (int py = -r - 1; py <= r + 1; py++)
        for (int px = -r - 1; px <= r + 1; px++)
        {
            const float distance = std::sqrt(static_cast<float>(px * px + py * py));
            const float coverage = std::fmax(0.0f, std::fmin(1.0f, r + 0.5f - distance));
            if (coverage > 0.0f)
                Blend(centerX + px, centerY + py, color, coverage);
        }
}

std::pair<s32, s32> tsl::gfx::Renderer::drawString(const std::string& text, bool, s32 x, s32 y, u32 fontSize,
                                                   const Color& color, ssize_t, bool draw)
{
    if (!g_face)
        return {0, 0};
    FT_Set_Char_Size(g_face, static_cast<FT_F26Dot6>(fontSize * g_xScale * 64.0f), static_cast<FT_F26Dot6>(fontSize * 64), 72, 72);
    float pen = static_cast<float>(x);
    for (size_t i = 0; i < text.size(); i++)
    {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (FT_Load_Char(g_face, c, FT_LOAD_RENDER))
            continue;
        const FT_GlyphSlot glyph = g_face->glyph;
        if (draw)
            for (unsigned row = 0; row < glyph->bitmap.rows; row++)
                for (unsigned col = 0; col < glyph->bitmap.width; col++)
                {
                    const float coverage = glyph->bitmap.buffer[row * glyph->bitmap.pitch + col] / 255.0f;
                    if (coverage > 0.0f)
                        Blend(static_cast<int>(pen) + glyph->bitmap_left + col, y - glyph->bitmap_top + row, color, coverage);
                }
        pen += glyph->advance.x / 64.0f;
    }
    return {static_cast<s32>(pen - x + 0.5f), static_cast<s32>(fontSize)};
}

void tsl::elm::OverlayFrame::draw(gfx::Renderer* renderer)
{
    renderer->drawRect(0, 0, FbWidth, FbHeight, Color(0, 0, 0, 15));
    renderer->drawString(m_title, false, 20, 52, 32, Color(15, 15, 15, 15));
    renderer->drawString(m_subtitle, false, 20, 75, 15, Color(11, 11, 11, 15));
    renderer->drawRect(15, 647, FbWidth - 30, 1, Color(15, 15, 15, 15));
    renderer->drawString("(B) Back     (A) OK", false, 30, 693, 23, Color(15, 15, 15, 15));
    if (m_content)
    {
        m_content->setBoundaries(35, 97, FbWidth - 85, FbHeight - 73 - 105);
        m_content->draw(renderer);
    }
}

void tsl::elm::HeaderOverlayFrame::draw(gfx::Renderer* renderer)
{
    if (m_content)
    {
        m_content->setBoundaries(35, 0, FbWidth - 85, FbHeight - 73 - 105);
        m_content->draw(renderer);
    }
}

tsl::Overlay* tsl::Overlay::get() { return g_overlay; }
void tsl::Overlay::close() { g_closed = true; }
void tsl::setNextOverlay(const std::string&, const std::string& args) { g_nextArgs = args; }
void tsl::hlp::requestForeground(bool) {}

int tsl::MockLoop(Overlay* overlay)
{
    g_overlay = overlay;
    LoadScript();
    FT_Init_FreeType(&g_ft);
    const char* font = getenv("OVLMOCK_FONT");
    if (!font || FT_New_Face(g_ft, font, 0, &g_face))
        fprintf(stderr, "ovlmock: no font (OVLMOCK_FONT)\n");
    if (const char* scale = getenv("OVLMOCK_XSCALE"))
        g_xScale = static_cast<float>(atof(scale));
    printf("ovlmock: calibration width %d (the console draws this in 274 px)\n",
           gfx::Renderer::get().getTextDimensions("Pick up the rock at the top of the rock struc...", false, 13).first);

    overlay->initServices();
    overlay->onShow();
    std::unique_ptr<Gui> gui = overlay->loadInitialGui();
    std::unique_ptr<elm::Element> root(gui->createUI());
    for (const Step& step : g_steps)
    {
        g_frame++;
        g_held = step.held;
        if (step.hasPos && Game())
            Game()->SetPlayerPosition(LiveGameData::Botw160(), step.pos[0], step.pos[1], step.pos[2]);
        if (step.setFlag != 0 && Game())
        {
            FlagSetter setter;
            setter.wanted = step.setFlag;
            LiveGameData::Report report;
            LiveGameData::Scan(Game()->memory, Game()->mainBase, LiveGameData::Botw160(), setter, report);
        }
        gui->update();
        if (!g_closed)
            gui->handleInput(step.down, step.held, HidTouchState(), HidAnalogStickState(), HidAnalogStickState());
        std::fill(g_fb.begin(), g_fb.end(), 0.0f);
        root->draw(&gfx::Renderer::get());
        if (!step.shot.empty())
            SavePng(step.shot);
        if (g_closed)
            break;
    }
    overlay->exitServices();
    if (!g_nextArgs.empty())
        printf("ovlmock: next overlay args: %s\n", g_nextArgs.c_str());
    return 0;
}
