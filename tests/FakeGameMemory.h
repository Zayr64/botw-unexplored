#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "LiveGameData.h"

// A sparse fake of the game's address space, in 4 KB pages: a read fails unless every page it touches
// is mapped, like the debug service of the console.
class FakeGameMemory : public LiveGameData::Memory
{
public:
    static constexpr uint64_t PageSize = 0x1000;

    bool Read(uint64_t address, void* out, size_t size) override
    {
        ++readCount;
        uint8_t* destination = static_cast<uint8_t*>(out);
        while (size > 0)
        {
            const std::map<uint64_t, std::vector<uint8_t>>::const_iterator page = pages.find(address / PageSize);
            if (page == pages.end())
                return false;
            const uint64_t offset = address % PageSize;
            const size_t count = static_cast<size_t>(std::min<uint64_t>(size, PageSize - offset));
            std::memcpy(destination, page->second.data() + offset, count);
            destination += count;
            address += count;
            size -= count;
        }
        return true;
    }

    void Map(uint64_t address, size_t size)
    {
        for (uint64_t page = address / PageSize; page <= (address + size - 1) / PageSize; ++page)
            if (pages.find(page) == pages.end())
                pages[page].assign(PageSize, 0);
    }

    void Unmap(uint64_t address, size_t size)
    {
        for (uint64_t page = address / PageSize; page <= (address + size - 1) / PageSize; ++page)
            pages.erase(page);
    }

    void Write(uint64_t address, const void* data, size_t size)
    {
        const uint8_t* source = static_cast<const uint8_t*>(data);
        Map(address, size);
        while (size > 0)
        {
            const uint64_t offset = address % PageSize;
            const size_t count = static_cast<size_t>(std::min<uint64_t>(size, PageSize - offset));
            std::memcpy(pages[address / PageSize].data() + offset, source, count);
            source += count;
            address += count;
            size -= count;
        }
    }

    template <typename T>
    void Put(uint64_t address, T value) { Write(address, &value, sizeof(value)); }

    // A write of the app (MapMarks): only to mapped memory, like the cheat service; `storeFails` makes the n-th fail.
    bool Store(uint64_t address, const void* data, size_t size) override
    {
        ++storeCount;
        if (storeFails > 0 && storeCount == storeFails)
            return false;
        for (uint64_t page = address / PageSize; page <= (address + size - 1) / PageSize; ++page)
            if (pages.find(page) == pages.end())
                return false;
        Write(address, data, size);
        return true;
    }

    std::map<uint64_t, std::vector<uint8_t>> pages;
    int readCount = 0;
    int storeCount = 0;
    int storeFails = 0;
};

struct FakeFlag
{
    LiveGameData::FlagKind kind;
    uint32_t hash;
    uint8_t value[64];
};

// Bytes of the value of each kind, as the tests write and compare them.
inline size_t FakeValueSize(LiveGameData::FlagKind kind)
{
    switch (kind)
    {
    case LiveGameData::FlagKind::Bool: return 1;
    case LiveGameData::FlagKind::S32: return 4;
    case LiveGameData::FlagKind::String32: return 32;
    case LiveGameData::FlagKind::Vector3f: return 12;
    case LiveGameData::FlagKind::F32: return 4;
    case LiveGameData::FlagKind::String64: return 64;
    default: return 0;
    }
}

// An array flag: every element is a Flag<T> object with the array's hash.
struct FakeArray
{
    LiveGameData::FlagKind kind;
    uint32_t hash;
    std::vector<std::vector<uint8_t>> values; // FakeValueSize(kind) bytes each
};

// Builds what LiveGameData reads: Manager -> flag buffer -> pointer tables -> flag objects.
class FakeGame
{
public:
    FakeGameMemory memory;
    uint64_t mainBase = 0x7100004000ULL;
    uint64_t manager = 0x2100000000ULL;
    uint64_t flagBuffer = 0x2100100000ULL;
    uint64_t proxyBuffer = 0x2100200000ULL; // tables that point at objects that are not Flag<T>
    std::pair<uint64_t, size_t> lastBuffer[static_cast<int>(LiveGameData::FlagKind::Count)]; // per kind: where its last buffer of objects is

