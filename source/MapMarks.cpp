#include "MapMarks.h"

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

    struct Slot
    {
        int32_t icon, proxyIcon;
        float position[3], proxyPosition[3];
    };

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

bool MapMarks::Add(Memory& memory, uint64_t mainBase, const Layout& layout, int32_t icon, const float position[3],
                   Result& result)
{
    result = Result();
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(position[i]) || std::fabs(position[i]) > 6000.0f)
            return Fail(result, "the place is outside the map");

    const Layout::Kind& s32 = layout.kinds[static_cast<int>(LiveGameData::FlagKind::S32)];
    const Layout::Kind& vec3 = layout.kinds[static_cast<int>(LiveGameData::FlagKind::Vector3f)];
    const uint64_t s32Vtable = mainBase + s32.vtable, vec3Vtable = mainBase + vec3.vtable;

    uint64_t manager = 0, buffers[2] = {0, 0};
    if (!memory.Read(mainBase + layout.managerPointer, &manager, sizeof(manager)) || manager == 0 ||
        !memory.Read(manager + layout.flagBufferOffset, &buffers[0], sizeof(buffers[0])) ||
        !memory.Read(manager + layout.flagBuffer1Offset, &buffers[1], sizeof(buffers[1])))
        return Fail(result, "the game data is not ready yet");

    // The buffer of Flag<T> objects is the one whose arrays have their vtables; the other holds the proxies.
    std::vector<uint64_t> flagIcons, flagPositions, proxyIcons, proxyPositions;
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
    std::vector<Slot> slots(count);
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
    bool pinUsed[LastPin - FirstPin + 1] = {};
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
                pinUsed[used - FirstPin] = true;
    }

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
    const uint64_t places[4] = {proxyPositions[s] + ProxyValueOffset, flagPositions[s] + vec3.valueOffset,
                                proxyIcons[s] + ProxyValueOffset, flagIcons[s] + s32.valueOffset};
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
