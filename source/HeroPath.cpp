#include "HeroPath.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr size_t HeaderSize = 64;
    constexpr size_t RecordSize = 1216;
    constexpr size_t PointsOffset = 16;
    constexpr int MaxRecords = 48;
    constexpr int MaxPoints = 300;
    constexpr float MaxStep = 500.0f; // a longer jump between two points is a break (the Atlas's rule)

    uint32_t U32(const uint8_t* p)
    {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
               (static_cast<uint32_t>(p[3]) << 24);
    }

    uint16_t U16(const uint8_t* p)
    {
        return static_cast<uint16_t>(p[0] | (p[1] << 8));
    }

    class Builder
    {
    public:
        void Break(HeroPath::End end = HeroPath::End::None)
        {
            if (m_Open && end != HeroPath::End::None)
                m_Path.segments.back().end = end;
            m_Open = false;
        }

        void Add(float x, float z)
        {
            if (m_Open)
            {
                const HeroPath::Point& last = m_Path.segments.back().points.back();
                if (std::hypot(x - last.x, z - last.z) > MaxStep)
                    m_Open = false;
            }
            if (!m_Open)
            {
                m_Path.segments.push_back(HeroPath::Segment());
                m_Open = true;
            }
            m_Path.segments.back().points.push_back({x, z});
            ++m_Path.points;
        }

        HeroPath::Path Finish()
        {
            // A point alone draws nothing.
            m_Path.segments.erase(std::remove_if(m_Path.segments.begin(), m_Path.segments.end(),
                                                 [](const HeroPath::Segment& s) { return s.points.size() < 2; }),
                                  m_Path.segments.end());
            m_Path.points = 0;
            for (const HeroPath::Segment& s : m_Path.segments)
                m_Path.points += s.points.size();
            return m_Path;
        }

    private:
        HeroPath::Path m_Path;
        bool m_Open = false;
    };
}

bool HeroPath::ReadFile(const uint8_t* data, size_t size, std::vector<Record>& records, std::string& error)
{
    if (data == nullptr || size != FileSize)
    {
        error = "not a trackblock file (size)";
        return false;
    }
    if (std::all_of(data, data + size, [](uint8_t b) { return b == 0; }))
        return true;
    const int lastRecord = data[2];
    const int lastCount = U16(data + 4);
    if (lastRecord >= MaxRecords || lastCount > MaxPoints)
    {
        error = "not a trackblock file (header)";
        return false;
    }
    for (int r = 0; r <= lastRecord; ++r)
    {
        const uint8_t* record = data + HeaderSize + static_cast<size_t>(r) * RecordSize;
        Record out;
        out.time = U32(record + 4);
        const int count = r == lastRecord ? lastCount : MaxPoints;
        out.words.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
            out.words.push_back(U32(record + PointsOffset + static_cast<size_t>(i) * 4));
        records.push_back(std::move(out));
    }
    return true;
}

double HeroPath::Length(const Path& path)
{
    double total = 0.0;
    for (const Segment& segment : path.segments)
        for (size_t i = 1; i < segment.points.size(); ++i)
            total += std::hypot(segment.points[i].x - segment.points[i - 1].x, segment.points[i].z - segment.points[i - 1].z);
    return total;
}

HeroPath::Path HeroPath::Build(std::vector<Record> records)
{
    std::stable_sort(records.begin(), records.end(), [](const Record& a, const Record& b) { return a.time < b.time; });
    Builder path;
    for (const Record& record : records)
    {
        for (const uint32_t word : record.words)
        {
            const uint32_t flags = word >> 27;
            if (!(flags & 0x10) || flags == 0x1F)
            {
                path.Break();
                continue;
            }
            const int rawX = static_cast<int>((word >> 13) & 0x3FFF);
            const int rawZ = static_cast<int>(word & 0x1FFF);
            const float x = static_cast<float>(rawX >= 8192 ? 8192 - rawX : rawX);
            const float z = static_cast<float>(rawZ >= 4096 ? 4096 - rawZ : rawZ);
            if (flags & 1)
                path.Break(End::Warp);
            path.Add(x, z);
            if (flags & 6)
                path.Break(flags & 4 ? End::Death : End::Warp);
        }
    }
    return path.Finish();
}
