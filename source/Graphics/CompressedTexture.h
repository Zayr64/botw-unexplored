#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../Dds.h"

// BC1 textures from DDS files (Dds.h), where the GPU reads BC1.
namespace CompressedTexture
{
    // Whether the GL driver takes BC1 (GL_EXT_texture_compression_s3tc); asked once, and logged.
    bool Bc1Supported();

    // The whole file, or false.
    bool ReadFile(const std::string& path, std::vector<uint8_t>& out);

    // Every level of `info` into the texture bound to GL_TEXTURE_2D, with as many mipmap levels as the file has.
    // Returns the first GL error (0 if none): GL_OUT_OF_MEMORY when the video memory is short.
    unsigned int Upload(const uint8_t* data, const Dds::Info& info);
}
