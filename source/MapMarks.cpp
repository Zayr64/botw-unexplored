#include "MapMarks.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace
{
    using LiveGameData::Layout;
    using LiveGameData::Memory;

    struct PtrArray
    {
        int32_t size;
        int32_t capacity;
        uint64_t data;
    };

    constexpr int MaxSlots = 512;    // the game has 200
    constexpr int MaxArrays = 20000; // array flags of one kind
    // FlagProxy<T> (decomp gdtFlagProxy.h): the vtable, mFlag (the Flag<T> it stands for), mValue (its own copy)
    constexpr uint64_t ProxyFlagOffset = 0x08;
    constexpr uint64_t ProxyValueOffset = 0x10;
    constexpr size_t ProxyRead = 0x20;
    constexpr size_t LargestFlag = 0x48; // Flag<Vector3f>

    void Append(std::string& text, const char* format, ...) __attribute__((format(printf, 2, 3)));
    void Append(std::string& text, const char* format, ...)
    {
        char buffer[200];
        va_list arguments;
        va_start(arguments, format);
        std::vsnprintf(buffer, sizeof(buffer), format, arguments);
        va_end(arguments);
        text += buffer;
    }

    bool Fail(MapMarks::Result& result, const char* error)
    {
        result.ok = false;
        result.error = error;
        result.details += "FAILED: ";
        result.details += error;
        result.details += "\n";
        return false;
    }

    uint64_t U64(const uint8_t* bytes)
    {
        uint64_t value;
        std::memcpy(&value, bytes, sizeof(value));
        return value;
    }

    // The element pointers of a sead::PtrArray.
    bool Elements(Memory& memory, uint64_t header, std::vector<uint64_t>& out)
    {
        PtrArray array;
        if (header == 0 || !memory.Read(header, &array, sizeof(array)) || array.size <= 0 || array.size > array.capacity ||
            array.size > MaxSlots || array.data == 0)
            return false;
        out.assign(static_cast<size_t>(array.size), 0);
        return memory.Read(array.data, out.data(), out.size() * 8) && out[0] != 0;
    }

    // The arrays of one kind of a flag buffer (TriggerParam): pointers to their PtrArray headers.
    bool ArrayHeaders(Memory& memory, uint64_t buffer, const Layout& layout, const Layout::Kind& info,
                      std::vector<uint64_t>& out)
    {
        PtrArray outer;
        if (buffer == 0 ||
            !memory.Read(buffer + layout.arrayTablesOffset + static_cast<uint64_t>(info.table) * 16, &outer, sizeof(outer)) ||
            outer.size <= 0 || outer.size > outer.capacity || outer.capacity > MaxArrays || outer.data == 0)
            return false;
        out.assign(static_cast<size_t>(outer.size), 0);
        return memory.Read(outer.data, out.data(), out.size() * 8);
    }

    // The array of Flag<T> objects with this hash: its first element has the kind's vtable and the hash.
    bool FindFlagArray(Memory& memory, uint64_t buffer, const Layout& layout, const Layout::Kind& info, uint64_t vtable,
                       uint32_t hash, std::vector<uint64_t>& elements)
    {
        std::vector<uint64_t> headers;
        if (info.size > LargestFlag || !ArrayHeaders(memory, buffer, layout, info, headers))
            return false;
        uint8_t object[LargestFlag];
        for (const uint64_t header : headers)
        {
            std::vector<uint64_t> list;
            if (!Elements(memory, header, list) || !memory.Read(list[0], object, info.size))
                continue;
            uint32_t found;
            std::memcpy(&found, object + info.hashOffset, sizeof(found));
            if (U64(object) == vtable && found == hash)
            {
                elements = list;
                return true;
            }
        }
        return false;
    }

    // The array of proxies whose first element stands for `firstFlag`.
    bool FindProxyArray(Memory& memory, uint64_t buffer, const Layout& layout, const Layout::Kind& info,
                        uint64_t firstFlag, std::vector<uint64_t>& elements)
    {
        std::vector<uint64_t> headers;
        if (!ArrayHeaders(memory, buffer, layout, info, headers))
            return false;
        uint8_t proxy[ProxyRead];
        for (const uint64_t header : headers)
        {
            std::vector<uint64_t> list;
            if (!Elements(memory, header, list) || !memory.Read(list[0], proxy, sizeof(proxy)))
                continue;
            if (U64(proxy + ProxyFlagOffset) == firstFlag)
            {
                elements = list;
                return true;
            }
        }
        return false;
    }

    using Slot = MapMarks::Slot;

    bool IsEmpty(const Slot& slot)
    {
        return slot.icon == MapMarks::EmptyIcon && slot.proxyIcon == MapMarks::EmptyIcon &&
               slot.position[0] <= MapMarks::EmptyX && slot.proxyPosition[0] <= MapMarks::EmptyX;
    }

    bool IsShown(int32_t icon, const float position[3])
    {
        return icon >= 0 && icon <= 44 && position[0] > MapMarks::EmptyX;
    }
}

