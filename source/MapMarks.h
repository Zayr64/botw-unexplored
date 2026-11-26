#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "LiveGameData.h"

// zayr32 (a test asked for by the user on 26/11): a mark on the running game's own map, where an object of this app is.
//
// The game keeps the stamps and pins the player puts on its map in two array flags of 200 (the same ones
// game_data.sav stores): MapApp_MapIconNo, the icon of each slot (-1 when empty), and MapApp_MapIconPos, where it is
// (x -100000 when empty). The game's code (1.6.0) skips a slot whose icon is over 44 or whose x is -100000 or less;
// its map offers the stamps 27 to 35 and the five pins 37 to 41 (one of each). Seen in DualScreen-Launcher's BotW
// map reader (JoeCorrell, read from the game's code) and in Marc Robledo's save editor, which adds stamps this way.
//
// Every flag is in two places (see LiveGameData.h): a Flag<T> object, which the game's getters read, and a FlagProxy<T>
// with its own copy of the value, which its setters write and which the save is made from (decomp: gdtFlagProxy.h,
// TriggerParamRef, Manager::syncUpdate). The game copies a change from the proxies to the flags only for the flags its
// setters recorded, so a mark is written to both.
namespace MapMarks
{
    constexpr uint32_t IconNoHash = 0x9383490E;  // MapApp_MapIconNo
    constexpr uint32_t IconPosHash = 0xEA9DEF3F; // MapApp_MapIconPos
    constexpr int32_t EmptyIcon = -1;
    constexpr float EmptyX = -100000.0f;
    constexpr int32_t FirstStamp = 27, LastStamp = 35, FirstPin = 37, LastPin = 41;

    struct Mark
    {
        int slot;
        int32_t icon;
        float position[3];
    };

    // A slot as the game holds it: the flag the game reads and the copy it saves.
    struct Slot
    {
        int32_t icon = EmptyIcon, proxyIcon = EmptyIcon;
        float position[3] = {EmptyX, 0.0f, 0.0f}, proxyPosition[3] = {EmptyX, 0.0f, 0.0f};
    };

    // Where the two arrays and their copies are, and what they hold.
    struct State
    {
        std::vector<uint64_t> flagIcons, flagPositions, proxyIcons, proxyPositions; // the objects, by slot
        std::vector<Slot> slots;
        bool pinUsed[LastPin - FirstPin + 1] = {};
        uint32_t valueOffsets[2] = {0, 0}; // of the value in Flag<s32> and Flag<Vector3f>
    };

    struct Result
    {
        bool ok = false;
        std::string error;        // one short sentence for the screen
        std::string details;      // several lines for the log
        std::vector<Mark> before; // the marks on the map before
        int slot = -1;            // where the new one went
        int32_t icon = EmptyIcon;
    };

    // Puts a mark on the game's map at `position` (game coordinates x, y, z): `icon`, or with -1 the first pin that is
    // not on the map. Not where the map already has a mark (2 units around, on the ground). Both copies are written,
    // the position before the icon, and read back. Nothing is written when anything looks different from what is
    // expected; a write that fails half way is undone as far as possible.
    bool Add(LiveGameData::Memory& memory, uint64_t mainBase, const LiveGameData::Layout& layout, int32_t icon,
             const float position[3], Result& result);

    // Finds and reads both arrays and their copies (result.before: the marks shown; result.details: every slot used).
    bool Load(LiveGameData::Memory& memory, uint64_t mainBase, const LiveGameData::Layout& layout, State& state,
              Result& result);

    // zayr33 (test): read only. Looks in the game's heap for the place of each mark on the map (its x with its z
    // near) outside the flags, to find the list the map screen draws from, which the game fills from the flags
    // when an area loads (a pin written to the flags shows after the next loading). Every hit goes to result.details
    // with the memory around it and the pointers into the game's code before it (the vtable of the object).
    bool Probe(LiveGameData::Memory& memory, uint64_t mainBase, const LiveGameData::Layout& layout, uint64_t heapBase,
               uint64_t heapSize, Result& result);
}
