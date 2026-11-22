#pragma once

#include <cstddef>
#include <cstdint>

// What the cooking app needs from BotW's game_data.sav: the pouch, rupees and a few flags.
//
// The file is a 0x0C byte header and then 8-byte records: the CRC32 of the flag's name and 4 bytes of value. Texts and
// arrays are several records in a row with the same hash (a 64-character text is 16 records). The records are sorted
// by hash, so a flag is found by binary search; a file that is not sorted is searched from the start instead.
namespace Compendium
{
    constexpr int PouchSize = 420;
    constexpr int PouchNameLength = 64;

    struct PouchItem
    {
        uint32_t hash = 0;              // CRC32 of the actor name
        char name[PouchNameLength] = {};
        int32_t value = 0;              // count; durability x 100 for weapons; dye colour for armor
        bool equipped = false;
    };

    class SaveFile
    {
    public:
        // `data` must stay alive while this object is used.
        bool Load(const uint8_t* data, size_t size);

        bool GetS32(uint32_t hash, int32_t& out) const;
        bool GetS32(const char* name, int32_t& out) const;
        bool GetBool(uint32_t hash, bool& out) const;
        bool GetBool(const char* name, bool& out) const;
        bool GetF32(const char* name, float& out) const;
        // Whether the save has this flag at all.
        bool Has(uint32_t hash) const;

        // The pouch, in the game's order, up to the first empty slot. Returns the number of items.
        int ReadPouch(PouchItem* out, int max) const;

    private:
        // Index of the first record with this hash, or -1.
        long Find(uint32_t hash) const;
        uint32_t Hash(size_t record) const;
        uint32_t Value(size_t record) const;

        const uint8_t* m_records = nullptr;
        size_t m_count = 0;
        bool m_sorted = false;
    };
}
