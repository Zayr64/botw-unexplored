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
    // Opened from the Album the app has a fraction of the memory (some 440 MB instead of 3.2 GB), so it keeps
    // fewer: still more than the 15 that a screen needs at MinZoom.
    constexpr int MaxLoadedApplication = 40;
    constexpr int MaxLoadedApplet = 24;
    // From the Album a chunk is loaded only if this much memory stays free after it. Loading one takes its
    // pixels twice for a moment: the decoded image, then the texture with its mipmaps.
    constexpr uint64_t AppletReserveBytes = 64ull * 1024 * 1024;
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
    // From the Album: whether one more chunk may be loaded with this much memory free.
    bool HasMemoryForChunk(uint64_t freeBytes);

    // map-hd.txt: header, version, then "<width> <height> <chunk size>" of the high-resolution image.
    bool IsValidInfo(std::istream& input);

    // The loaded chunk used least recently, skipping the ones used in the current frame; -1 if none.
    int PickEviction(const std::vector<std::pair<int, unsigned int>>& loadedLastUsed, unsigned int currentFrame);
}