    // Flags are stored in the order given, in buffers of `perBuffer` objects with unmapped memory between
    // them; the tables are sorted by hash like the game's. `vtableShift` makes the objects of a kind look
    // like something else. `listed` false keeps a flag out of the tables (array flags use the same objects).
    void Build(const LiveGameData::Layout& layout, const std::vector<FakeFlag>& flags, size_t perBuffer = 1000,
               uint64_t vtableShift = 0, const std::vector<bool>* listed = nullptr)
    {
        const int kindCount = static_cast<int>(LiveGameData::FlagKind::Count);
        memory.Map((mainBase + layout.managerPointer) & ~0xFFFULL, 0x1000);
        memory.Map((mainBase + layout.playerPointer) & ~0xFFFULL, 0x1000);
        memory.Map(manager, 0x1000);
        memory.Map(flagBuffer, 0x400);
        memory.Put<uint64_t>(mainBase + layout.managerPointer, manager);
        memory.Put<uint64_t>(manager + layout.flagBufferOffset, flagBuffer);
        memory.Put<uint64_t>(manager + layout.flagBuffer1Offset, proxyBuffer);
        memory.Put<uint64_t>(manager + layout.flagBufferRef, manager + layout.flagBufferOffset);

        // The second buffer: every table with a few "proxies" (another vtable, no hash of their own).
        const uint64_t proxies = 0x2110000000ULL, proxyTable = 0x2120000000ULL;
        memory.Map(proxyBuffer, 0x400);
        memory.Map(proxies, 0x1000);
        memory.Map(proxyTable, 0x100);
        for (int i = 0; i < 16; ++i)
        {
            memory.Put<uint64_t>(proxies + i * 0x18, mainBase + 0x2BD0000);
            memory.Put<uint64_t>(proxyTable + i * 8, proxies + i * 0x18);
        }
        for (int table = 0; table < 9; ++table)
        {
            const uint64_t header = proxyBuffer + layout.tablesOffset + static_cast<uint64_t>(table) * 16;
            memory.Put<int32_t>(header, 16);
            memory.Put<int32_t>(header + 4, 16);
            memory.Put<uint64_t>(header + 8, proxyTable);
        }

        for (int kind = 0; kind < kindCount; ++kind)
        {
            const LiveGameData::Layout::Kind& info = layout.kinds[kind];
            std::vector<std::pair<uint32_t, uint64_t>> entries; // hash, address
            // The first buffer ends on a 64 KB boundary with one object across it.
            uint64_t address = 0x2300000000ULL + static_cast<uint64_t>(kind) * 0x100000000ULL + 0x10000 - 8 -
                               (static_cast<uint64_t>(std::min<size_t>(perBuffer, 3)) - 1) * info.size;
            size_t inBuffer = 0;
            uint64_t bufferStart = 0;
            std::vector<uint8_t> bytes;
            const auto flush = [&]() {
                if (bytes.empty())
                    return;
                memory.Write(bufferStart, bytes.data(), bytes.size());
                lastBuffer[kind] = std::make_pair(bufferStart, bytes.size());
                bytes.clear();
            };
            for (size_t i = 0; i < flags.size(); ++i)
            {
                if (static_cast<int>(flags[i].kind) != kind)
                    continue;
                if (inBuffer == 0)
                    bufferStart = address;
                std::vector<uint8_t> object(info.size, 0);
                const uint64_t vtable = mainBase + info.vtable + vtableShift;
                std::memcpy(object.data(), &vtable, 8);
                std::memcpy(object.data() + info.hashOffset, &flags[i].hash, 4);
                const size_t valueSize = FakeValueSize(static_cast<LiveGameData::FlagKind>(kind));
                std::memcpy(object.data() + info.valueOffset, flags[i].value, valueSize);
                bytes.insert(bytes.end(), object.begin(), object.end());
                if (!listed || (*listed)[i])
                    entries.push_back(std::make_pair(flags[i].hash, address));
                address += info.size;
                if (++inBuffer == perBuffer)
                {
                    flush();
                    inBuffer = 0;
                    address += 0x3000 + (entries.size() % 7) * 0x1000; // at least one unmapped page
                    address &= ~7ULL;
                }
            }
            flush();

            std::stable_sort(entries.begin(), entries.end(),
                             [](const std::pair<uint32_t, uint64_t>& a, const std::pair<uint32_t, uint64_t>& b) {
                                 return a.first < b.first;
                             });
            const uint64_t table = 0x2200000000ULL + static_cast<uint64_t>(kind) * 0x10000000ULL;
            memory.Map(table, entries.size() * 8 + 8);
            for (size_t i = 0; i < entries.size(); ++i)
                memory.Put<uint64_t>(table + i * 8, entries[i].second);
            const uint64_t header = flagBuffer + layout.tablesOffset + static_cast<uint64_t>(info.table) * 16;
            memory.Put<int32_t>(header, static_cast<int32_t>(entries.size()));
            memory.Put<int32_t>(header + 4, static_cast<int32_t>(entries.size()));
            memory.Put<uint64_t>(header + 8, table);
        }
    }

    // Per array hash: its Flag<T> objects and the proxies that stand for them (FlagProxy: vtable, the flag, the value).
    std::map<uint32_t, std::vector<uint64_t>> arrayFlags, arrayProxies;
    static constexpr uint64_t ProxyValueOffset = 0x10;

