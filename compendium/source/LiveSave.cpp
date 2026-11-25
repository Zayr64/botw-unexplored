#include "LiveSave.h"

#include <algorithm>
#include <cstring>

#include "CookData.h"
#include "SaveFile.h"

namespace Compendium
{
    namespace
    {
        // The pouch: the actor names, their counts (durability for weapons) and whether each is equipped.
        const uint32_t s_Arrays[3] = {Crc32("PorchItem"), Crc32("PorchItem_Value1"), Crc32("PorchItem_EquipFlag")};
        // The play time in seconds: 0 until the game has loaded a save (the hash BotW Unexplored's SaveParser uses).
        constexpr uint32_t s_Playtime = 0x73C29681;

        int ArrayIndex(uint32_t hash)
        {
            for (int i = 0; i < 3; i++)
                if (s_Arrays[i] == hash)
                    return i;
            return -1;
        }

        void PutU32(std::vector<uint8_t>& out, uint32_t value)
        {
            for (int i = 0; i < 4; i++)
                out.push_back(static_cast<uint8_t>(value >> (8 * i)));
        }
    }

    const uint32_t* LiveArrays()
    {
        return s_Arrays;
    }

    int LiveArrayCount()
    {
        return 3;
    }

    void LiveSave::OnFlag(LiveGameData::FlagKind kind, uint32_t hash, const uint8_t* value, uint64_t)
    {
        // The elements of arrays are flag objects too and may be met among the single flags: the pouch's come from
        // OnArrayElement, in order, and only from there.
        if (ArrayIndex(hash) >= 0)
            return;
        Record record = {hash, 0};
        switch (kind)
        {
        case LiveGameData::FlagKind::Bool:
            record.value = (value[0] & 1) ? 1u : 0u;
            break;
        case LiveGameData::FlagKind::S32:
        case LiveGameData::FlagKind::F32: // the save keeps the bits of the float
            std::memcpy(&record.value, value, sizeof(record.value));
            break;
        default:
            return; // texts and vectors: not used by this app
        }
        if (hash == s_Playtime && kind == LiveGameData::FlagKind::S32 && record.value > m_playtime)
            m_playtime = record.value;
        m_records.push_back(record);
    }

    void LiveSave::OnArrayElement(LiveGameData::FlagKind kind, uint32_t hash, int, const uint8_t* value)
    {
        const int array = ArrayIndex(hash);
        if (array < 0)
            return;
        if (kind == LiveGameData::FlagKind::String64)
        {
            // 64 characters, 4 to a record, in order (what SaveFile::ReadPouch reads back).
            for (size_t k = 0; k < PouchNameLength / 4; k++)
            {
                Record record = {hash, 0};
                std::memcpy(&record.value, value + k * 4, 4);
                m_records.push_back(record);
            }
        }
        else
        {
            Record record = {hash, 0};
            if (kind == LiveGameData::FlagKind::Bool)
                record.value = (value[0] & 1) ? 1u : 0u;
            else
                std::memcpy(&record.value, value, sizeof(record.value));
            m_records.push_back(record);
        }
        ++m_arrayElements[array];
    }

    bool LiveSave::Build(std::vector<uint8_t>& out, std::string& why) const
    {
        out.clear();
        if (m_playtime == 0)
        {
            why = "the game has not loaded a save yet";
            return false;
        }
        for (int i = 0; i < 3; i++)
            if (m_arrayElements[i] != PouchSize)
            {
                why = "the pouch could not be read";
                return false;
            }

        // Sorted by hash like the game's file; the records of one hash keep their order (stable).
        std::vector<Record> records(m_records);
        std::stable_sort(records.begin(), records.end(),
                         [](const Record& a, const Record& b) { return a.hash < b.hash; });

        out.reserve(12 + records.size() * 8 + 4);
        // The header of a 1.6.0 save (version 0x471E), then the records, then the end marker.
        PutU32(out, 0x0000471E);
        PutU32(out, 0xFFFFFFFF);
        PutU32(out, 0x00000001);
        for (const Record& r : records)
        {
            PutU32(out, r.hash);
            PutU32(out, r.value);
        }
        PutU32(out, 0xFFFFFFFF);
        why.clear();
        return true;
    }
}
