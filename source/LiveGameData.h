#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Progress flags of a running BotW, read straight from the game's memory. Nothing is ever written.
//
// The game keeps every flag (the same ones game_data.sav stores) in ksys::gdt::Manager. A flag buffer
// (a TriggerParam) has one pointer table per flag type; each entry points at a Flag<T> object that starts
// with the vtable of its type and holds the name hash and the current value. The Manager has two buffers,
// kept in step every frame: one with the Flag<T> objects and one with small proxies that point at them
// (decomp: Manager::allocParam1). The one whose tables point at Flag<T> objects is used. The tables only
// tell which 64 KB pages hold flag objects; the objects themselves are recognised by their vtable, so the
// result does not depend on the order of the tables. Addresses and offsets are those of BotW 1.6.0
// (build id 8E9978D50BDD20B4), checked in the game's code; struct layouts from the zeldaret/botw decomp.
namespace LiveGameData
{
    // Whatever can read the game's memory: Atmosphere's cheat service on the console, a fake in the tests.
    class Memory
    {
    public:
        virtual ~Memory() {}
        virtual bool Read(uint64_t address, void* out, size_t size) = 0;
    };

    enum class FlagKind
    {
        Bool,     // value: 1 byte, bit 0
        S32,      // value: 4 bytes
        String32, // value: up to 32 characters, zero terminated
        Vector3f, // value: 3 floats
        Count
    };

    class Visitor
    {
    public:
        virtual ~Visitor() {}
        // `address` is where the value is in the game's memory, to read that one flag again later.
        virtual void OnFlag(FlagKind kind, uint32_t hash, const uint8_t* value, uint64_t address) = 0;
    };

    struct Layout
    {
        uint64_t managerPointer;   // main + this: ksys::gdt::Manager::sInstance
        uint64_t flagBufferOffset;  // Manager + this: mFlagBuffer (TriggerParam*), read by the game's getters
        uint64_t flagBuffer1Offset; // Manager + this: mFlagBuffer1 (TriggerParam*), written by its setters
        uint64_t flagBufferRef;     // Manager + this: mParam.mParam, which must point at one of the two
        uint64_t tablesOffset;     // TriggerParam + this: first sead::PtrArray {s32 size, s32 capacity, T** data}
        uint64_t playerPointer;    // main + this: pointer chain to Link's actor
        uint64_t playerActorOffset;
        uint64_t playerMatrixOffset; // sead::Matrix34f, row major, translation in the last column
        struct Kind
        {
            int table;          // index of its PtrArray in the TriggerParam
            uint64_t vtable;    // main + this
            uint32_t size;      // of the Flag<T> object
            uint32_t hashOffset;
            uint32_t valueOffset;
        } kinds[static_cast<int>(FlagKind::Count)];
    };

    extern const uint8_t BotwBuildId160[8];
    const Layout& Botw160();

    struct Report
    {
        bool ok = false;
        std::string error;   // one short sentence for the screen
        std::string details; // several lines for the log
        int tableSize[static_cast<int>(FlagKind::Count)] = {};
        int objects[static_cast<int>(FlagKind::Count)] = {};
        int pages = 0;
    };

    // Calls the visitor for every flag object of the four kinds. False (with report.error) when anything
    // looks different from what is expected; the visitor may already have been called by then.
    bool Scan(Memory& memory, uint64_t mainBase, const Layout& layout, Visitor& visitor, Report& report);

    // Link's position right now. False while the game is loading or Link does not exist.
    bool ReadPlayerPosition(Memory& memory, uint64_t mainBase, const Layout& layout, float position[3]);
}
