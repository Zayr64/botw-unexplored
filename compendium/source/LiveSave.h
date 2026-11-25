#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "LiveGameData.h"

// A game_data.sav made from the flags of the running game (read by LiveGameData), so that the rest of the app reads
// it exactly like a save: one record per bool, s32 and f32 flag, and the pouch's three arrays in the save's layout (a
// 64-character name is 16 records of 4 characters; the elements of an array follow each other under one hash).
namespace Compendium
{
    // The arrays to ask LiveGameData for: PorchItem, PorchItem_Value1, PorchItem_EquipFlag.
    const uint32_t* LiveArrays();
    int LiveArrayCount();

    class LiveSave : public LiveGameData::Visitor
    {
    public:
        void OnFlag(LiveGameData::FlagKind kind, uint32_t hash, const uint8_t* value, uint64_t address) override;
        void OnArrayElement(LiveGameData::FlagKind kind, uint32_t hash, int index, const uint8_t* value) override;

        // The save file. False (with `why`, one short sentence) when the game has not loaded a save yet or the pouch
        // was not read whole.
        bool Build(std::vector<uint8_t>& out, std::string& why) const;

        size_t RecordCount() const { return m_records.size(); }

    private:
        struct Record
        {
            uint32_t hash;
            uint32_t value;
        };
        std::vector<Record> m_records;
        int m_arrayElements[3] = {}; // per pouch array, in LiveArrays() order
        uint32_t m_playtime = 0;
    };
}