bool MapMarks::Load(Memory& memory, uint64_t mainBase, const Layout& layout, State& state, Result& result)
{
    state = State();
    const Layout::Kind& s32 = layout.kinds[static_cast<int>(LiveGameData::FlagKind::S32)];
    const Layout::Kind& vec3 = layout.kinds[static_cast<int>(LiveGameData::FlagKind::Vector3f)];
    const uint64_t s32Vtable = mainBase + s32.vtable, vec3Vtable = mainBase + vec3.vtable;

    uint64_t manager = 0, buffers[2] = {0, 0};
    if (!memory.Read(mainBase + layout.managerPointer, &manager, sizeof(manager)) || manager == 0 ||
        !memory.Read(manager + layout.flagBufferOffset, &buffers[0], sizeof(buffers[0])) ||
        !memory.Read(manager + layout.flagBuffer1Offset, &buffers[1], sizeof(buffers[1])))
        return Fail(result, "the game data is not ready yet");

    // The buffer of Flag<T> objects is the one whose arrays have their vtables; the other holds the proxies.
    std::vector<uint64_t>& flagIcons = state.flagIcons;
    std::vector<uint64_t>& flagPositions = state.flagPositions;
    std::vector<uint64_t>& proxyIcons = state.proxyIcons;
    std::vector<uint64_t>& proxyPositions = state.proxyPositions;
    int flags = -1;
    for (int i = 0; i < 2 && flags < 0; ++i)
        if (FindFlagArray(memory, buffers[i], layout, s32, s32Vtable, IconNoHash, flagIcons) &&
            FindFlagArray(memory, buffers[i], layout, vec3, vec3Vtable, IconPosHash, flagPositions))
            flags = i;
    if (flags < 0)
        return Fail(result, "the marks of the game's map were not found");
    const uint64_t proxies = buffers[1 - flags];
    if (!FindProxyArray(memory, proxies, layout, s32, flagIcons[0], proxyIcons) ||
        !FindProxyArray(memory, proxies, layout, vec3, flagPositions[0], proxyPositions))
        return Fail(result, "the copy of the map's marks that the game saves was not found");
    const size_t count = flagIcons.size();
    Append(result.details, "map marks: %zu slots; flags in buffer %llx, proxies in %llx\n", count,
           static_cast<unsigned long long>(buffers[flags]), static_cast<unsigned long long>(proxies));
    if (flagPositions.size() != count || proxyIcons.size() != count || proxyPositions.size() != count)
        return Fail(result, "the marks of the game's map do not look as expected");

    // Every slot, both copies; each proxy must stand for the flag of the same slot.
    std::vector<Slot>& slots = state.slots;
    slots.assign(count, Slot());
    uint8_t object[LargestFlag];
    uint8_t proxy[ProxyRead];
    for (size_t i = 0; i < count; ++i)
    {
        Slot& slot = slots[i];
        uint32_t hash = 0;
        if (!memory.Read(flagIcons[i], object, s32.size) || U64(object) != s32Vtable)
            return Fail(result, "a mark of the game's map could not be read");
        std::memcpy(&hash, object + s32.hashOffset, 4);
        std::memcpy(&slot.icon, object + s32.valueOffset, 4);
        if (hash != IconNoHash)
            return Fail(result, "a mark of the game's map could not be read");
        if (!memory.Read(flagPositions[i], object, vec3.size) || U64(object) != vec3Vtable)
            return Fail(result, "a mark of the game's map could not be read");
        std::memcpy(&hash, object + vec3.hashOffset, 4);
        std::memcpy(slot.position, object + vec3.valueOffset, 12);
        if (hash != IconPosHash)
            return Fail(result, "a mark of the game's map could not be read");
        if (!memory.Read(proxyIcons[i], proxy, sizeof(proxy)) || U64(proxy + ProxyFlagOffset) != flagIcons[i])
            return Fail(result, "the copy of a mark could not be read");
        std::memcpy(&slot.proxyIcon, proxy + ProxyValueOffset, 4);
        if (!memory.Read(proxyPositions[i], proxy, sizeof(proxy)) || U64(proxy + ProxyFlagOffset) != flagPositions[i])
            return Fail(result, "the copy of a mark could not be read");
        std::memcpy(slot.proxyPosition, proxy + ProxyValueOffset, 12);
    }

    // What is on the map, and which pins
    for (size_t i = 0; i < count; ++i)
    {
        const Slot& slot = slots[i];
        if (IsEmpty(slot))
            continue;
        Append(result.details, "slot %zu: icon %d at %.1f %.1f %.1f; saved copy: icon %d at %.1f %.1f %.1f\n", i,
               static_cast<int>(slot.icon), slot.position[0], slot.position[1], slot.position[2],
               static_cast<int>(slot.proxyIcon), slot.proxyPosition[0], slot.proxyPosition[1], slot.proxyPosition[2]);
        if (IsShown(slot.icon, slot.position))
        {
            Mark mark = {static_cast<int>(i), slot.icon, {slot.position[0], slot.position[1], slot.position[2]}};
            result.before.push_back(mark);
        }
        for (const int32_t used : {slot.icon, slot.proxyIcon})
            if (used >= FirstPin && used <= LastPin)
                state.pinUsed[used - FirstPin] = true;
    }
    state.valueOffsets[0] = s32.valueOffset;
    state.valueOffsets[1] = vec3.valueOffset;
    return true;
}

