#include "MapHd.h"

#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include <glad/glad.h>
#include <stb/stb_image.h>
#include <switch.h>

#include "Dds.h"
#include "Graphics/BasicVertices.h"
#include "Graphics/CompressedTexture.h"
#include "Graphics/Mesh.hpp"
#include "Graphics/Quad.h"
#include "Graphics/Texture2D.h"
#include "Graphics/Vertex.h"
#include "FreeMemory.h"
#include "Log.h"
#include "MapHdGrid.h"

namespace
{
    const char* Directory = "sdmc:/switch/botw-unexplored/map-hd/";
    // After finding no memory for a chunk, how many frames to wait before looking again.
    constexpr unsigned int MemoryCheckInterval = 120;

    enum class ChunkState
    {
        Empty,
        Loading,
        Loaded,
        Failed
    };

    struct Chunk
    {
        ChunkState state = ChunkState::Empty;
        Texture2D* texture = nullptr;
        Mesh<TextureVertex>* mesh = nullptr;
        unsigned int lastUsed = 0;
        int reduction = 1;      // of the texture loaded: 1 full size, 2 halved
        bool upgrading = false; // loaded halved, the full size being decoded (the halved one stays on screen)
        bool noUpgrade = false; // the full size could not be loaded: not tried again
        int kilobytes = 0;      // of video memory, while loaded
    };

    struct Decoded
    {
        int index = -1;
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        bool fromDecoder = true; // freed by stbi_image_free; false: a halved copy, freed by free
        int reduction = 1;
        std::vector<uint8_t> dds; // the BC1 file instead of pixels (zayr26)
    };

    void FreePixels(Decoded& decoded)
    {
        std::vector<uint8_t>().swap(decoded.dds);
        if (!decoded.pixels)
            return;
        if (decoded.fromDecoder)
            stbi_image_free(decoded.pixels);
        else
            std::free(decoded.pixels);
        decoded.pixels = nullptr;
    }

    bool s_Enabled = false;
    bool s_Active = false;
    bool s_Application = true;  // false when opened from the Album, with little memory
    int s_MaxLoaded = MapHdGrid::MaxLoadedApplication;
    int s_BaseReduction = 1;    // 2 from the Album: chunks are loaded halved, full size only zoomed in
    bool s_Compressed = false;  // the chunks are read from map-hd/*.dds (BC1), always at full size
    unsigned int s_NextMemoryCheck = 0;
    bool s_MemoryLogged = false;
    bool s_UpgradeLogged = false;
    unsigned int s_Frame = 0;
    int s_LoadedCount = 0;
    int s_FullCount = 0; // loaded at full size
    // KB of textures loaded (MapHdGrid::TextureKilobytes of each chunk) and how many may be: lowered to what is loaded
    // if a texture ever fails for lack of video memory.
    int s_TextureKb = 0;
    int s_TextureBudgetKb = MapHdGrid::TextureBudgetKb(true);
    bool s_BudgetLogged = false;
    bool s_VideoErrorLogged = false;
    // For the log at the end: the most chunks loaded at once, and the least memory free when one was asked for.
    int s_PeakLoaded = 0;
    int s_PeakFull = 0;
    int s_PeakTextureKb = 0;
    uint64_t s_LeastFree = FreeMemory::Unknown;
    Chunk s_Chunks[MapHdGrid::ChunkCount];
    std::vector<int> s_Visible;

    // Shared with the decoding thread
    std::thread s_Worker;
    std::mutex s_Mutex;
    std::condition_variable s_Wake;
    int s_Request = -1;
    int s_RequestReduction = 1;
    bool s_Stop = false;
    bool s_HasDecoded = false;
    Decoded s_Decoded;

