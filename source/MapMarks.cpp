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
    // zayr35: the marks as the map screen holds them (BotW 1.6.0, main 8E9978D50BDD20B4). Where and what, from
    // igawa6/eden-duo-companions (native/modules/botw_map_offsets.h, botw_map_pins.h, botw_map_special.h), a module of
    // the Eden emulator that reads and edits these marks; the dumps of 26/11 on this console agree with it (the three
    // vtables of the marks, the size of a pin and of a stamp, the index of a pin).
    constexpr uint64_t ScreenManagerPointer = 0x2CA63B0; // main+: the map screen's manager of marks
    constexpr uint64_t ScreenManagerVtable = 0x2B86848;
    constexpr uint64_t ScreenControlPointer = 0x2CA63B8; // main+: the screen's control, which points at the manager
    constexpr uint64_t ScreenControlVtable = 0x2B8AAD8;
    constexpr uint64_t ControlManagerOffset = 0x8D8;
    constexpr uint64_t MedallionOffset = 0x480; // the Travel Medallion's mark (icon 42)
    constexpr uint64_t StampsOffset = 0x610;    // sead::PtrArray of the 100 stamps
    constexpr uint64_t FreeStampOffset = 0x650; // the first stamp not on the map
    constexpr uint64_t PinsOffset = 0x658;      // sead::PtrArray of the 5 pins
    constexpr uint64_t ProjectionOffset = 0x878, Projection2Offset = 0x8B0;
    constexpr uint64_t PinVtable = 0x2B866B0, StampVtable = 0x2B874F0, MedallionVtable = 0x2B875E8;
    constexpr int ScreenPins = 5, ScreenStamps = 100;
    constexpr size_t PinSize = 0x80, StampSize = 0x50, MedallionRead = 0x100, RecordRead = 0x100;
    // A mark: +0x08 the record the game makes to show it (null: not on the map), +0x1C its icon, +0x20 and +0x24 its
    // cell in a grid of 250 units, +0x28 where it is, +0x3C state bits (0x10: on the map), +0x40 its index
    constexpr size_t RecordAt = 0x08, IconAt = 0x1C, CellAt = 0x20, PositionAt = 0x28, StateAt = 0x3C, IndexAt = 0x40;
    constexpr uint32_t StateShown = 0x10;
    constexpr uint64_t MainSpan = 0x4000000;

    struct ScreenMark
    {
        uint8_t bytes[MedallionRead];
        uint64_t vtable, record;
        int32_t icon, cell[2], index;
        float position[3];
        uint32_t state;
    };

    bool ReadScreenMark(Memory& memory, uint64_t address, size_t size, ScreenMark& mark)
    {
        std::memset(&mark, 0, sizeof(mark));
        if (address == 0 || size > sizeof(mark.bytes) || !memory.Read(address, mark.bytes, size))
            return false;
        mark.vtable = U64(mark.bytes);
        mark.record = U64(mark.bytes + RecordAt);
        std::memcpy(&mark.icon, mark.bytes + IconAt, 4);
        std::memcpy(mark.cell, mark.bytes + CellAt, 8);
        std::memcpy(mark.position, mark.bytes + PositionAt, 12);
        std::memcpy(&mark.state, mark.bytes + StateAt, 4);
        std::memcpy(&mark.index, mark.bytes + IndexAt, 4);
        return true;
    }

    // main+X for a pointer into the game's code, the address otherwise
    std::string Where(uint64_t value, uint64_t mainBase)
    {
        char text[40];
        if (value >= mainBase && value < mainBase + MainSpan)
            std::snprintf(text, sizeof(text), "main+%llx", static_cast<unsigned long long>(value - mainBase));
        else
            std::snprintf(text, sizeof(text), "%llx", static_cast<unsigned long long>(value));
        return text;
    }

    void DumpBytes(const uint8_t* bytes, size_t size, std::string& out)
    {
        for (size_t line = 0; line < size; line += 32)
        {
            Append(out, "  +%03zx:", line);
            for (size_t word = line; word < line + 32 && word + 4 <= size; word += 4)
            {
                uint32_t value;
                std::memcpy(&value, bytes + word, 4);
                Append(out, " %08x", value);
            }
            out += "\n";
        }
    }

    // One line for a mark, its bytes when asked, and the record it points at.
    void DescribeMark(Memory& memory, uint64_t mainBase, const char* kind, int number, uint64_t address,
                      const ScreenMark& mark, size_t size, bool dump, std::string& out)
    {
        if (number >= 0)
            Append(out, "%s %d", kind, number);
        else
            out += kind;
        Append(out, " at %llx: vtable %s, icon %d, cell %d %d, at %.2f %.2f %.2f, state 0x%x, index %d, record %llx",
               static_cast<unsigned long long>(address), Where(mark.vtable, mainBase).c_str(), static_cast<int>(mark.icon),
               static_cast<int>(mark.cell[0]), static_cast<int>(mark.cell[1]), mark.position[0], mark.position[1],
               mark.position[2], static_cast<unsigned>(mark.state), static_cast<int>(mark.index),
               static_cast<unsigned long long>(mark.record));
        if (mark.state & StateShown)
        {
            const int cellX = static_cast<int>(std::floor((mark.position[0] + 5000.0f) / 250.0f));
            const int cellZ = static_cast<int>(std::floor((mark.position[2] + 4000.0f) / 250.0f));
            if (cellX != mark.cell[0] || cellZ != mark.cell[1])
                Append(out, " (the grid of 250 gives %d %d)", cellX, cellZ);
        }
        out += "\n";
        if (dump)
            DumpBytes(mark.bytes, size, out);
        if (mark.record == 0)
            return;
        uint8_t record[RecordRead];
        if (!memory.Read(mark.record, record, sizeof(record)))
        {
            Append(out, "  record at %llx: could not be read\n", static_cast<unsigned long long>(mark.record));
            return;
        }
        Append(out, "  record at %llx: first word %s\n", static_cast<unsigned long long>(mark.record),
               Where(U64(record), mainBase).c_str());
        DumpBytes(record, sizeof(record), out);
    }

    struct Shown
    {
        std::string name; // "pin 1", "stamp 0", "the medallion"
        int32_t icon;
        float position[3];
        bool inFlags;
    };
}

