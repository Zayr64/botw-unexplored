#include "LiveGameData.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
    constexpr int KindCount = static_cast<int>(LiveGameData::FlagKind::Count);
    constexpr uint64_t PageBits = 16;
    constexpr size_t PageSize = static_cast<size_t>(1) << PageBits;
    constexpr size_t PageTail = 0x100;     // the largest flag object, for the ones that cross into the next page
    constexpr size_t SmallPage = 0x1000;
    constexpr int MaxTableSize = 400000;   // the largest table (bool) has tens of thousands of entries
    constexpr size_t MaxPages = 8192;      // 512 MB of pages: far more than the flags take
    constexpr int PointersPerRead = 512;

    // Static so that neither the stack nor the heap of a small process (the overlay) is used.
    uint8_t s_Window[PageSize + PageTail];
    uint64_t s_Pointers[PointersPerRead];

    struct PtrArray
    {
        int32_t size;
        int32_t capacity;
        uint64_t data;
    };

    uint64_t ReadU64(const uint8_t* bytes)
    {
        uint64_t value;
        std::memcpy(&value, bytes, sizeof(value));
        return value;
    }

    void Append(std::string& text, const char* format, ...) __attribute__((format(printf, 2, 3)));
    void Append(std::string& text, const char* format, ...)
    {
        char buffer[160];
        va_list arguments;
        va_start(arguments, format);
        std::vsnprintf(buffer, sizeof(buffer), format, arguments);
        va_end(arguments);
        text += buffer;
    }

    bool Fail(LiveGameData::Report& report, const char* error)
    {
        report.ok = false;
        report.error = error;
        report.details += "FAILED: ";
        report.details += error;
        report.details += "\n";
        return false;
    }

    void AddPage(std::vector<uint64_t>& pages, uint64_t page)
    {
        const std::vector<uint64_t>::iterator place = std::lower_bound(pages.begin(), pages.end(), page);
        if (place == pages.end() || *place != page)
            pages.insert(place, page);
    }

    // Visits the flag objects that start in the first `length` bytes of the window (`available` bytes were read).
    void VisitWindow(const uint8_t* window, size_t length, size_t available, const uint64_t vtables[KindCount],
                     const LiveGameData::Layout& layout, LiveGameData::Visitor& visitor, LiveGameData::Report& report)
    {
        size_t offset = 0;
        while (offset + 8 <= length)
        {
            const uint64_t first = ReadU64(window + offset);
            int kind = -1;
            for (int k = 0; k < KindCount; ++k)
                if (first == vtables[k])
                    kind = k;
            if (kind < 0 || offset + layout.kinds[kind].size > available)
            {
                offset += 8;
                continue;
            }
            const LiveGameData::Layout::Kind& info = layout.kinds[kind];
            uint32_t hash;
            std::memcpy(&hash, window + offset + info.hashOffset, sizeof(hash));
            visitor.OnFlag(static_cast<LiveGameData::FlagKind>(kind), hash, window + offset + info.valueOffset);
            ++report.objects[kind];
            offset += info.size;
        }
    }
}

const uint8_t LiveGameData::BotwBuildId160[8] = {0x8E, 0x99, 0x78, 0xD5, 0x0B, 0xDD, 0x20, 0xB4};

const LiveGameData::Layout& LiveGameData::Botw160()
{
    static const Layout layout = {
        0x2CC5FE8, // ksys::gdt::Manager::sInstance
        0xC00,     // mFlagBuffer
        0xBE0,     // mParam.mParam
        0x08,      // mBoolFlags
        0x2CA1140, // Link: [[main+2CA1140]+60]+398
        0x60,
        0x398,
        {
            {0, 0x2BD1078, 0x18, 0x0C, 0x0B},  // Flag<bool>
            {1, 0x2BD1140, 0x28, 0x1C, 0x18},  // Flag<s32>
            {3, 0x2BD12D0, 0x100, 0xF0, 0xCC}, // Flag<FixedSafeString<32>>: the characters of mValue
            {7, 0x2BD15F0, 0x48, 0x3C, 0x30},  // Flag<Vector3f>
        },
    };
    return layout;
}

