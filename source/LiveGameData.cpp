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
    constexpr int MaxArrays = 20000;        // array flags of one kind (the game has a few hundred)
    constexpr int MaxArrayElements = 100000;
    constexpr uint64_t MainSpan = 0x4000000; // a vtable taken from the game's memory must be inside main
    constexpr size_t LargestObject = 0x180;  // Flag<FixedSafeString<64>>

    // Static so that neither the stack nor the heap of a small process (the overlay) is used.
    uint8_t s_Window[PageSize + PageTail];
    uint64_t s_Pointers[PointersPerRead];
    uint64_t s_ArrayPointers[PointersPerRead];
    uint8_t s_Object[LargestObject];

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

    // Visits the flag objects that start in the first `length` bytes of the window, which holds the
    // `available` bytes read from `address`.
    void VisitWindow(uint64_t address, const uint8_t* window, size_t length, size_t available, const uint64_t vtables[KindCount],
                     const LiveGameData::Layout& layout, LiveGameData::Visitor& visitor, LiveGameData::Report& report)
    {
        size_t offset = 0;
        while (offset + 8 <= length)
        {
            const uint64_t first = ReadU64(window + offset);
            int kind = -1;
            for (int k = 0; k < KindCount; ++k)
                if (vtables[k] != 0 && first == vtables[k]) // 0: a kind this scan does not read
                    kind = k;
            if (kind < 0 || offset + layout.kinds[kind].size > available)
            {
                offset += 8;
                continue;
            }
            const LiveGameData::Layout::Kind& info = layout.kinds[kind];
            uint32_t hash;
            std::memcpy(&hash, window + offset + info.hashOffset, sizeof(hash));
            visitor.OnFlag(static_cast<LiveGameData::FlagKind>(kind), hash, window + offset + info.valueOffset,
                           address + offset + info.valueOffset);
            ++report.objects[kind];
            offset += info.size;
        }
    }
}

namespace
{
    // Whether the first entries of the bool and s32 tables point at objects with the vtable of Flag<T>.
    bool HoldsFlagObjects(LiveGameData::Memory& memory, const PtrArray tables[9], const uint64_t vtables[KindCount],
                          const LiveGameData::Layout& layout)
    {
        constexpr int Probes = 8;
        for (int kind = 0; kind < 2; ++kind)
        {
            const PtrArray& table = tables[layout.kinds[kind].table];
            if (table.size <= 0 || table.data == 0)
                return false;
            uint64_t pointers[Probes];
            const int count = table.size < Probes ? table.size : Probes;
            if (!memory.Read(table.data, pointers, static_cast<size_t>(count) * 8))
                return false;
            for (int i = 0; i < count; ++i)
            {
                uint64_t first = 0;
                if (pointers[i] == 0 || !memory.Read(pointers[i], &first, sizeof(first)) || first != vtables[kind])
                    return false;
            }
        }
        return true;
    }

    // The vtable of an extra kind, taken from the objects its table points at: the first entries must share one
    // vtable inside main, and their hashes must not go down (the game's tables are sorted by hash, which is how its
    // getters find a flag). 0 when that does not hold.
    uint64_t LearnVtable(LiveGameData::Memory& memory, uint64_t mainBase, const PtrArray& table,
                         const LiveGameData::Layout::Kind& info, const char* name, LiveGameData::Report& report)
    {
        constexpr int Probes = 8;
        if (table.size <= 0 || table.size > table.capacity || table.capacity > MaxTableSize || table.data == 0 ||
            info.size > LargestObject)
        {
            Append(report.details, "%s: table %d of %d at %llx, not used\n", name, table.size, table.capacity,
                   static_cast<unsigned long long>(table.data));
            return 0;
        }
        uint64_t pointers[Probes];
        const int count = table.size < Probes ? table.size : Probes;
        if (!memory.Read(table.data, pointers, static_cast<size_t>(count) * 8))
            return 0;
        uint64_t vtable = 0;
        uint32_t previousHash = 0;
        for (int i = 0; i < count; ++i)
        {
            if (pointers[i] == 0 || (pointers[i] & 7) != 0 || !memory.Read(pointers[i], s_Object, info.size))
                return 0;
            const uint64_t first = ReadU64(s_Object);
            uint32_t hash;
            std::memcpy(&hash, s_Object + info.hashOffset, sizeof(hash));
            if ((i > 0 && (first != vtable || hash < previousHash)) || first < mainBase || first >= mainBase + MainSpan)
            {
                Append(report.details, "%s: the objects of its table do not look like flags, not used\n", name);
                return 0;
            }
            vtable = first;
            previousHash = hash;
        }
        Append(report.details, "%s: vtable main+%llx (expected main+%llx), table %d\n", name,
               static_cast<unsigned long long>(vtable - mainBase), static_cast<unsigned long long>(info.vtable),
               table.size);
        return vtable;
    }

