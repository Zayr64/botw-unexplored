#include "Dds.h"

#include <algorithm>
#include <cstring>

namespace
{
    uint32_t U32(const uint8_t* p)
    {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
               (static_cast<uint32_t>(p[3]) << 24);
    }

    bool EndsWith(const std::string& text, const char* suffix)
    {
        const size_t n = std::strlen(suffix);
        if (text.size() < n)
            return false;
        for (size_t i = 0; i < n; i++)
        {
            char c = text[text.size() - n + i];
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
            if (c != suffix[i])
                return false;
        }
        return true;
    }
}

size_t Dds::Bc1LevelSize(int width, int height)
{
    const size_t blocksX = static_cast<size_t>(std::max(1, (width + 3) / 4));
    const size_t blocksY = static_cast<size_t>(std::max(1, (height + 3) / 4));
    return blocksX * blocksY * 8;
}

bool Dds::Parse(const uint8_t* data, size_t size, Info& out)
{
    out = Info();
    if (!data || size < HeaderSize || std::memcmp(data, "DDS ", 4) != 0 || U32(data + 4) != 124)
        return false;
    const uint8_t* pixelFormat = data + 4 + 72; // after size, flags, height, width, pitch, depth, mipmaps, reserved[11]
    if (U32(pixelFormat) != 32 || !(U32(pixelFormat + 4) & 0x4) || std::memcmp(pixelFormat + 8, "DXT1", 4) != 0)
        return false;
    const uint32_t height = U32(data + 12), width = U32(data + 16);
    if (width == 0 || height == 0 || width > 16384 || height > 16384)
        return false;
    uint32_t levels = (U32(data + 8) & 0x20000) ? U32(data + 28) : 1;
    if (levels == 0)
        levels = 1;
    if (levels > static_cast<uint32_t>(MaxLevels))
        return false;

    out.width = static_cast<int>(width);
    out.height = static_cast<int>(height);
    size_t offset = HeaderSize;
    int w = out.width, h = out.height;
    for (uint32_t i = 0; i < levels; i++)
    {
        Level& level = out.levels[i];
        level.width = w;
        level.height = h;
        level.offset = offset;
        level.size = Bc1LevelSize(w, h);
        if (level.size > size - offset)
        {
            out = Info();
            return false;
        }
        offset += level.size;
        out.dataBytes += level.size;
        out.levelCount++;
        if (w == 1 && h == 1)
            break; // a chain never goes further, whatever the header says
        // Each level half the one before, rounded down, as OpenGL wants it
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
    }
    return true;
}

std::string Dds::SiblingPath(const std::string& imagePath)
{
    for (const char* extension : {".png", ".jpg", ".jpeg"})
        if (EndsWith(imagePath, extension))
            return imagePath.substr(0, imagePath.size() - std::strlen(extension)) + ".dds";
    return std::string();
}