bool LiveGameData::Scan(Memory& memory, uint64_t mainBase, const Layout& layout, Visitor& visitor, Report& report)
{
    report = Report();

    uint64_t manager = 0;
    if (!memory.Read(mainBase + layout.managerPointer, &manager, sizeof(manager)) || manager == 0)
        return Fail(report, "the game data is not ready yet");

    uint64_t buffer = 0, reference = 0, referenced = 0;
    if (!memory.Read(manager + layout.flagBufferOffset, &buffer, sizeof(buffer)) || buffer == 0)
        return Fail(report, "the game data is not ready yet");
    if (!memory.Read(manager + layout.flagBufferRef, &reference, sizeof(reference)) || reference == 0 ||
        !memory.Read(reference, &referenced, sizeof(referenced)))
        return Fail(report, "the game data is not ready yet");
    Append(report.details, "manager %llx, flag buffer %llx, getters read %llx\n", static_cast<unsigned long long>(manager),
           static_cast<unsigned long long>(buffer), static_cast<unsigned long long>(referenced));
    if (referenced != buffer)
        return Fail(report, "the game's flag buffer is not where it should be");

    PtrArray tables[9];
    if (!memory.Read(buffer + layout.tablesOffset, tables, sizeof(tables)))
        return Fail(report, "the flag tables could not be read");

    uint64_t vtables[KindCount];
    std::vector<uint64_t> pages;
    for (int kind = 0; kind < KindCount; ++kind)
    {
        const Layout::Kind& info = layout.kinds[kind];
        const PtrArray& table = tables[info.table];
        vtables[kind] = mainBase + info.vtable;
        Append(report.details, "table %d: %d of %d at %llx\n", info.table, table.size, table.capacity,
               static_cast<unsigned long long>(table.data));
        if (table.size <= 0 || table.size > table.capacity || table.capacity > MaxTableSize || table.data == 0)
            return Fail(report, "a flag table has an unexpected size");
        report.tableSize[kind] = table.size;

        for (int32_t start = 0; start < table.size; start += PointersPerRead)
        {
            const int32_t count = std::min<int32_t>(PointersPerRead, table.size - start);
            if (!memory.Read(table.data + static_cast<uint64_t>(start) * 8, s_Pointers, static_cast<size_t>(count) * 8))
                return Fail(report, "a flag table could not be read");
            for (int32_t i = 0; i < count; ++i)
            {
                if (s_Pointers[i] == 0 || (s_Pointers[i] & 7) != 0)
                    return Fail(report, "a flag table holds an invalid pointer");
                AddPage(pages, s_Pointers[i] >> PageBits);
            }
            if (pages.size() > MaxPages)
                return Fail(report, "the flags are spread over too much memory");
        }
    }
    report.pages = static_cast<int>(pages.size());
    Append(report.details, "%d pages of flags\n", report.pages);

    int unreadable = 0;
    for (const uint64_t page : pages)
    {
        const uint64_t address = page << PageBits;
        // The tail catches an object that starts at the end of this page; the next page may not be mapped.
        if (memory.Read(address, s_Window, PageSize + PageTail))
            VisitWindow(s_Window, PageSize, PageSize + PageTail, vtables, layout, visitor, report);
        else if (memory.Read(address, s_Window, PageSize))
            VisitWindow(s_Window, PageSize, PageSize, vtables, layout, visitor, report);
        else
        {
            // Part of the 64 KB is not mapped: go through it 4 KB at a time.
            for (size_t part = 0; part < PageSize; part += SmallPage)
            {
                if (memory.Read(address + part, s_Window, SmallPage + PageTail))
                    VisitWindow(s_Window, SmallPage, SmallPage + PageTail, vtables, layout, visitor, report);
                else if (memory.Read(address + part, s_Window, SmallPage))
                    VisitWindow(s_Window, SmallPage, SmallPage, vtables, layout, visitor, report);
                else
                    ++unreadable;
            }
        }
    }

    Append(report.details, "objects found: bool %d of %d, s32 %d of %d, string %d of %d, vector3 %d of %d; %d unreadable 4 KB blocks\n",
           report.objects[0], report.tableSize[0], report.objects[1], report.tableSize[1], report.objects[2],
           report.tableSize[2], report.objects[3], report.tableSize[3], unreadable);

    // Every entry of a table is one object. Fewer objects than entries means part of the flags was missed
    // (or the tables do not hold what is expected); more is fine, array flags use the same objects.
    for (int kind = 0; kind < KindCount; ++kind)
        if (report.objects[kind] < report.tableSize[kind])
        {
            uint8_t sample[0x18] = {};
            uint64_t firstObject = 0;
            if (memory.Read(tables[layout.kinds[kind].table].data, &firstObject, sizeof(firstObject)) &&
                memory.Read(firstObject, sample, sizeof(sample)))
            {
                report.details += "first object of the table:";
                for (const uint8_t byte : sample)
                    Append(report.details, " %02x", byte);
                Append(report.details, " (expected vtable %llx)\n", static_cast<unsigned long long>(vtables[kind]));
            }
            return Fail(report, "the game's flags do not look as expected");
        }

    report.ok = true;
    return true;
}

bool LiveGameData::ReadPlayerPosition(Memory& memory, uint64_t mainBase, const Layout& layout, float position[3])
{
    uint64_t player = 0, actor = 0;
    float matrix[12];
    if (!memory.Read(mainBase + layout.playerPointer, &player, sizeof(player)) || player == 0 ||
        !memory.Read(player + layout.playerActorOffset, &actor, sizeof(actor)) || actor == 0 ||
        !memory.Read(actor + layout.playerMatrixOffset, matrix, sizeof(matrix)))
        return false;

    const float x = matrix[3], y = matrix[7], z = matrix[11];
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return false;
    // All zeros while the game is still loading.
    if (x == 0.0f && y == 0.0f && z == 0.0f)
        return false;
    position[0] = x;
    position[1] = y;
    position[2] = z;
    return true;
}