    bool Requested(const LiveGameData::Options& options, uint32_t hash)
    {
        for (int i = 0; i < options.arrayCount; ++i)
            if (options.arrays[i] == hash)
                return true;
        return false;
    }

    // One array flag, element by element: every element must be an object of the kind with the array's hash. With
    // `visitor`, each value is passed on; without it, only checked.
    bool ReadArray(LiveGameData::Memory& memory, const PtrArray& array, uint64_t vtable,
                   const LiveGameData::Layout::Kind& info, LiveGameData::FlagKind kind, uint32_t hash,
                   LiveGameData::Visitor* visitor)
    {
        for (int32_t start = 0; start < array.size; start += PointersPerRead)
        {
            const int32_t count = std::min<int32_t>(PointersPerRead, array.size - start);
            if (!memory.Read(array.data + static_cast<uint64_t>(start) * 8, s_ArrayPointers, static_cast<size_t>(count) * 8))
                return false;
            for (int32_t i = 0; i < count; ++i)
            {
                if (s_ArrayPointers[i] == 0 || (s_ArrayPointers[i] & 7) != 0 ||
                    !memory.Read(s_ArrayPointers[i], s_Object, info.size))
                    return false;
                uint32_t elementHash;
                std::memcpy(&elementHash, s_Object + info.hashOffset, sizeof(elementHash));
                if (ReadU64(s_Object) != vtable || elementHash != hash)
                    return false;
                if (visitor)
                    visitor->OnArrayElement(kind, hash, start + i, s_Object + info.valueOffset);
            }
        }
        return true;
    }

    // The array flags asked for, among the arrays of one kind. Returns how many were read whole. With `vtable` 0
    // (no single flag of the kind to take it from), an array is taken when its first element has the hash asked for
    // at the kind's offset and a vtable inside main; all its elements must then share both.
    int ReadArrays(LiveGameData::Memory& memory, uint64_t mainBase, uint64_t buffer, const LiveGameData::Layout& layout,
                   LiveGameData::FlagKind kind, uint64_t vtable, const LiveGameData::Options& options,
                   LiveGameData::Visitor& visitor, LiveGameData::Report& report)
    {
        const LiveGameData::Layout::Kind& info = layout.kinds[static_cast<int>(kind)];
        PtrArray outer;
        if (info.size > LargestObject ||
            !memory.Read(buffer + layout.arrayTablesOffset + static_cast<uint64_t>(info.table) * 16, &outer, sizeof(outer)))
            return 0;
        if (outer.size <= 0 || outer.size > outer.capacity || outer.capacity > MaxArrays || outer.data == 0)
        {
            Append(report.details, "array table %d: %d of %d, not used\n", info.table, outer.size, outer.capacity);
            return 0;
        }
        int found = 0;
        for (int32_t start = 0; start < outer.size; start += PointersPerRead)
        {
            const int32_t count = std::min<int32_t>(PointersPerRead, outer.size - start);
            if (!memory.Read(outer.data + static_cast<uint64_t>(start) * 8, s_Pointers, static_cast<size_t>(count) * 8))
                return found;
            for (int32_t i = 0; i < count; ++i)
            {
                PtrArray array;
                uint64_t firstElement = 0;
                if (s_Pointers[i] == 0 || !memory.Read(s_Pointers[i], &array, sizeof(array)) || array.size <= 0 ||
                    array.size > array.capacity || array.capacity > MaxArrayElements || array.data == 0 ||
                    !memory.Read(array.data, &firstElement, sizeof(firstElement)) || firstElement == 0 ||
                    (firstElement & 7) != 0 || !memory.Read(firstElement, s_Object, info.size))
                    continue;
                const uint64_t elementVtable = ReadU64(s_Object);
                uint32_t hash;
                std::memcpy(&hash, s_Object + info.hashOffset, sizeof(hash));
                if (!Requested(options, hash))
                    continue;
                if (vtable != 0 ? elementVtable != vtable : (elementVtable < mainBase || elementVtable >= mainBase + MainSpan))
                    continue;
                // Checked whole before anything is passed on, so that the visitor never sees half an array.
                if (!ReadArray(memory, array, elementVtable, info, kind, hash, nullptr) ||
                    !ReadArray(memory, array, elementVtable, info, kind, hash, &visitor))
                {
                    Append(report.details, "array %08x (table %d): its elements could not be read\n", hash, info.table);
                    continue;
                }
                Append(report.details, "array %08x (table %d): %d elements, vtable main+%llx\n", hash, info.table,
                       array.size, static_cast<unsigned long long>(elementVtable - mainBase));
                ++found;
            }
        }
        return found;
    }