    void Worker()
    {
        std::unique_lock<std::mutex> lock(s_Mutex);
        while (true)
        {
            s_Wake.wait(lock, [] { return s_Stop || s_Request >= 0; });
            if (s_Stop)
                break;
            const int index = s_Request;
            const int reduction = s_RequestReduction;
            lock.unlock();

            Decoded decoded;
            decoded.index = index;
            decoded.reduction = reduction;
            int channels = 0;
            const std::string path = std::string(Directory) + MapHdGrid::ChunkFileName(index);
            // The BC1 file as it is, when there is one; else the JPEG, decoded
            if (s_Compressed && CompressedTexture::ReadFile(Dds::SiblingPath(path), decoded.dds))
                decoded.reduction = 1;
            else
                decoded.pixels = stbi_load(path.c_str(), &decoded.width, &decoded.height, &channels, 3);
            if (decoded.pixels && reduction == 2)
            {
                // From the Album: the chunk at half its size, so that it takes a quarter of the memory.
                const int width = MapHdGrid::HalfSize(decoded.width), height = MapHdGrid::HalfSize(decoded.height);
                unsigned char* half = static_cast<unsigned char*>(std::malloc(static_cast<size_t>(width) * height * 3));
                if (half)
                    MapHdGrid::Halve(decoded.pixels, decoded.width, decoded.height, half);
                FreePixels(decoded);
                decoded.pixels = half;
                decoded.fromDecoder = false;
                decoded.width = width;
                decoded.height = height;
            }

            lock.lock();
            s_Decoded = std::move(decoded);
            s_HasDecoded = true;
            s_Request = -1;
        }
    }

