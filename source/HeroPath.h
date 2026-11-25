#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The Hero's Path: where Link has walked, as the game keeps it in the save's tracker/trackblockNN.sav files.
//
// Each file has a 64-byte header and up to 48 records of 1216 bytes: the play time of the record at +4 and up to 300
// points of 4 bytes from +16 (all of them but in the last record, whose count is the u16 at +4 of the header; the
// index of the last record is the byte at +2). A point is x (14 bits, signed as 8192 - value from 8192), z (13 bits,
// the same from 4096) in the game's units, and 5 flag bits: 0x10 on the world map (else in a shrine or another map,
// or 0x1F for no point), 0x01 a warp before it, 0x02 / 0x04 a warp / a death after it. The records are reused once the
// history is full, so they are put in order by their time, not by file. The format is from the zephenryus gist and
// savage13/heros_path (BSD-2), the rules for breaking the line from the BotW-TotK Atlas (jinghaihan, MIT).
namespace HeroPath
{
    constexpr size_t FileSize = 65536;
    constexpr int MaxFiles = 30;

    struct Record
    {
        uint32_t time = 0;
        std::vector<uint32_t> words;
    };

    // The records of one trackblock file. False (with `error`) for a file that is not one; an empty file (all zero)
    // is fine and adds nothing.
    bool ReadFile(const uint8_t* data, size_t size, std::vector<Record>& records, std::string& error);

    struct Point
    {
        float x; // the game's x
        float z; // the game's z (north is negative)
    };

    enum class End : uint8_t
    {
        None,
        Warp,
        Death
    };

    // A stretch of path without a jump: warps, deaths, time inside shrines and gaps of more than 500 units split it.
    struct Segment
    {
        std::vector<Point> points;
        End end = End::None;
    };

    struct Path
    {
        std::vector<Segment> segments;
        size_t points = 0;
    };

    // The path from the records of every file, in the order of their time.
    Path Build(std::vector<Record> records);

    // How far the path goes, in the game's units (metres), not counting the jumps between segments.
    double Length(const Path& path);
}