    // Adds the pages of the objects a table points at. False (nothing added) when the table is not usable.
    bool AddTablePages(LiveGameData::Memory& memory, const PtrArray& table, std::vector<uint64_t>& pages)
    {
        std::vector<uint64_t> added;
        for (int32_t start = 0; start < table.size; start += PointersPerRead)
        {
            const int32_t count = std::min<int32_t>(PointersPerRead, table.size - start);
            if (!memory.Read(table.data + static_cast<uint64_t>(start) * 8, s_Pointers, static_cast<size_t>(count) * 8))
                return false;
            for (int32_t i = 0; i < count; ++i)
            {
                if (s_Pointers[i] == 0 || (s_Pointers[i] & 7) != 0)
                    return false;
                AddPage(added, s_Pointers[i] >> PageBits);
            }
            if (added.size() > MaxPages)
                return false;
        }
        for (const uint64_t page : added)
            AddPage(pages, page);
        return true;
    }
}

const uint8_t LiveGameData::BotwBuildId160[8] = {0x8E, 0x99, 0x78, 0xD5, 0x0B, 0xDD, 0x20, 0xB4};

const LiveGameData::Layout& LiveGameData::Botw160()
{
    static const Layout layout = {
        0x2CC5FE8, // ksys::gdt::Manager::sInstance
        0xC00,     // mFlagBuffer
        0xBF8,     // mFlagBuffer1
        0xBE0,     // mParam.mParam
        0x08,      // mBoolFlags
        0x98,      // mBoolArrayFlags: after the nine tables of single flags
        0x2CA1140, // Link: [[main+2CA1140]+60]+398
        0x60,
        0x398,
        {
            {0, 0x2BD1078, 0x18, 0x0C, 0x0B},  // Flag<bool>
            {1, 0x2BD1140, 0x28, 0x1C, 0x18},  // Flag<s32>
            {3, 0x2BD12D0, 0x100, 0xF0, 0xCC}, // Flag<FixedSafeString<32>>: the characters of mValue
            {7, 0x2BD15F0, 0x48, 0x3C, 0x30},  // Flag<Vector3f>
            // The extras. Their vtables are taken from the game's tables; the ones here follow the order of the
            // others (0xC8 apart, one per type of the decomp's FlagType) and are only written to the log next to
            // what was found. Sizes and offsets from the decomp (gdtFlag.h): mValue after three FlagConfig values.
            {2, 0x2BD1208, 0x28, 0x1C, 0x18},    // Flag<f32>: like Flag<s32>
            {4, 0x2BD1398, 0x180, 0x170, 0x12C}, // Flag<FixedSafeString<64>>: config 3 x 0x58 from +0x10
        },
    };
    return layout;
}

