// HeroPath: made-up trackblock files, and the real ones of a save when given.
//
//   test_hero_path [tracker folder]
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "HeroPath.h"

namespace
{
    void Put32(std::vector<uint8_t>& file, size_t at, uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
            file[at + i] = static_cast<uint8_t>(value >> (8 * i));
    }

    uint32_t Word(int x, int z, uint32_t flags)
    {
        const uint32_t rawX = static_cast<uint32_t>(x >= 0 ? x : 8192 - x) & 0x3FFF;
        const uint32_t rawZ = static_cast<uint32_t>(z >= 0 ? z : 4096 - z) & 0x1FFF;
        return (flags << 27) | (rawX << 13) | rawZ;
    }

    // One file with `records` records; record r has time `times[r]` and the points given for it.
    std::vector<uint8_t> MakeFile(const std::vector<uint32_t>& times, const std::vector<std::vector<uint32_t>>& points)
    {
        std::vector<uint8_t> file(HeroPath::FileSize, 0);
        file[2] = static_cast<uint8_t>(times.size() - 1);
        const uint16_t lastCount = static_cast<uint16_t>(points.back().size());
        file[4] = static_cast<uint8_t>(lastCount);
        file[5] = static_cast<uint8_t>(lastCount >> 8);
        for (size_t r = 0; r < times.size(); ++r)
        {
            const size_t at = 64 + r * 1216;
            Put32(file, at + 4, times[r]);
            for (size_t i = 0; i < points[r].size(); ++i)
                Put32(file, at + 16 + i * 4, points[r][i]);
        }
        return file;
    }

    std::vector<uint32_t> Walk(int fromX, int fromZ, int steps, int dx, int dz)
    {
        std::vector<uint32_t> words;
        for (int i = 0; i < steps; ++i)
            words.push_back(Word(fromX + i * dx, fromZ + i * dz, 0x10));
        return words;
    }
}

int main(int argc, char** argv)
{
    using namespace HeroPath;

    // Negative coordinates, a full record of 300 and a last record of 5; records out of order are put in order.
    {
        std::vector<uint32_t> first = Walk(-100, -50, 300, 1, 1);
        std::vector<uint32_t> last = Walk(200, 250, 5, 1, 0);
        std::vector<uint8_t> file = MakeFile({20, 10}, {first, last}); // the second record is older
        std::vector<Record> records;
        std::string error;
        assert(ReadFile(file.data(), file.size(), records, error) && records.size() == 2);
        assert(records[0].words.size() == 300 && records[1].words.size() == 5);
        const Path path = Build(records);
        // Older first: (200..204, 250), then a jump of more than 500? (204,250) -> (-100,-50) is 427: one segment.
        assert(path.segments.size() == 1 && path.points == 305);
        assert(path.segments[0].points[0].x == 200.0f && path.segments[0].points[0].z == 250.0f);
        assert(path.segments[0].points[5].x == -100.0f && path.segments[0].points[5].z == -50.0f);
        // 4 steps of 1, the step from (204, 250) to (-100, -50), then 299 diagonal steps.
        const double expected = 4.0 + std::hypot(304.0, 300.0) + 299.0 * std::sqrt(2.0);
        assert(std::fabs(Length(path) - expected) < 0.01);
    }

    // Breaks: inside a shrine (no 0x10), a warp before a point, a death after one, a long jump.
    {
        std::vector<uint32_t> words = Walk(0, 0, 3, 10, 0);                 // segment A: 3 points
        words.push_back(Word(30, 0, 0x08));                                 // in a shrine: break
        std::vector<uint32_t> b = Walk(1000, 1000, 3, 5, 0);                // segment B
        words.insert(words.end(), b.begin(), b.end());
        words.push_back(Word(1015, 1000, 0x10 | 0x04));                     // last point of B, then a death
        std::vector<uint32_t> c = Walk(-2000, 3000, 2, 1, 1);               // segment C
        words.insert(words.end(), c.begin(), c.end());
        words.push_back(Word(-1000, 3000, 0x10 | 0x01));                    // warp before it: D starts here
        words.push_back(Word(-999, 3000, 0x10));
        words.push_back(Word(4000, 3000, 0x10));                            // jump > 500: E
        words.push_back(Word(4001, 3000, 0x10));
        words.push_back(0xFFFFFFFFu);                                       // no point
        std::vector<uint8_t> file = MakeFile({1}, {words});
        std::vector<Record> records;
        std::string error;
        assert(ReadFile(file.data(), file.size(), records, error));
        const Path path = Build(records);
        assert(path.segments.size() == 5);
        assert(path.segments[0].points.size() == 3 && path.segments[0].end == End::None);
        assert(path.segments[1].points.size() == 4 && path.segments[1].end == End::Death);
        assert(path.segments[2].points.size() == 2 && path.segments[2].end == End::Warp);
        assert(path.segments[3].points.size() == 2 && path.segments[4].points.size() == 2);
    }

    // Empty file: nothing, no error. Wrong size or header: refused.
    {
        std::vector<uint8_t> empty(FileSize, 0);
        std::vector<Record> records;
        std::string error;
        assert(ReadFile(empty.data(), empty.size(), records, error) && records.empty());
        assert(!ReadFile(empty.data(), 1000, records, error));
        empty[2] = 60;
        assert(!ReadFile(empty.data(), empty.size(), records, error) && !error.empty());
    }

    // The real files of a save, when given: everything on the map.
    if (argc > 1)
    {
        std::vector<Record> records;
        int files = 0;
        for (int i = 0; i < MaxFiles; ++i)
        {
            char name[32];
            std::snprintf(name, sizeof(name), "/trackblock%02d.sav", i);
            std::ifstream in(std::string(argv[1]) + name, std::ios::binary);
            if (!in)
                continue;
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::string error;
            assert(ReadFile(data.data(), data.size(), records, error));
            ++files;
        }
        const Path path = Build(records);
        float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
        int warps = 0, deaths = 0;
        for (const Segment& s : path.segments)
        {
            warps += s.end == End::Warp;
            deaths += s.end == End::Death;
            for (const Point& p : s.points)
            {
                minX = std::fmin(minX, p.x);
                maxX = std::fmax(maxX, p.x);
                minZ = std::fmin(minZ, p.z);
                maxZ = std::fmax(maxZ, p.z);
            }
        }
        std::printf("%s: %d files, %zu records, %zu points in %zu segments (%d end in a warp, %d in a death); "
                    "x %.0f..%.0f, z %.0f..%.0f; %.1f km\n",
                    argv[1], files, records.size(), path.points, path.segments.size(), warps, deaths, minX, maxX, minZ,
                    maxZ, Length(path) / 1000.0);
        assert(path.points > 0 && minX >= -6000 && maxX <= 6000 && minZ >= -5000 && maxZ <= 5000);
    }

    std::printf("hero path: ok\n");
    return 0;
}