    void Upload(Decoded& decoded)
    {
        Chunk& chunk = s_Chunks[decoded.index];
        Decoded& pixels = decoded;
        const bool compressed = !decoded.dds.empty();
        // A halved chunk evicted while its full size was being decoded: nothing to replace any more.
        if (chunk.state == ChunkState::Empty)
        {
            FreePixels(pixels);
            return;
        }
        const bool upgrade = chunk.state == ChunkState::Loaded;
        int width = MapHdGrid::ChunkWidthPixels(decoded.index), height = MapHdGrid::ChunkHeightPixels(decoded.index);
        if (decoded.reduction == 2)
        {
            width = MapHdGrid::HalfSize(width);
            height = MapHdGrid::HalfSize(height);
        }
        const std::string kind = compressed ? " (BC1)" : decoded.reduction == 1 ? " (full size)" : " (halved)";
        Dds::Info dds;
        const bool valid = compressed
            ? Dds::Parse(decoded.dds.data(), decoded.dds.size(), dds) && dds.width == width && dds.height == height
            : decoded.pixels && decoded.width == width && decoded.height == height;
        if (!valid)
        {
            Log("MAP HD: could not load", MapHdGrid::ChunkFileName(decoded.index) + kind);
            FreePixels(pixels);
            if (upgrade)
            {
                // The halved one stays.
                chunk.upgrading = false;
                chunk.noUpgrade = true;
            }
            else
                chunk.state = ChunkState::Failed;
            return;
        }

        Texture2D* texture = new Texture2D();
        texture->m_Width = width;
        texture->m_Height = height;
        texture->m_NrChannels = 3;
        glActiveTexture(GL_TEXTURE0);
        glGenTextures(1, &texture->m_Texture);
        glBindTexture(GL_TEXTURE_2D, texture->m_Texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        GLenum error = GL_NO_ERROR;
        if (compressed)
            error = CompressedTexture::Upload(decoded.dds.data(), dds); // every level is in the file
        else
        {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            while (glGetError() != GL_NO_ERROR) {} // only the errors of this texture are looked at below
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, decoded.pixels);
            error = glGetError();
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            if (error == GL_NO_ERROR)
            {
                glGenerateMipmap(GL_TEXTURE_2D);
                error = glGetError();
            }
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        FreePixels(pixels);
        const int kilobytes = MapHdGrid::TextureKilobytes(decoded.reduction, compressed);
        if (error != GL_NO_ERROR)
        {
            // Out of video memory, most likely: an incomplete texture would draw black over the map. It goes, the
            // chunk keeps what it had (halved, or the low-resolution map under it), and no more textures than now
            // are kept from here on.
            delete texture;
            s_TextureBudgetKb = std::max(s_TextureKb, kilobytes);
            if (!s_VideoErrorLogged)
            {
                Log("MAP HD: no video memory for " + MapHdGrid::ChunkFileName(decoded.index) + kind + ", GL error " +
                    std::to_string(error) + "; textures kept from now on: " + std::to_string(s_TextureBudgetKb / 1024) +
                    " MB, loaded: " +
                    std::to_string(s_LoadedCount) + " (" + std::to_string(s_FullCount) + " at full size)");
                s_VideoErrorLogged = true;
            }
            if (upgrade)
            {
                chunk.upgrading = false;
                chunk.noUpgrade = true;
            }
            else // asked for again when there is room; with nothing loaded there never will be
                chunk.state = s_LoadedCount > 0 ? ChunkState::Empty : ChunkState::Failed;
            return;
        }

        // Same corner order and texture coordinates as the low-resolution map, which is also loaded flipped.
        const MapHdGrid::Rect rect = MapHdGrid::ChunkRect(decoded.index);
        const glm::vec2 corners[4] = {
            {rect.left, rect.bottom}, {rect.right, rect.bottom}, {rect.right, rect.top}, {rect.left, rect.top}
        };
        Mesh<TextureVertex>* mesh = new Mesh<TextureVertex>();
        for (int i = 0; i < 4; ++i)
            mesh->AddVertex(TextureVertex(corners[i], BasicVertices::Quad::TextureCoordinates[i]));
        for (int i = 0; i < 6; ++i)
            mesh->AddIndex(BasicVertices::Quad::Indices[i]);
        mesh->m_Texture = texture;
        mesh->Update();

        if (upgrade)
        {
            // The full size takes the place of the halved one, which only goes now.
            delete chunk.mesh;
            delete chunk.texture;
            if (chunk.reduction == 1)
                --s_FullCount;
            s_TextureKb -= chunk.kilobytes;
        }
        else
            ++s_LoadedCount;
        chunk.texture = texture;
        chunk.mesh = mesh;
        chunk.reduction = decoded.reduction;
        if (chunk.reduction == 1)
            ++s_FullCount;
        chunk.kilobytes = kilobytes;
        s_TextureKb += kilobytes;
        chunk.upgrading = false;
        chunk.state = ChunkState::Loaded;
        chunk.lastUsed = s_Frame;
        s_PeakLoaded = std::max(s_PeakLoaded, s_LoadedCount);
        s_PeakFull = std::max(s_PeakFull, s_FullCount);
        s_PeakTextureKb = std::max(s_PeakTextureKb, s_TextureKb);
    }

    void Unload(int index)
    {
        Chunk& chunk = s_Chunks[index];
        delete chunk.mesh; // destroys its buffers
        chunk.mesh = nullptr;
        delete chunk.texture; // deletes the GL texture
        chunk.texture = nullptr;
        if (chunk.state == ChunkState::Loaded)
        {
            --s_LoadedCount;
            if (chunk.reduction == 1)
                --s_FullCount;
            s_TextureKb -= chunk.kilobytes;
        }
        chunk.kilobytes = 0;
        chunk.state = ChunkState::Empty;
        chunk.reduction = 1;
        chunk.upgrading = false; // a full size still being decoded is dropped when it arrives
    }

    // The loaded chunk used least recently and not in this frame, or -1.
    int EvictionCandidate()
    {
        std::vector<std::pair<int, unsigned int>> loaded;
        for (int index = 0; index < MapHdGrid::ChunkCount; ++index)
            if (s_Chunks[index].state == ChunkState::Loaded)
                loaded.push_back({index, s_Chunks[index].lastUsed});
        return MapHdGrid::PickEviction(loaded, s_Frame);
    }

    // Whether `kilobytes` more of texture fit in the budget, after letting go of chunks off screen if needed. With
    // `fullOnScreen` (zoomed out again, the chunks wanted halved) a chunk on screen at full size may go too, the
    // farthest from the center first: it comes back halved.
    bool MakeTextureRoom(int kilobytes, bool fullOnScreen)
    {
        while (s_TextureKb + kilobytes > s_TextureBudgetKb)
        {
            int victim = EvictionCandidate();
            for (auto it = s_Visible.rbegin(); victim < 0 && fullOnScreen && it != s_Visible.rend(); ++it)
                if (s_Chunks[*it].state == ChunkState::Loaded && s_Chunks[*it].reduction == 1)
                    victim = *it;
            if (victim < 0)
            {
                if (!s_BudgetLogged)
                {
                    Log("MAP HD: textures at their limit (" + std::to_string(s_TextureKb / 1024) + " of " +
                        std::to_string(s_TextureBudgetKb / 1024) + " MB): the chunks still missing wait for some to go "
                        "off screen, the halved ones on screen stay halved");
                    s_BudgetLogged = true;
                }
                return false;
            }
            Unload(victim);
        }
        return true;
    }

    std::string MegabytesText(uint64_t bytes)
    {
        return bytes == FreeMemory::Unknown ? std::string("unknown") : std::to_string(bytes >> 20) + " MB";
    }

    // Whether one more chunk may be loaded now. From the Album that depends on the memory left: when it is
    // short a chunk that is off screen goes first, and the answer is no until a later frame.
    bool HasRoomForChunk()
    {
        if (s_Application)
            return true;
        if (s_Frame < s_NextMemoryCheck)
            return false;
        const uint64_t freeBytes = FreeMemory::Bytes();
        if (freeBytes != FreeMemory::Unknown && (s_LeastFree == FreeMemory::Unknown || freeBytes < s_LeastFree))
            s_LeastFree = freeBytes;
        if (MapHdGrid::HasMemoryForChunk(freeBytes))
            return true;

        const int victim = EvictionCandidate();
        if (victim >= 0)
        {
            Unload(victim);
            return false; // looked at again in the next frame, with that memory back
        }
        if (!s_MemoryLogged)
        {
            Log("MAP HD: no memory for more chunks, free: " + MegabytesText(freeBytes) + ", loaded: " +
                std::to_string(s_LoadedCount));
            s_MemoryLogged = true;
        }
        s_NextMemoryCheck = s_Frame + MemoryCheckInterval;
        return false;
    }
}

void MapHd::Init()
{
    const AppletType appletType = appletGetAppletType();
    s_Application = appletType == AppletType_Application || appletType == AppletType_SystemApplication;
    const uint64_t freeBytes = FreeMemory::Bytes();
    Log(std::string("MEMORY: ") + (s_Application ? "application" : "applet (Album)") + ", free: " + MegabytesText(freeBytes));
    std::ifstream info(std::string(Directory) + "map-hd.txt");
    if (!info.is_open() || !MapHdGrid::IsValidInfo(info))
    {
        Log("MAP HD: off (no valid map-hd/map-hd.txt)");
        return;
    }

    stbi_set_flip_vertically_on_load(true); // like every other texture in the app
    // The BC1 chunks (zayr26, made by tools/map_dds.cpp) when the SD card has them and the GL driver takes BC1: an
    // eighth of the video memory, so every chunk at full size from the Album too. Without them, the JPEG ones.
    const bool hasDds = access((std::string(Directory) + Dds::SiblingPath(MapHdGrid::ChunkFileName(0))).c_str(), R_OK) == 0;
    s_Compressed = hasDds && CompressedTexture::Bc1Supported();
    if (hasDds && !s_Compressed)
        Log("MAP HD: the GL driver does not take BC1: map-hd/*.dds left aside, the JPEG chunks are used");
    s_BaseReduction = MapHdGrid::Reduction(s_Application, s_Compressed);
    s_Stop = false;
    s_Worker = std::thread(Worker);
    s_Enabled = true;
    s_MaxLoaded = MapHdGrid::MaxLoadedChunks(s_Application, s_Compressed);
    s_TextureBudgetKb = MapHdGrid::TextureBudgetKb(s_Application);
    s_NextMemoryCheck = 0;
    s_MemoryLogged = false;
    s_UpgradeLogged = false;
    s_BudgetLogged = false;
    s_VideoErrorLogged = false;
    Log("MAP HD: on, up to " + std::to_string(s_MaxLoaded) + " chunks" +
        (s_Compressed ? std::string(" in BC1 (map-hd/*.dds), at full size")
         : s_Application ? std::string(" (JPEG)")
         : std::string(" (JPEG) at half size (the map at 2x), at full size when zoomed in, each only while memory allows")) +
        ", up to " + std::to_string(s_TextureBudgetKb / 1024) + " MB of textures");
}

void MapHd::Update(const glm::vec2& cameraPosition, float zoom, float halfWidth, float halfHeight)
{
    if (!s_Enabled)
        return;
    ++s_Frame;

    Decoded decoded;
    bool hasDecoded = false;
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        if (s_HasDecoded)
        {
            decoded = std::move(s_Decoded);
            s_Decoded = Decoded();
            s_HasDecoded = false;
            hasDecoded = true;
        }
    }
    if (hasDecoded)
        Upload(decoded); // one upload per frame at most

