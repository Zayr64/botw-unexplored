#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <utility>
#include <vector>

// Grid of the optional high-resolution map: the map image at 4x, cut in square chunks that are loaded only
// when they are on screen. Map units are pixels of the low-resolution map, centered on (0, 0) with y up.
namespace MapHdGrid
{
    constexpr int MapWidth = 3000;
    constexpr int MapHeight = 2500;
    constexpr int Scale = 4; // high-resolution pixels per map unit
    constexpr int ChunkPixels = 1024;
    constexpr int HdWidth = MapWidth * Scale;
    constexpr int HdHeight = MapHeight * Scale;
    constexpr int Columns = (HdWidth + ChunkPixels - 1) / ChunkPixels;
    constexpr int Rows = (HdHeight + ChunkPixels - 1) / ChunkPixels;
    constexpr int ChunkCount = Columns * Rows;
    constexpr int InfoVersion = 1;

    // Below this zoom the low-resolution map already has a pixel or more per screen pixel.
    constexpr float MinZoom = 1.5f;
    // From this zoom on the halved chunks (2 pixels per map unit) are stretched past 1.25 screen pixels per pixel and
    // look soft: from the Album the chunks on screen are then loaded again at full size, while memory allows. A
    // screen holds at most 3 x 3 chunks here, and fewer as the zoom goes on.
    constexpr float FullZoom = 2.5f;

    // Chunks kept loaded. Each takes up to 1024 x 1024 x 3 bytes plus a third for the mipmaps (about 4 MB).
    // Opened from the Album the app has a fraction of the memory: on the console, with BotW running, 79 MB free when
    // it opens and 60 MB once the map is up (log of 25/11). There each chunk is halved as soon as it is decoded
    // (512 pixels, about 1 MB with its mipmaps: the map at 2x instead of 4x), and fewer are kept: still more than the
    // 15 that a screen needs at MinZoom.
    constexpr int MaxLoadedApplication = 40;
    constexpr int MaxLoadedApplet = 24;
    // From the Album a chunk is loaded only if this much memory stays free after it. Loading one takes the decoded
    // image of the whole chunk for a moment (3 MB, and the decoder's own buffers), then its texture. Until zayr15 the
    // reserve was 64 MB, chosen before the number above was known: with it no chunk ever loaded from the Album.
    constexpr uint64_t AppletReserveBytes = 32ull * 1024 * 1024;
    constexpr uint64_t ChunkPeakBytes = 8ull * 1024 * 1024;
    // The textures do not come out of the memory measured above (zayr17, 25/11: with 9 chunks loaded, 6 of them at
    // full size, the free memory did not move). From the Album their own pool is small: on 26/11, panning zoomed in,
    // the chunks at full size piled up (only the 24 above bounded them) until new textures failed and showed black.
    // So the textures are weighed too, in KB as the GPU holds them: RGB is stored as RGBA, 4 bytes a pixel and a
    // third more for the mipmaps (5461 KB for a chunk at full size, 1365 halved); BC1 takes 4 bits a pixel (683 KB).
    // From the Album up to AppletTextureBudgetKb: 6 chunks at full size and 3 halved on a screen, what zayr25 allowed,
    // reached by upgrades that hold one texture more for a moment (5 full, 4 halved and the new one: 38.2 MB). On the
    // console a texture failed (GL_OUT_OF_MEMORY) near 40 MB on 26/11.
    constexpr int AppletTextureBudgetKb = 38 * 1024 + 512;
    // With the BC1 chunks (map-hd/*.dds, zayr26) every chunk is loaded at full size, from the Album too, and as many
    // stay as in the application: 40 of them take 27 MB.
    constexpr int MaxLoadedCompressed = MaxLoadedApplication;

    struct Rect
    {
        float left, bottom, right, top;
    };

    // Edge chunks are smaller, so the chunks cover exactly HdWidth x HdHeight.
    int ChunkWidthPixels(int index);
    int ChunkHeightPixels(int index);
    Rect ChunkRect(int index);
    std::string ChunkFileName(int index); // "<column>_<row>.jpg", two digits each

    // Chunks overlapping the view, nearest to its center first.
    std::vector<int> VisibleChunks(float centerX, float centerY, float halfWidth, float halfHeight);

    bool IsUsableZoom(float zoom);

    int MaxLoadedChunks(bool application, bool compressed);
    // How much each chunk is reduced before it becomes a texture: 1 as an application or with the BC1 chunks, 2 from
    // the Album with the JPEG ones.
    int Reduction(bool application, bool compressed);
    // The reduction wanted for the chunks on screen at this zoom: from the Album, full size from FullZoom on.
    int WantedReduction(bool application, float zoom);
    // `pixels` (RGB, `width` x `height`) halved into `out`, each pixel the mean of a 2 x 2 block (an odd last row or
    // column counts alone). `out` holds HalfSize(width) x HalfSize(height) pixels.
    int HalfSize(int size);
    void Halve(const unsigned char* pixels, int width, int height, unsigned char* out);
    // From the Album: whether one more chunk may be loaded with this much memory free.
    bool HasMemoryForChunk(uint64_t freeBytes);
    // KB of video memory a chunk takes with its mipmaps: RGB at this reduction (5461 at full size, 1365 halved), or
    // BC1 (683, always at full size).
    int TextureKilobytes(int reduction, bool compressed);
    // KB of textures kept loaded at once: as an application, all of MaxLoadedApplication at full size in RGB (no limit
    // of its own); from the Album, AppletTextureBudgetKb.
    int TextureBudgetKb(bool application);

    // map-hd.txt: header, version, then "<width> <height> <chunk size>" of the high-resolution image.
    bool IsValidInfo(std::istream& input);

    // The loaded chunk used least recently, skipping the ones used in the current frame; -1 if none.
    int PickEviction(const std::vector<std::pair<int, unsigned int>>& loadedLastUsed, unsigned int currentFrame);
}