bool MapMarks::Add(Memory& memory, uint64_t mainBase, const Layout& layout, int32_t icon, const float position[3],
                   Result& result)
{
    result = Result();
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(position[i]) || std::fabs(position[i]) > 6000.0f)
            return Fail(result, "the place is outside the map");
    State state;
    if (!Load(memory, mainBase, layout, state, result))
        return false;
    const std::vector<Slot>& slots = state.slots;
    const size_t count = slots.size();
    const bool* pinUsed = state.pinUsed;

    // One mark per place: Y pressed again on the same card does not put a second pin there
    for (const Mark& mark : result.before)
        if (std::fabs(mark.position[0] - position[0]) < 2.0f && std::fabs(mark.position[2] - position[2]) < 2.0f)
        {
            Append(result.details, "slot %d already marks that place\n", mark.slot);
            return Fail(result, "the game's map already has a mark there");
        }

    if (icon < 0)
    {
        for (int32_t pin = FirstPin; pin <= LastPin && icon < 0; ++pin)
            if (!pinUsed[pin - FirstPin])
                icon = pin;
        if (icon < 0)
            return Fail(result, "the five pins are already on the game's map");
    }
    else if (icon > 44 || (icon >= FirstPin && icon <= LastPin && pinUsed[icon - FirstPin]))
        return Fail(result, "that pin is already on the game's map");

    int chosen = -1;
    for (size_t i = 0; i < count && chosen < 0; ++i)
        if (IsEmpty(slots[i]))
            chosen = static_cast<int>(i);
    if (chosen < 0)
        return Fail(result, "the game's map has no room for another mark");

    // The position first: the game skips a slot while its icon is -1, so a mark never shows half written.
    const size_t s = static_cast<size_t>(chosen);
    const int32_t emptyIcon = EmptyIcon;
    const float emptyPosition[3] = {EmptyX, 0.0f, 0.0f};
    const uint64_t places[4] = {state.proxyPositions[s] + ProxyValueOffset, state.flagPositions[s] + state.valueOffsets[1],
                                state.proxyIcons[s] + ProxyValueOffset, state.flagIcons[s] + state.valueOffsets[0]};
    const void* values[4] = {position, position, &icon, &icon};
    const size_t sizes[4] = {12, 12, 4, 4};
    int written = 0;
    for (; written < 4; ++written)
        if (!memory.Store(places[written], values[written], sizes[written]))
            break;
    bool same = written == 4;
    for (int i = 0; i < 4 && same; ++i)
    {
        uint8_t back[12];
        same = memory.Read(places[i], back, sizes[i]) && std::memcmp(back, values[i], sizes[i]) == 0;
    }
    if (!same)
    {
        // Undo what went in: the icons, then the positions
        for (int i = 3; i >= 0; --i)
            memory.Store(places[i], i >= 2 ? static_cast<const void*>(&emptyIcon) : static_cast<const void*>(emptyPosition),
                         sizes[i]);
        Append(result.details, "slot %d: %d of 4 writes done, then undone\n", chosen, written);
        return Fail(result, "the mark could not be written to the game");
    }

    result.ok = true;
    result.slot = chosen;
    result.icon = icon;
    Append(result.details, "slot %d: icon %d written at %.1f %.1f %.1f, both copies read back\n", chosen,
           static_cast<int>(icon), position[0], position[1], position[2]);
    return true;
}