bool LiveGameData::Scan(Memory& memory, uint64_t mainBase, const Layout& layout, Visitor& visitor, Report& report,
                        const Options& options)
{
    report = Report();

    uint64_t manager = 0;
    if (!memory.Read(mainBase + layout.managerPointer, &manager, sizeof(manager)) || manager == 0)
        return Fail(report, "the game data is not ready yet");

    // The two flag buffers, and the one the game's getters go through (a pointer to one of the two fields).
    uint64_t buffers[2] = {0, 0};
    uint64_t reference = 0, referenced = 0;
    if (!memory.Read(manager + layout.flagBufferOffset, &buffers[0], sizeof(buffers[0])) ||
        !memory.Read(manager + layout.flagBuffer1Offset, &buffers[1], sizeof(buffers[1])) ||
        !memory.Read(manager + layout.flagBufferRef, &reference, sizeof(reference)) || reference == 0 ||
        !memory.Read(reference, &referenced, sizeof(referenced)) || referenced == 0)
        return Fail(report, "the game data is not ready yet");
    Append(report.details, "manager %llx, flag buffers %llx and %llx, getters read %llx\n",
           static_cast<unsigned long long>(manager), static_cast<unsigned long long>(buffers[0]),
           static_cast<unsigned long long>(buffers[1]), static_cast<unsigned long long>(referenced));
    if (referenced != buffers[0] && referenced != buffers[1])
        return Fail(report, "the game's flag buffers are not where they should be");

    // The core kinds by the vtables of 1.6.0; the extras stay 0 (not read) until taken from the game's tables.
    uint64_t vtables[KindCount] = {};
    for (int kind = 0; kind < CoreKinds; ++kind)
        vtables[kind] = mainBase + layout.kinds[kind].vtable;

    // The buffer whose tables point at Flag<T> objects (the other one holds proxies).
    uint64_t buffer = 0;
    PtrArray tables[9];
    for (const uint64_t candidate : buffers)
    {
        if (candidate == 0 || !memory.Read(candidate + layout.tablesOffset, tables, sizeof(tables)))
            continue;
        if (HoldsFlagObjects(memory, tables, vtables, layout))
        {
            buffer = candidate;
            break;
        }
    }
    Append(report.details, "flag objects in buffer %llx\n", static_cast<unsigned long long>(buffer));
    if (buffer == 0)
    {
        // What the first table of the first buffer points at, for the log.
        uint8_t sample[0x18] = {};
        uint64_t firstObject = 0;
        if (buffers[0] != 0 && memory.Read(buffers[0] + layout.tablesOffset, tables, sizeof(tables)) && tables[0].data != 0 &&
            memory.Read(tables[0].data, &firstObject, sizeof(firstObject)) && memory.Read(firstObject, sample, sizeof(sample)))
        {
            report.details += "first object of the first table:";
            for (const uint8_t byte : sample)
                Append(report.details, " %02x", byte);
            Append(report.details, " (expected vtable %llx)\n", static_cast<unsigned long long>(vtables[0]));
        }
        return Fail(report, "the game's flags do not look as expected");
    }

    std::vector<uint64_t> pages;
    for (int kind = 0; kind < CoreKinds; ++kind)
    {
        const Layout::Kind& info = layout.kinds[kind];
        const PtrArray& table = tables[info.table];
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

    // The f32 flags, when asked for: read with the others when their table holds objects that look like flags.
    const int f32 = static_cast<int>(FlagKind::F32);
    if (options.f32)
    {
        const PtrArray& table = tables[layout.kinds[f32].table];
        const uint64_t vtable = LearnVtable(memory, mainBase, table, layout.kinds[f32], "f32", report);
        if (vtable != 0 && AddTablePages(memory, table, pages) && pages.size() <= MaxPages)
        {
            vtables[f32] = vtable;
            report.tableSize[f32] = table.size;
        }
    }
    // The vtable of the 64-character texts, for their arrays only (single ones are not read).
    const int string64 = static_cast<int>(FlagKind::String64);
    uint64_t string64Vtable = 0;
    if (options.arrayCount > 0)
        string64Vtable = LearnVtable(memory, mainBase, tables[layout.kinds[string64].table], layout.kinds[string64],
                                     "string64", report);

    report.pages = static_cast<int>(pages.size());
    Append(report.details, "%d pages of flags\n", report.pages);

    int unreadable = 0;
    for (const uint64_t page : pages)
    {
        const uint64_t address = page << PageBits;
        // The tail catches an object that starts at the end of this page; the next page may not be mapped.
        if (memory.Read(address, s_Window, PageSize + PageTail))
            VisitWindow(address, s_Window, PageSize, PageSize + PageTail, vtables, layout, visitor, report);
        else if (memory.Read(address, s_Window, PageSize))
            VisitWindow(address, s_Window, PageSize, PageSize, vtables, layout, visitor, report);
        else
        {
            // Part of the 64 KB is not mapped: go through it 4 KB at a time.
            for (size_t part = 0; part < PageSize; part += SmallPage)
            {
                if (memory.Read(address + part, s_Window, SmallPage + PageTail))
                    VisitWindow(address + part, s_Window, SmallPage, SmallPage + PageTail, vtables, layout, visitor, report);
                else if (memory.Read(address + part, s_Window, SmallPage))
                    VisitWindow(address + part, s_Window, SmallPage, SmallPage, vtables, layout, visitor, report);
                else
                    ++unreadable;
            }
        }
    }

    Append(report.details, "objects found: bool %d of %d, s32 %d of %d, string %d of %d, vector3 %d of %d; %d unreadable 4 KB blocks\n",
           report.objects[0], report.tableSize[0], report.objects[1], report.tableSize[1], report.objects[2],
           report.tableSize[2], report.objects[3], report.tableSize[3], unreadable);
    if (vtables[f32] != 0)
        Append(report.details, "objects found: f32 %d of %d\n", report.objects[f32], report.tableSize[f32]);

    // Every entry of a table is one object. Fewer objects than entries means part of the flags was missed
    // (or the tables do not hold what is expected); more is fine, array flags use the same objects.
    for (int kind = 0; kind < CoreKinds; ++kind)
        if (report.objects[kind] < report.tableSize[kind])
            return Fail(report, "part of the game's flags could not be read");
    report.f32 = vtables[f32] != 0 && report.objects[f32] >= report.tableSize[f32];

    if (options.arrayCount > 0)
    {
        report.arraysFound = ReadArrays(memory, mainBase, buffer, layout, FlagKind::Bool, vtables[0], options, visitor, report) +
                             ReadArrays(memory, mainBase, buffer, layout, FlagKind::S32, vtables[1], options, visitor, report) +
                             ReadArrays(memory, mainBase, buffer, layout, FlagKind::String64, string64Vtable, options, visitor, report);
        Append(report.details, "arrays read: %d of %d asked for\n", report.arraysFound, options.arrayCount);
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
