#include "CompressedTexture.h"

#include <cstdio>
#include <cstring>

#include <glad/glad.h>

#include "../Log.h"

#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#endif

bool CompressedTexture::Bc1Supported()
{
    static int supported = -1;
    if (supported < 0)
    {
        supported = 0;
        GLint count = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &count);
        for (GLint i = 0; i < count && !supported; i++)
        {
            const char* name = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
            if (name && (std::strcmp(name, "GL_EXT_texture_compression_s3tc") == 0 ||
                         std::strcmp(name, "GL_EXT_texture_compression_dxt1") == 0))
                supported = 1;
        }
        while (glGetError() != GL_NO_ERROR) {}
        Log(std::string("GL: BC1 (S3TC) textures ") + (supported ? "supported" : "not supported") + ", " +
            std::to_string(count) + " extensions");
    }
    return supported == 1;
}

bool CompressedTexture::ReadFile(const std::string& path, std::vector<uint8_t>& out)
{
    out.clear();
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file)
        return false;
    bool ok = std::fseek(file, 0, SEEK_END) == 0;
    const long size = ok ? std::ftell(file) : -1;
    ok = ok && size > 0 && std::fseek(file, 0, SEEK_SET) == 0;
    if (ok)
    {
        out.resize(static_cast<size_t>(size));
        ok = std::fread(out.data(), 1, out.size(), file) == out.size();
    }
    std::fclose(file);
    if (!ok)
        out.clear();
    return ok;
}

unsigned int CompressedTexture::Upload(const uint8_t* data, const Dds::Info& info)
{
    while (glGetError() != GL_NO_ERROR) {} // only the errors of this texture are looked at
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, info.levelCount - 1);
    for (int i = 0; i < info.levelCount; i++)
    {
        const Dds::Level& level = info.levels[i];
        glCompressedTexImage2D(GL_TEXTURE_2D, i, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, level.width, level.height, 0,
                               static_cast<GLsizei>(level.size), data + level.offset);
        const GLenum error = glGetError();
        if (error != GL_NO_ERROR)
            return error;
    }
    return GL_NO_ERROR;
}