namespace
{
    // Static: the app may run from the Album with little memory.
    constexpr size_t ProbeChunk = 0x100000;
    uint8_t s_ProbeChunk[ProbeChunk + 64];

    bool Near(uint64_t address, const std::vector<uint64_t>& objects, uint64_t span)
    {
        for (const uint64_t object : objects)
            if (address >= object && address < object + span)
                return true;
        return false;
    }

    // Goes through the heap a megabyte at a time (64 KB at a time where part is not mapped), calling `visit` with
    // each block read. Returns how many 64 KB blocks could not be read.
    template <typename Visit>
    int WalkHeap(Memory& memory, uint64_t heapBase, uint64_t heapSize, Visit visit)
    {
        int unreadable = 0;
        for (uint64_t chunk = 0; chunk < heapSize; chunk += ProbeChunk)
        {
            const uint64_t address = heapBase + chunk;
            const size_t size = static_cast<size_t>(std::min<uint64_t>(ProbeChunk, heapSize - chunk));
            if (memory.Read(address, s_ProbeChunk, size))
            {
                visit(address, s_ProbeChunk, size);
                continue;
            }
            for (size_t part = 0; part < size; part += 0x10000)
            {
                const size_t length = std::min<size_t>(0x10000, size - part);
                if (memory.Read(address + part, s_ProbeChunk, length))
                    visit(address + part, s_ProbeChunk, length);
                else
                    ++unreadable;
            }
        }
        return unreadable;
    }

    void Dump(Memory& memory, uint64_t at, int from, int to, std::string& out)
    {
        uint32_t words[8];
        for (int line = from; line < to; line += 32)
            if (memory.Read(at + line, words, sizeof(words)))
            {
                Append(out, "  %+5d:", line);
                for (const uint32_t word : words)
                    Append(out, " %08x", word);
                out += "\n";
            }
    }
}