    s_Active = MapHdGrid::IsUsableZoom(zoom);
    if (!s_Active)
    {
        s_Visible.clear();
        return; // the loaded chunks stay for when the map is zoomed in again
    }

    s_Visible = MapHdGrid::VisibleChunks(cameraPosition.x, cameraPosition.y, halfWidth, halfHeight);
    for (int index : s_Visible)
        if (s_Chunks[index].state == ChunkState::Loaded)
            s_Chunks[index].lastUsed = s_Frame;

    // The nearest chunk on screen that is still missing, if the decoder is idle and there is room for it. When every
    // chunk on screen is there and the map is zoomed in (from the Album), the nearest one still halved is loaded
    // again at full size, the halved one staying on screen until then.
    int wanted = -1;
    bool upgrade = false;
    for (int index : s_Visible)
    {
        if (s_Chunks[index].state == ChunkState::Empty)
        {
            wanted = index;
            break;
        }
    }
    if (wanted < 0 && MapHdGrid::WantedReduction(s_Application, zoom) < s_BaseReduction)
    {
        for (int index : s_Visible)
        {
            const Chunk& chunk = s_Chunks[index];
            if (chunk.state == ChunkState::Loaded && chunk.reduction != 1 && !chunk.upgrading && !chunk.noUpgrade)
            {
                wanted = index;
                upgrade = true;
                break;
            }
        }
    }
    bool idle = false;
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        idle = s_Request < 0 && !s_HasDecoded;
    }
    // An upgrade holds both textures for a moment: the full size is weighed whole, the halved one is freed after.
    const int kilobytes = MapHdGrid::TextureKilobytes(upgrade ? 1 : s_BaseReduction, s_Compressed && !upgrade);
    const bool zoomedOut = !s_Compressed && MapHdGrid::WantedReduction(s_Application, zoom) > 1;
    if (wanted >= 0 && idle && MakeTextureRoom(kilobytes, !upgrade && zoomedOut) && HasRoomForChunk())
    {
        if (upgrade && !s_UpgradeLogged)
        {
            Log("MAP HD: zoomed in, loading the chunks on screen at full size while memory allows");
            s_UpgradeLogged = true;
        }
        std::lock_guard<std::mutex> lock(s_Mutex);
        if (upgrade)
            s_Chunks[wanted].upgrading = true;
        else
            s_Chunks[wanted].state = ChunkState::Loading;
        s_Request = wanted;
        s_RequestReduction = upgrade ? 1 : s_BaseReduction;
        s_Wake.notify_one();
    }

    while (s_LoadedCount > s_MaxLoaded)
    {
        const int victim = EvictionCandidate();
        if (victim < 0)
            break;
        Unload(victim);
    }
}