    // The tables of array flags of the flag buffer: one table per kind present, each entry a PtrArray of the
    // elements, whose objects lie one after the other; and the same tables in the proxy buffer, whose arrays hold
    // proxies with their own copy of each value (decomp: TriggerParam::copyAllFlags). Call after Build.
    // `vtableShift` as in Build.
    void BuildArrays(const LiveGameData::Layout& layout, const std::vector<FakeArray>& arrays, uint64_t vtableShift = 0)
    {
        const int kindCount = static_cast<int>(LiveGameData::FlagKind::Count);
        uint64_t objects = 0x3200000000ULL; // away from the objects of Build (0x23.. to 0x28..)
        uint64_t headers = 0x3100000000ULL;
        uint64_t proxies = 0x3400000000ULL, proxyHeaders = 0x3300000000ULL;
        for (int kind = 0; kind < kindCount; ++kind)
        {
            const LiveGameData::Layout::Kind& info = layout.kinds[kind];
            std::vector<uint64_t> entries, proxyEntries;
            for (const FakeArray& array : arrays)
            {
                if (static_cast<int>(array.kind) != kind)
                    continue;
                const uint64_t elements = headers + 0x10, proxyElements = proxyHeaders + 0x10;
                const size_t valueSize = std::min(FakeValueSize(array.kind), static_cast<size_t>(16));
                memory.Map(headers, 0x10 + array.values.size() * 8);
                memory.Put<int32_t>(headers, static_cast<int32_t>(array.values.size()));
                memory.Put<int32_t>(headers + 4, static_cast<int32_t>(array.values.size()));
                memory.Put<uint64_t>(headers + 8, elements);
                memory.Map(proxyHeaders, 0x10 + array.values.size() * 8);
                memory.Put<int32_t>(proxyHeaders, static_cast<int32_t>(array.values.size()));
                memory.Put<int32_t>(proxyHeaders + 4, static_cast<int32_t>(array.values.size()));
                memory.Put<uint64_t>(proxyHeaders + 8, proxyElements);
                for (size_t i = 0; i < array.values.size(); ++i)
                {
                    std::vector<uint8_t> object(info.size, 0);
                    const uint64_t vtable = mainBase + info.vtable + vtableShift;
                    std::memcpy(object.data(), &vtable, 8);
                    std::memcpy(object.data() + info.hashOffset, &array.hash, 4);
                    std::memcpy(object.data() + info.valueOffset, array.values[i].data(),
                                std::min(array.values[i].size(), FakeValueSize(array.kind)));
                    memory.Write(objects, object.data(), object.size());
                    memory.Put<uint64_t>(elements + i * 8, objects);
                    arrayFlags[array.hash].push_back(objects);

                    std::vector<uint8_t> proxy(0x20, 0);
                    const uint64_t proxyVtable = mainBase + 0x2BD0000;
                    std::memcpy(proxy.data(), &proxyVtable, 8);
                    std::memcpy(proxy.data() + 8, &objects, 8);
                    std::memcpy(proxy.data() + ProxyValueOffset, array.values[i].data(),
                                std::min(array.values[i].size(), valueSize));
                    memory.Write(proxies, proxy.data(), proxy.size());
                    memory.Put<uint64_t>(proxyElements + i * 8, proxies);
                    arrayProxies[array.hash].push_back(proxies);
                    objects += info.size;
                    proxies += proxy.size();
                }
                entries.push_back(headers);
                proxyEntries.push_back(proxyHeaders);
                headers += (0x10 + array.values.size() * 8 + 0xFFF) & ~0xFFFULL;
                proxyHeaders += (0x10 + array.values.size() * 8 + 0xFFF) & ~0xFFFULL;
                objects = (objects + 0x1FFF) & ~0xFFFULL; // a page between the arrays
                proxies = (proxies + 0x1FFF) & ~0xFFFULL;
            }
            for (int which = 0; which < 2; ++which)
            {
                const std::vector<uint64_t>& list = which == 0 ? entries : proxyEntries;
                const uint64_t table = (which == 0 ? 0x3000000000ULL : 0x3500000000ULL) + static_cast<uint64_t>(kind) * 0x10000000ULL;
                const uint64_t header = (which == 0 ? flagBuffer : proxyBuffer) + layout.arrayTablesOffset +
                                        static_cast<uint64_t>(info.table) * 16;
                memory.Map(table, list.size() * 8 + 8);
                for (size_t i = 0; i < list.size(); ++i)
                    memory.Put<uint64_t>(table + i * 8, list[i]);
                memory.Put<int32_t>(header, static_cast<int32_t>(list.size()));
                memory.Put<int32_t>(header + 4, static_cast<int32_t>(list.size()));
                memory.Put<uint64_t>(header + 8, table);
            }
        }
    }

    void SetPlayerPosition(const LiveGameData::Layout& layout, float x, float y, float z)
    {
        const uint64_t player = 0x2000000000ULL, actor = 0x2000100000ULL;
        memory.Map(player, 0x100);
        memory.Map(actor, 0x1000);
        memory.Put<uint64_t>(mainBase + layout.playerPointer, player);
        memory.Put<uint64_t>(player + layout.playerActorOffset, actor);
        const float matrix[12] = {1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z};
        memory.Write(actor + layout.playerMatrixOffset, matrix, sizeof(matrix));
    }
};
