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

    std::map<uint64_t, std::vector<uint8_t>> pages;
    int readCount = 0;
};

struct FakeFlag
{
    LiveGameData::FlagKind kind;
    uint32_t hash;
    uint8_t value[32];
};

// Builds what LiveGameData reads: Manager -> flag buffer -> pointer tables -> flag objects.
class FakeGame
{
public:
    FakeGameMemory memory;
    uint64_t mainBase = 0x7100004000ULL;
    uint64_t manager = 0x2100000000ULL;
    uint64_t flagBuffer = 0x2100100000ULL;
    std::pair<uint64_t, size_t> lastBuffer[4]; // per kind: where its last buffer of objects is

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
        memory.Put<uint64_t>(manager + layout.flagBufferRef, manager + layout.flagBufferOffset);

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
                const size_t valueSize = kind == 0 ? 1 : kind == 1 ? 4 : kind == 2 ? 32 : 12;
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
