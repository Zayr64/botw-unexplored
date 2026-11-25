#include "MapHd.h"

#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <glad/glad.h>
#include <stb/stb_image.h>
#include <switch.h>

#include "Graphics/BasicVertices.h"
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
    };

    struct Decoded
    {
        int index = -1;
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        bool fromDecoder = true; // freed by stbi_image_free; false: a halved copy, freed by free
        int reduction = 1;
    };

    void FreePixels(Decoded& decoded)
    {
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
    unsigned int s_NextMemoryCheck = 0;
    bool s_MemoryLogged = false;
    bool s_UpgradeLogged = false;
    unsigned int s_Frame = 0;
    int s_LoadedCount = 0;
    int s_FullCount = 0; // loaded at full size
    // For the log at the end: the most chunks loaded at once, and the least memory free when one was asked for.
    int s_PeakLoaded = 0;
    int s_PeakFull = 0;
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
            s_Decoded = decoded;
            s_HasDecoded = true;
            s_Request = -1;
        }
    }

    void Upload(const Decoded& decoded)
    {
        Chunk& chunk = s_Chunks[decoded.index];
        Decoded pixels = decoded;
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
        if (!decoded.pixels || decoded.width != width || decoded.height != height)
        {
            Log("MAP HD: could not load", MapHdGrid::ChunkFileName(decoded.index) +
                                              (decoded.reduction == 1 ? " (full size)" : " (halved)"));
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
        texture->m_Width = decoded.width;
        texture->m_Height = decoded.height;
        texture->m_NrChannels = 3;
        glActiveTexture(GL_TEXTURE0);
        glGenTextures(1, &texture->m_Texture);
        glBindTexture(GL_TEXTURE_2D, texture->m_Texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, decoded.width, decoded.height, 0, GL_RGB, GL_UNSIGNED_BYTE, decoded.pixels);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
        FreePixels(pixels);

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
        }
        else
            ++s_LoadedCount;
        chunk.texture = texture;
        chunk.mesh = mesh;
        chunk.reduction = decoded.reduction;
        if (chunk.reduction == 1)
            ++s_FullCount;
        chunk.upgrading = false;
        chunk.state = ChunkState::Loaded;
        chunk.lastUsed = s_Frame;
        s_PeakLoaded = std::max(s_PeakLoaded, s_LoadedCount);
        s_PeakFull = std::max(s_PeakFull, s_FullCount);
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
        }
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
    s_BaseReduction = MapHdGrid::Reduction(s_Application);
    s_Stop = false;
    s_Worker = std::thread(Worker);
    s_Enabled = true;
    s_MaxLoaded = MapHdGrid::MaxLoadedChunks(s_Application);
    s_NextMemoryCheck = 0;
    s_MemoryLogged = false;
    s_UpgradeLogged = false;
    Log("MAP HD: on, up to " + std::to_string(s_MaxLoaded) + " chunks" +
        (s_Application ? "" : " at half size (the map at 2x), at full size when zoomed in, each only while memory allows"));
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
            decoded = s_Decoded;
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
    if (wanted >= 0 && idle && HasRoomForChunk())
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
        " at full size); least memory free when one was asked for: " + MegabytesText(s_LeastFree));

    for (int index = 0; index < MapHdGrid::ChunkCount; ++index)
        Unload(index);
    s_Visible.clear();
    s_Enabled = false;
    s_Active = false;
}