bool MapMarks::ReadScreen(Memory& memory, uint64_t mainBase, const Layout& layout, Result& result)
{
    result = Result();
    State state;
    const bool flagsRead = Load(memory, mainBase, layout, state, result);
    if (!flagsRead)
        result.details += "(the flags could not be read: the screen alone)\n";
    const std::vector<Mark> flags = result.before;

    int failed = 0;
    auto check = [&](bool passed, const std::string& what) {
        if (!passed)
        {
            ++failed;
            Append(result.details, "check failed: %s\n", what.c_str());
        }
        return passed;
    };

    uint64_t manager = 0, managerVtable = 0;
    if (!memory.Read(mainBase + ScreenManagerPointer, &manager, sizeof(manager)) || manager == 0 ||
        !memory.Read(manager, &managerVtable, sizeof(managerVtable)))
        return Fail(result, "the game's map screen was not found");
    Append(result.details, "map screen: manager at %llx, vtable %s\n", static_cast<unsigned long long>(manager),
           Where(managerVtable, mainBase).c_str());
    if (managerVtable != mainBase + ScreenManagerVtable)
        return Fail(result, "the game's map screen does not look as expected");

    // The screen's control and the map's projection: only told, nothing depends on them
    uint64_t control = 0, controlVtable = 0, controlManager = 0;
    if (memory.Read(mainBase + ScreenControlPointer, &control, sizeof(control)) && control != 0 &&
        memory.Read(control, &controlVtable, sizeof(controlVtable)) &&
        memory.Read(control + ControlManagerOffset, &controlManager, sizeof(controlManager)))
        Append(result.details, "control at %llx: vtable %s (expected main+%llx), +0x8d8 the manager: %s\n",
               static_cast<unsigned long long>(control), Where(controlVtable, mainBase).c_str(),
               static_cast<unsigned long long>(ScreenControlVtable), controlManager == manager ? "yes" : "no");
    else
        Append(result.details, "control: not there (%llx)\n", static_cast<unsigned long long>(control));
    float projection[6];
    if (memory.Read(manager + ProjectionOffset, projection, 16) && memory.Read(manager + Projection2Offset, projection + 4, 8))
        Append(result.details, "projection: %g %g %g %g; %g %g\n", projection[0], projection[1], projection[2],
               projection[3], projection[4], projection[5]);

    std::vector<Shown> shown;
    auto note = [&](const std::string& name, const ScreenMark& mark) {
        if (mark.state & StateShown)
            shown.push_back({name, mark.icon, {mark.position[0], mark.position[1], mark.position[2]}, false});
    };

    // The five pins
    PtrArray pins = {};
    std::vector<uint64_t> pinList;
    if (check(memory.Read(manager + PinsOffset, &pins, sizeof(pins)), "the pins could not be read") &&
        check(pins.size == ScreenPins && pins.capacity == ScreenPins && pins.data != 0, "there are not five pins"))
    {
        pinList.assign(ScreenPins, 0);
        if (!check(memory.Read(pins.data, pinList.data(), pinList.size() * 8), "the pins' pointers could not be read"))
            pinList.clear();
    }
    Append(result.details, "pins: %d of %d at %llx\n", static_cast<int>(pins.size), static_cast<int>(pins.capacity),
           static_cast<unsigned long long>(pins.data));
    for (size_t i = 0; i < pinList.size(); ++i)
    {
        const std::string name = "pin " + std::to_string(i);
        ScreenMark mark;
        if (!check(ReadScreenMark(memory, pinList[i], PinSize, mark), name + " could not be read"))
            continue;
        check(mark.vtable == mainBase + PinVtable, name + "'s vtable");
        check(mark.index == static_cast<int32_t>(i), name + "'s index");
        DescribeMark(memory, mainBase, "pin", static_cast<int>(i), pinList[i], mark, PinSize, true, result.details);
        note(name, mark);
    }

    // The stamps: those on the map or ever used, the first free one
    PtrArray stamps = {};
    std::vector<uint64_t> stampList;
    if (check(memory.Read(manager + StampsOffset, &stamps, sizeof(stamps)), "the stamps could not be read") &&
        check(stamps.size == ScreenStamps && stamps.capacity == ScreenStamps && stamps.data != 0,
              "there are not 100 stamps"))
    {
        stampList.assign(ScreenStamps, 0);
        if (!check(memory.Read(stamps.data, stampList.data(), stampList.size() * 8),
                   "the stamps' pointers could not be read"))
            stampList.clear();
    }
    uint64_t freeStamp = 0;
    memory.Read(manager + FreeStampOffset, &freeStamp, sizeof(freeStamp));
    std::string stampLines;
    int onMap = 0, freeIndex = -1, firstNotShown = -1, badVtables = 0;
    for (size_t j = 0; j < stampList.size(); ++j)
    {
        ScreenMark mark;
        if (!ReadScreenMark(memory, stampList[j], StampSize, mark) || mark.vtable != mainBase + StampVtable)
        {
            ++badVtables;
            continue;
        }
        const bool isShown = (mark.state & StateShown) != 0;
        if (isShown)
            ++onMap;
        else if (firstNotShown < 0)
            firstNotShown = static_cast<int>(j);
        if (stampList[j] == freeStamp)
            freeIndex = static_cast<int>(j);
        if (isShown || mark.record != 0 || mark.state != 0 || mark.position[0] > EmptyX)
            DescribeMark(memory, mainBase, "stamp", static_cast<int>(j), stampList[j], mark, StampSize,
                         isShown || mark.record != 0, stampLines);
        note("stamp " + std::to_string(j), mark);
    }
    check(badVtables == 0, std::to_string(badVtables) + " stamps could not be read or have another vtable");
    Append(result.details, "stamps: %d of %d at %llx; %d on the map; the first free one (%llx) is stamp %d, the first "
                           "not on the map stamp %d\n",
           static_cast<int>(stamps.size), static_cast<int>(stamps.capacity), static_cast<unsigned long long>(stamps.data),
           onMap, static_cast<unsigned long long>(freeStamp), freeIndex, firstNotShown);
    result.details += stampLines;

    // The Travel Medallion
    uint64_t medallion = 0;
    ScreenMark mark;
    if (check(memory.Read(manager + MedallionOffset, &medallion, sizeof(medallion)) &&
                  ReadScreenMark(memory, medallion, MedallionRead, mark),
              "the medallion could not be read"))
    {
        check(mark.vtable == mainBase + MedallionVtable, "the medallion's vtable");
        check(mark.icon == 42, "the medallion's icon");
        DescribeMark(memory, mainBase, "the medallion", -1, medallion, mark, MedallionRead, true, result.details);
        note("the medallion", mark);
    }

    // Which marks of the flags the screen has (a pin the app wrote shows after a loading)
    if (flagsRead)
    {
        for (const Mark& flag : flags)
        {
            const Shown* match = nullptr;
            for (Shown& screen : shown)
                if (!screen.inFlags && screen.icon == flag.icon && std::fabs(screen.position[0] - flag.position[0]) < 0.5f &&
                    std::fabs(screen.position[2] - flag.position[2]) < 0.5f)
                {
                    screen.inFlags = true;
                    match = &screen;
                    break;
                }
            Append(result.details, "flag slot %d (icon %d): %s%s\n", flag.slot, static_cast<int>(flag.icon),
                   match ? "on the map screen as " : "not on the map screen", match ? match->name.c_str() : "");
        }
        for (const Shown& screen : shown)
            if (!screen.inFlags)
                Append(result.details, "%s (icon %d): on the map screen, not in the flags\n", screen.name.c_str(),
                       static_cast<int>(screen.icon));
    }
    if (failed == 0)
        result.details += "checks: all passed\n";
    else
        Append(result.details, "checks: %d failed\n", failed);
    result.failedChecks = failed;
    result.error.clear();
    result.ok = true;
    return true;
}