void MapHd::Render(glm::mat4* projectionMatrix, glm::mat4* viewMatrix)
{
    if (!s_Enabled || !s_Active || TexturedQuad::m_Shader.m_id == 0)
        return;

    Shader& shader = TexturedQuad::m_Shader;
    shader.Bind();
    shader.SetUniform("tex", 0);
    shader.SetUniform("u_ProjectionMatrix", *projectionMatrix);
    shader.SetUniform("u_ViewMatrix", *viewMatrix);
    shader.SetUniform("u_ModelMatrix", glm::mat4(1.0f));
    shader.SetUniform("u_Scale", 1.0f);
    for (int index : s_Visible)
        if (s_Chunks[index].state == ChunkState::Loaded)
            s_Chunks[index].mesh->Render();
    shader.Unbind();
}

void MapHd::Destroy()
{
    if (!s_Enabled)
        return;
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_Stop = true;
    }
    s_Wake.notify_one();
    if (s_Worker.joinable())
        s_Worker.join();
    if (s_HasDecoded)
        FreePixels(s_Decoded);
    s_HasDecoded = false;

    // How it went, for the next time the memory rule is looked at.
    Log("MAP HD: at most " + std::to_string(s_PeakLoaded) + " chunks loaded at once (" + std::to_string(s_PeakFull) +
        " at full size), " + std::to_string(s_PeakTextureKb / 1024) + " MB of textures; least memory free when one was asked for: " +
        MegabytesText(s_LeastFree));

    for (int index = 0; index < MapHdGrid::ChunkCount; ++index)
        Unload(index);
    s_Visible.clear();
    s_Enabled = false;
    s_Active = false;
}
