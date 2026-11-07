#pragma once

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

    // map-hd.txt: header, version, then "<width> <height> <chunk size>" of the high-resolution image.
    bool IsValidInfo(std::istream& input);

    // The loaded chunk used least recently, skipping the ones used in the current frame; -1 if none.
    int PickEviction(const std::vector<std::pair<int, unsigned int>>& loadedLastUsed, unsigned int currentFrame);
}
