#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// DDS files with BC1 (DXT1) data and their mipmaps, as tools/map_dds.cpp writes them: the high-resolution map chunks
// (map-hd/*.dds on the SD card) and romfs:/map-lowres.dds. BC1 keeps 4 bits a pixel, which the GPU reads as they are:
// an eighth of what an RGB image takes there (the GPU stores RGB as RGBA). Rows go bottom up, as the app loads its
// other textures.
namespace Dds
{
    constexpr size_t HeaderSize = 128; // "DDS " and the 124-byte header
    constexpr int MaxLevels = 16;

    struct Level
    {
        int width = 0;
        int height = 0;
        size_t offset = 0; // from the start of the file
        size_t size = 0;
    };

    struct Info
    {
        int width = 0;
        int height = 0;
        int levelCount = 0;
        Level levels[MaxLevels];
        size_t dataBytes = 0; // every level
    };

    // Bytes of one BC1 level: 8 per block of 4 x 4 pixels, a partial block counted whole.
    size_t Bc1LevelSize(int width, int height);

    // Whether `data` is a BC1 DDS whose every level is in it; where each level is. A file with a full mipmap chain or
    // with fewer levels (the GPU is told how many) both pass.
    bool Parse(const uint8_t* data, size_t size, Info& out);

    // The DDS that may stand for an image: the same path with .dds in place of .png, .jpg or .jpeg; "" for others.
    std::string SiblingPath(const std::string& imagePath);
}
