#include "MapHdGrid.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <istream>
#include <sstream>

namespace
{
    bool IsValidIndex(int index)
    {
        return index >= 0 && index < MapHdGrid::ChunkCount;
    }
}

int MapHdGrid::ChunkWidthPixels(int index)
{
    if (!IsValidIndex(index)) return 0;
    const int column = index % Columns;
    return std::min(ChunkPixels, HdWidth - column * ChunkPixels);
}

int MapHdGrid::ChunkHeightPixels(int index)
{
    if (!IsValidIndex(index)) return 0;
    const int row = index / Columns;
    return std::min(ChunkPixels, HdHeight - row * ChunkPixels);
}

MapHdGrid::Rect MapHdGrid::ChunkRect(int index)
{
    if (!IsValidIndex(index)) return {0.0f, 0.0f, 0.0f, 0.0f};
    const int column = index % Columns;
    const int row = index / Columns;
    const float chunkUnits = static_cast<float>(ChunkPixels) / Scale;
    Rect rect;
    rect.left = -MapWidth / 2.0f + column * chunkUnits;
    rect.top = MapHeight / 2.0f - row * chunkUnits;
    rect.right = rect.left + static_cast<float>(ChunkWidthPixels(index)) / Scale;
    rect.bottom = rect.top - static_cast<float>(ChunkHeightPixels(index)) / Scale;
    return rect;
}

std::string MapHdGrid::ChunkFileName(int index)
{
    if (!IsValidIndex(index)) return "";
    char name[32];
    std::snprintf(name, sizeof(name), "%02d_%02d.jpg", index % Columns, index / Columns);
    return name;
}

std::vector<int> MapHdGrid::VisibleChunks(float centerX, float centerY, float halfWidth, float halfHeight)
{
    std::vector<std::pair<float, int>> found;
    for (int index = 0; index < ChunkCount; ++index)
    {
        const Rect rect = ChunkRect(index);
        if (rect.right <= centerX - halfWidth || rect.left >= centerX + halfWidth ||
            rect.top <= centerY - halfHeight || rect.bottom >= centerY + halfHeight)
            continue;
        const float dx = (rect.left + rect.right) / 2.0f - centerX;
        const float dy = (rect.bottom + rect.top) / 2.0f - centerY;
        found.push_back({dx * dx + dy * dy, index});
    }
    std::sort(found.begin(), found.end());
    std::vector<int> result;
    result.reserve(found.size());
    for (const auto& entry : found) result.push_back(entry.second);
    return result;
}

bool MapHdGrid::IsUsableZoom(float zoom)
{
    return std::isfinite(zoom) && zoom >= MinZoom;
}

int MapHdGrid::MaxLoadedChunks(bool application)
{
    return application ? MaxLoadedApplication : MaxLoadedApplet;
}

bool MapHdGrid::HasMemoryForChunk(uint64_t freeBytes)
{
    return freeBytes >= AppletReserveBytes + ChunkPeakBytes;
}

bool MapHdGrid::IsValidInfo(std::istream& input)
{
    std::string header, version, size;
    if (!std::getline(input, header) || !std::getline(input, version) || !std::getline(input, size))
        return false;
    if (!header.empty() && header.back() == '\r') header.pop_back();
    if (header != "BOTW_UNEXPLORED_MAP_HD") return false;
    char* end = nullptr;
    if (std::strtol(version.c_str(), &end, 10) != InfoVersion || end == version.c_str()) return false;
    std::istringstream values(size);
    int width = 0, height = 0, chunk = 0;
    if (!(values >> width >> height >> chunk)) return false;
    return width == HdWidth && height == HdHeight && chunk == ChunkPixels;
}

int MapHdGrid::PickEviction(const std::vector<std::pair<int, unsigned int>>& loadedLastUsed, unsigned int currentFrame)
{
    int victim = -1;
    unsigned int oldest = currentFrame;
    for (const auto& entry : loadedLastUsed)
    {
        if (entry.second < oldest)
        {
            oldest = entry.second;
            victim = entry.first;
        }
    }
    return victim;
}
