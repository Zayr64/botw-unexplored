// The BC1 DDS reader: the header, where each mipmap level is, and the files that do not pass.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Dds.h"

namespace
{
    void Put32(std::vector<uint8_t>& out, uint32_t v)
    {
        for (int i = 0; i < 4; i++)
            out.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }

    // A DDS like tools/map_dds.cpp writes, with `levels` levels and `extra` bytes more or fewer than they need.
    std::vector<uint8_t> MakeDds(uint32_t width, uint32_t height, uint32_t levels, long extra, const char* fourCC = "DXT1")
    {
        std::vector<uint8_t> out = {'D', 'D', 'S', ' '};
        Put32(out, 124);
        Put32(out, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000);
        Put32(out, height);
        Put32(out, width);
        Put32(out, 0);
        Put32(out, 0);
        Put32(out, levels);
        for (int i = 0; i < 11; i++)
            Put32(out, 0);
        Put32(out, 32);
        Put32(out, 0x4);
        out.insert(out.end(), fourCC, fourCC + 4);
        for (int i = 0; i < 5; i++)
            Put32(out, 0);
        Put32(out, 0x8 | 0x1000 | 0x400000);
        for (int i = 0; i < 4; i++)
            Put32(out, 0);
        size_t data = 0;
        uint32_t w = width, h = height;
        for (uint32_t i = 0; i < levels; i++)
        {
            data += Dds::Bc1LevelSize(static_cast<int>(w), static_cast<int>(h));
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }
        out.resize(out.size() + static_cast<size_t>(static_cast<long>(data) + extra), 0xAB);
        return out;
    }
}

int main(int argc, char** argv)
{
    assert(Dds::Bc1LevelSize(1024, 1024) == 256 * 256 * 8);
    assert(Dds::Bc1LevelSize(1, 1) == 8 && Dds::Bc1LevelSize(5, 3) == 2 * 1 * 8 && Dds::Bc1LevelSize(23, 11) == 6 * 3 * 8);

    // A chunk of 1024 with its 11 levels: 699064 bytes after the header, as map_dds reported
    const std::vector<uint8_t> chunk = MakeDds(1024, 1024, 11, 0);
    Dds::Info info;
    assert(Dds::Parse(chunk.data(), chunk.size(), info));
    assert(info.width == 1024 && info.height == 1024 && info.levelCount == 11 && info.dataBytes == 699064);
    assert(info.levels[0].offset == Dds::HeaderSize && info.levels[0].size == 524288);
    assert(info.levels[10].width == 1 && info.levels[10].height == 1 && info.levels[10].size == 8);
    assert(info.levels[10].offset + 8 == chunk.size());

    // An edge chunk: the levels halve rounding down, as OpenGL wants (736, 368, ... 23, 11, 5, 2, 1)
    const std::vector<uint8_t> edge = MakeDds(736, 784, 10, 0);
    assert(Dds::Parse(edge.data(), edge.size(), info) && info.levelCount == 10);
    assert(info.levels[5].width == 23 && info.levels[6].width == 11 && info.levels[7].width == 5);
    assert(info.levels[9].width == 1 && info.levels[9].height == 1);
    // The map under everything: 3000 x 2500, 12 levels
    const std::vector<uint8_t> lowres = MakeDds(3000, 2500, 12, 0);
    assert(Dds::Parse(lowres.data(), lowres.size(), info) && info.levelCount == 12 && info.levels[3].height == 312);

    // Fewer levels than a full chain pass too; a header claiming more than the chain stops at 1 x 1
    const std::vector<uint8_t> two = MakeDds(1024, 1024, 2, 0);
    assert(Dds::Parse(two.data(), two.size(), info) && info.levelCount == 2);
    std::vector<uint8_t> tooMany = MakeDds(4, 4, 3, 0);
    assert(Dds::Parse(tooMany.data(), tooMany.size(), info) && info.levelCount == 3 && info.levels[2].width == 1);
    tooMany[28] = 9; // mipmaps 9 for a 4 x 4 image: only 3 exist
    assert(Dds::Parse(tooMany.data(), tooMany.size(), info) && info.levelCount == 3);

    // Files that do not pass: cut short, another format, not a DDS, too small
    const std::vector<uint8_t> shortFile = MakeDds(1024, 1024, 11, -1);
    assert(!Dds::Parse(shortFile.data(), shortFile.size(), info) && info.levelCount == 0);
    const std::vector<uint8_t> dxt5 = MakeDds(1024, 1024, 11, 0, "DXT5");
    assert(!Dds::Parse(dxt5.data(), dxt5.size(), info));
    std::vector<uint8_t> notDds = chunk;
    notDds[0] = 'X';
    assert(!Dds::Parse(notDds.data(), notDds.size(), info));
    assert(!Dds::Parse(chunk.data(), 100, info) && !Dds::Parse(nullptr, 0, info));
    std::vector<uint8_t> zero = MakeDds(1024, 1024, 11, 0);
    zero[16] = zero[17] = zero[18] = zero[19] = 0; // width 0
    assert(!Dds::Parse(zero.data(), zero.size(), info));

    // The DDS that stands for an image
    assert(Dds::SiblingPath("romfs:/map-lowres.png") == "romfs:/map-lowres.dds");
    assert(Dds::SiblingPath("sdmc:/x/map-hd/05_03.jpg") == "sdmc:/x/map-hd/05_03.dds");
    assert(Dds::SiblingPath("a.JPEG") == "a.dds" && Dds::SiblingPath("a.bmp").empty() && Dds::SiblingPath("").empty());

    // DDS_FILE: a real one made by tools/map_dds.cpp (romfs/map-lowres.dds or a chunk)
    if (argc > 1)
    {
        FILE* f = std::fopen(argv[1], "rb");
        assert(f);
        std::vector<uint8_t> real;
        uint8_t buffer[65536];
        size_t n = 0;
        while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0)
            real.insert(real.end(), buffer, buffer + n);
        std::fclose(f);
        assert(Dds::Parse(real.data(), real.size(), info));
        assert(info.levels[info.levelCount - 1].width == 1 && info.levels[info.levelCount - 1].height == 1);
        assert(Dds::HeaderSize + info.dataBytes == real.size());
        std::printf("test_dds: %s %d x %d, %d levels\n", argv[1], info.width, info.height, info.levelCount);
    }
    return 0;
}