bool MapMarks::Probe(Memory& memory, uint64_t mainBase, const Layout& layout, uint64_t heapBase, uint64_t heapSize,
                     Result& result)
{
    result = Result();
    State state;
    if (!Load(memory, mainBase, layout, state, result))
        return false;
    if (result.before.empty())
        return Fail(result, "the game's map has no mark to look for");
    if (heapSize == 0 || heapSize > 0x200000000ULL)
        return Fail(result, "the game's heap is not known");

    struct Pattern
    {
        uint32_t x, z;
        int32_t icon;
        int slot;
    };
    std::vector<Pattern> patterns;
    for (const Mark& mark : result.before)
    {
        Pattern pattern;
        std::memcpy(&pattern.x, &mark.position[0], 4);
        std::memcpy(&pattern.z, &mark.position[2], 4);
        pattern.icon = mark.icon;
        pattern.slot = mark.slot;
        patterns.push_back(pattern);
    }
    Append(result.details, "probe 2: heap %llx, %llu MB; %zu marks\n", static_cast<unsigned long long>(heapBase),
           static_cast<unsigned long long>(heapSize >> 20), patterns.size());

    // Pass 1: the x of each mark with its z within 32 bytes
    struct Found
    {
        uint64_t address;
        size_t pattern;
        int zAt;
    };
    std::vector<Found> found;
    constexpr size_t MaxFound = 4000;
    const int unreadable = WalkHeap(memory, heapBase, heapSize, [&](uint64_t address, const uint8_t* bytes, size_t size) {
        for (size_t offset = 0; offset + 4 <= size && found.size() < MaxFound; offset += 4)
        {
            uint32_t word;
            std::memcpy(&word, bytes + offset, 4);
            for (size_t k = 0; k < patterns.size(); ++k)
            {
                if (word != patterns[k].x)
                    continue;
                int zAt = 0;
                for (int d = -32; d <= 32 && zAt == 0; d += 4)
                {
                    const long at = static_cast<long>(offset) + d;
                    if (d == 0 || at < 0 || static_cast<size_t>(at) + 4 > size)
                        continue;
                    uint32_t other;
                    std::memcpy(&other, bytes + at, 4);
                    if (other == patterns[k].z)
                        zAt = d;
                }
                if (zAt != 0)
                    found.push_back({address + offset, k, zAt});
            }
        }
    });
    Append(result.details, "pass 1: %zu hits, %d unreadable 64 KB blocks\n", found.size(), unreadable);

    // Each hit: the mark's icon near it, and the object it is in (the nearest pointer into the code before it)
    std::vector<uint64_t> objects; // of the hits with the icon near, for pass 2
    uint64_t lastDumpedPage = ~0ULL;
    int lines = 0;
    for (size_t i = 0; i < found.size() && lines < 600; ++i)
    {
        const Found& hit = found[i];
        const Pattern& pattern = patterns[hit.pattern];
        const bool known = Near(hit.address, state.flagPositions, 0x48) || Near(hit.address, state.proxyPositions, 0x20);
        int iconAt = 0;
        uint32_t near[32]; // 64 bytes before and after
        if (!known && memory.Read(hit.address - 64, near, sizeof(near)))
            for (int d = 4; d <= 60 && iconAt == 0; d += 4)
            {
                if (static_cast<int32_t>(near[16 - d / 4]) == pattern.icon)
                    iconAt = -d;
                else if (d < 64 && 16 + d / 4 < 32 && static_cast<int32_t>(near[16 + d / 4]) == pattern.icon)
                    iconAt = d;
            }
        uint64_t object = 0, vtable = 0;
        uint64_t before[32]; // 256 bytes before
        const uint64_t aligned = hit.address & ~7ULL;
        if (!known && memory.Read(aligned - sizeof(before), before, sizeof(before)))
            for (int k = 31; k >= 0 && object == 0; --k)
                if (before[k] >= mainBase && before[k] < mainBase + 0x4000000)
                {
                    object = aligned - sizeof(before) + static_cast<uint64_t>(k) * 8;
                    vtable = before[k];
                }
        Append(result.details, "hit %zu: slot %d (icon %d) at %llx, z at %+d%s", i + 1, pattern.slot,
               static_cast<int>(pattern.icon), static_cast<unsigned long long>(hit.address), hit.zAt,
               known ? ", the flag or its copy\n" : "");
        ++lines;
        if (known)
            continue;
        if (iconAt != 0)
            Append(result.details, ", icon at %+d", iconAt);
        if (object != 0)
            Append(result.details, ", in an object at %+lld (main+%llx)",
                   static_cast<long long>(object) - static_cast<long long>(hit.address),
                   static_cast<unsigned long long>(vtable - mainBase));
        result.details += "\n";
        if (iconAt != 0 && object != 0 && objects.size() < 24 &&
            std::find(objects.begin(), objects.end(), object) == objects.end())
            objects.push_back(object);
        // The memory around: for the first hit of each 64 KB, and for every one with the icon near
        const uint64_t page = hit.address >> 16;
        if (page != lastDumpedPage || iconAt != 0)
        {
            lastDumpedPage = page;
            Dump(memory, hit.address, -128, 128, result.details);
            lines += 8;
        }
    }

    // Pass 2: who points at those objects (the list of the map screen's marks, if they are in one)
    if (!objects.empty())
    {
        struct Pointer
        {
            uint64_t address;
            size_t object;
        };
        std::vector<Pointer> pointers;
        WalkHeap(memory, heapBase, heapSize, [&](uint64_t address, const uint8_t* bytes, size_t size) {
            for (size_t offset = 0; offset + 8 <= size && pointers.size() < 200; offset += 8)
            {
                uint64_t word;
                std::memcpy(&word, bytes + offset, 8);
                for (size_t k = 0; k < objects.size(); ++k)
                    if (word == objects[k])
                        pointers.push_back({address + offset, k});
            }
        });
        Append(result.details, "pass 2: %zu objects, %zu pointers to them\n", objects.size(), pointers.size());
        for (size_t i = 0; i < objects.size(); ++i)
        {
            Append(result.details, "object %zu at %llx:\n", i + 1, static_cast<unsigned long long>(objects[i]));
            Dump(memory, objects[i], -64, 256, result.details);
        }
        for (const Pointer& pointer : pointers)
        {
            Append(result.details, "pointer to object %zu at %llx\n", pointer.object + 1,
                   static_cast<unsigned long long>(pointer.address));
            Dump(memory, pointer.address, -64, 64, result.details);
            uint64_t before[16];
            if (memory.Read(pointer.address - sizeof(before), before, sizeof(before)))
                for (int k = 15; k >= 0; --k)
                    if (before[k] >= mainBase && before[k] < mainBase + 0x4000000)
                    {
                        Append(result.details, "  main+%llx at %+d\n",
                               static_cast<unsigned long long>(before[k] - mainBase), (k - 16) * 8);
                        break;
                    }
        }
    }
    result.ok = true;
    return true;
}
