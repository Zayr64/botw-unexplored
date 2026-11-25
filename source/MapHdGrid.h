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

    int MaxLoadedChunks(bool application);
    // How much each chunk is reduced before it becomes a texture: 1 as an application, 2 from the Album.
    int Reduction(bool application);
    // `pixels` (RGB, `width` x `height`) halved into `out`, each pixel the mean of a 2 x 2 block (an odd last row or
    // column counts alone). `out` holds HalfSize(width) x HalfSize(height) pixels.
    int HalfSize(int size);
    void Halve(const unsigned char* pixels, int width, int height, unsigned char* out);
    // From the Album: whether one more chunk may be loaded with this much memory free.
    bool HasMemoryForChunk(uint64_t freeBytes);

    // map-hd.txt: header, version, then "<width> <height> <chunk size>" of the high-resolution image.
    bool IsValidInfo(std::istream& input);

    // The loaded chunk used least recently, skipping the ones used in the current frame; -1 if none.
    int PickEviction(const std::vector<std::pair<int, unsigned int>>& loadedLastUsed, unsigned int currentFrame);
}
