#include "SaveFile.h"

#include <cstring>

#include "CookData.h"

namespace Compendium
{
    namespace
    {
        constexpr size_t HeaderSize = 0x0C;
        constexpr size_t RecordSize = 8;

        uint32_t ReadU32(const uint8_t* p)
        {
            return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                   (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
        }
    }

    bool SaveFile::Load(const uint8_t* data, size_t size)
    {
        m_records = nullptr;
        m_count = 0;
        if (!data || size < HeaderSize + RecordSize * 32)
            return false;
        m_records = data + HeaderSize;
        // The 4-byte end marker is half a record, so the division leaves it out.
        m_count = (size - HeaderSize) / RecordSize;
        m_sorted = true;
        for (size_t i = 1; i < m_count && m_sorted; i++)
            m_sorted = Hash(i - 1) <= Hash(i);
        return true;
    }

    uint32_t SaveFile::Hash(size_t record) const
    {
        return ReadU32(m_records + record * RecordSize);
    }

    uint32_t SaveFile::Value(size_t record) const
    {
        return ReadU32(m_records + record * RecordSize + 4);
    }

    long SaveFile::Find(uint32_t hash) const
    {
        if (!m_sorted)
        {
            for (size_t i = 0; i < m_count; i++)
                if (Hash(i) == hash)
                    return static_cast<long>(i);
            return -1;
        }
        size_t lo = 0;
        size_t hi = m_count;
        while (lo < hi)
        {
            const size_t mid = lo + (hi - lo) / 2;
            if (Hash(mid) < hash)
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo < m_count && Hash(lo) == hash ? static_cast<long>(lo) : -1;
    }

    bool SaveFile::Has(uint32_t hash) const
    {
        return Find(hash) >= 0;
    }

    bool SaveFile::GetS32(uint32_t hash, int32_t& out) const
    {
        const long i = Find(hash);
        if (i < 0)
            return false;
        const uint32_t v = Value(static_cast<size_t>(i));
        memcpy(&out, &v, sizeof(out));
        return true;
    }

    bool SaveFile::GetS32(const char* name, int32_t& out) const
    {
        return GetS32(Crc32(name), out);
    }

    bool SaveFile::GetBool(uint32_t hash, bool& out) const
    {
        const long i = Find(hash);
        if (i < 0)
            return false;
        out = Value(static_cast<size_t>(i)) != 0;
        return true;
    }

    bool SaveFile::GetBool(const char* name, bool& out) const
    {
        return GetBool(Crc32(name), out);
    }

    bool SaveFile::GetF32(const char* name, float& out) const
    {
        const long i = Find(Crc32(name));
        if (i < 0)
            return false;
        const uint32_t v = Value(static_cast<size_t>(i));
        memcpy(&out, &v, sizeof(out));
        return true;
    }

    int SaveFile::ReadPouch(PouchItem* out, int max) const
    {
        const long names = Find(Crc32("PorchItem"));
        const long values = Find(Crc32("PorchItem_Value1"));
        const long equipped = Find(Crc32("PorchItem_EquipFlag"));
        if (names < 0 || values < 0)
            return 0;
        const size_t perName = PouchNameLength / 4;
        int count = 0;
        for (int slot = 0; slot < PouchSize && count < max; slot++)
        {
            const size_t first = static_cast<size_t>(names) + static_cast<size_t>(slot) * perName;
            if (first + perName > m_count || Hash(first) != Hash(static_cast<size_t>(names)))
                break;
            PouchItem item;
            // Each record holds 4 characters, in file order.
            for (size_t k = 0; k < perName; k++)
                memcpy(item.name + k * 4, m_records + (first + k) * RecordSize + 4, 4);
            item.name[PouchNameLength - 1] = '\0';
            if (item.name[0] == '\0')
                break;
            item.hash = Crc32(item.name);
            const size_t v = static_cast<size_t>(values) + static_cast<size_t>(slot);
            if (v < m_count && Hash(v) == Hash(static_cast<size_t>(values)))
            {
                const uint32_t raw = Value(v);
                memcpy(&item.value, &raw, sizeof(item.value));
            }
            if (equipped >= 0)
            {
                const size_t e = static_cast<size_t>(equipped) + static_cast<size_t>(slot);
                item.equipped = e < m_count && Hash(e) == Hash(static_cast<size_t>(equipped)) && Value(e) != 0;
            }
            out[count++] = item;
        }
        return count;
    }
}
