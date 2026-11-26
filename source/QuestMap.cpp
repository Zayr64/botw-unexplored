#include "QuestMap.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "ProgressData.h"

namespace
{
    const char* HeaderV1 = "# BOTW_UNEXPLORED_QUESTS 1";
    const char* HeaderV2 = "# BOTW_UNEXPLORED_QUESTS 2";
    std::vector<QuestMap::Entry> s_Entries;
    std::vector<uint32_t> s_Flags;

    std::vector<std::string> SplitTabs(const std::string& line)
    {
        std::vector<std::string> fields;
        size_t start = 0;
        for (;;)
        {
            const size_t tab = line.find('\t', start);
            fields.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
            if (tab == std::string::npos)
                return fields;
            start = tab + 1;
        }
    }

    bool ParseHash(const std::string& text, uint32_t& value)
    {
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
        if (text.empty() || *end != '\0' || parsed > 0xFFFFFFFFul)
            return false;
        value = static_cast<uint32_t>(parsed);
        return true;
    }

    bool ParseFloat(const std::string& text, float& value)
    {
        char* end = nullptr;
        value = std::strtof(text.c_str(), &end);
        return !text.empty() && *end == '\0';
    }

    // "x,z,offHash;..." (or empty) into markers; false if any does not read.
    bool ParseMarkers(const std::string& text, std::vector<QuestMap::Marker>& markers)
    {
        markers.clear();
        if (text.empty())
            return true;
        std::istringstream list(text);
        std::string item;
        while (std::getline(list, item, ';'))
        {
            const size_t a = item.find(','), b = a == std::string::npos ? a : item.find(',', a + 1);
            QuestMap::Marker marker;
            if (b == std::string::npos || !ParseFloat(item.substr(0, a), marker.x) ||
                !ParseFloat(item.substr(a + 1, b - a - 1), marker.z) || !ParseHash(item.substr(b + 1), marker.offHash))
                return false;
            if (static_cast<int>(markers.size()) < QuestMap::MaxMarkers)
                markers.push_back(marker);
        }
        return true;
    }
}

bool QuestMap::Parse(const std::string& text, std::vector<Entry>& entries)
{
    entries.clear();
    std::istringstream input(text);
    std::string line;
    if (!std::getline(input, line))
        return false;
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    const bool v2 = line == HeaderV2;
    if (line != HeaderV1 && !v2)
        return false;
    bool lastKept = false; // whether the steps that follow belong to a quest that was read
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const std::vector<std::string> f = SplitTabs(line);
        if (v2 && !f.empty() && f[0] == "step")
        {
            Step step;
            if (lastKept && f.size() == 5 && ParseHash(f[1], step.flagHash) && (f[2] == "0" || f[2] == "1") &&
                ParseMarkers(f[3], step.markers))
            {
                step.ends = f[2] == "1";
                step.text = f[4];
                entries.back().steps.push_back(step);
            }
            continue;
        }
        lastKept = false;
        if (f.size() != 10 || (f[0] != "side" && f[0] != "shrine" && !(v2 && f[0] == "main")) ||
            (f[1] != "0" && f[1] != "1"))
            continue;
        Entry entry;
        entry.shrine = f[0] == "shrine";
        entry.main = f[0] == "main";
        entry.dlc = f[1] == "1";
        if (!ParseHash(f[2], entry.startHash) || !ParseHash(f[3], entry.finishHash) ||
            !ParseHash(f[4], entry.requiresHash) || !ParseFloat(f[5], entry.x) || !ParseFloat(f[6], entry.z) ||
            entry.startHash == 0 || f[7].empty())
            continue;
        entry.name = f[7];
        entry.location = f[8];
        entry.description = f[9];
        entries.push_back(entry);
        lastKept = true;
    }
    return true;
}

bool QuestMap::Load(const std::string& path)
{
    s_Entries.clear();
    s_Flags.clear();
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    std::ostringstream text;
    text << file.rdbuf();
    if (!Parse(text.str(), s_Entries))
        return false;
    for (const Entry& entry : s_Entries)
    {
        s_Flags.insert(s_Flags.end(), {entry.startHash, entry.finishHash, entry.requiresHash});
        for (const Step& step : entry.steps)
        {
            s_Flags.push_back(step.flagHash);
            for (const Marker& marker : step.markers)
                s_Flags.push_back(marker.offHash);
        }
    }
    std::sort(s_Flags.begin(), s_Flags.end());
    s_Flags.erase(std::unique(s_Flags.begin(), s_Flags.end()), s_Flags.end());
    if (!s_Flags.empty() && s_Flags.front() == 0)
        s_Flags.erase(s_Flags.begin());
    return true;
}

const std::vector<uint32_t>& QuestMap::Flags()
{
    return s_Flags;
}

const std::vector<QuestMap::Entry>& QuestMap::Entries()
{
    return s_Entries;
}

int QuestMap::CurrentStep(const Entry& entry, const std::function<bool(uint32_t)>& isSet)
{
    if (entry.steps.empty() || !isSet(entry.startHash) || (entry.finishHash != 0 && isSet(entry.finishHash)))
        return -1;
    int current = 0; // taken: at the first step at least
    for (int i = static_cast<int>(entry.steps.size()) - 1; i > 0; --i)
    {
        if (entry.steps[i].flagHash != 0 && isSet(entry.steps[i].flagHash))
        {
            current = i;
            break;
        }
    }
    return entry.steps[current].ends ? -1 : current;
}

bool QuestMap::IsDone(const Entry& entry, const std::function<bool(uint32_t)>& isSet)
{
    if (entry.finishHash != 0 && isSet(entry.finishHash))
        return true;
    if (entry.steps.empty() || !isSet(entry.startHash))
        return false;
    for (const Step& step : entry.steps)
        if (step.ends && step.flagHash != 0 && isSet(step.flagHash))
            return true;
    return false;
}

std::vector<QuestMap::Marker> QuestMap::ActiveMarkers(const Entry& entry, int step,
                                                      const std::function<bool(uint32_t)>& isSet)
{
    std::vector<Marker> markers;
    if (step < 0 || step >= static_cast<int>(entry.steps.size()))
        return markers;
    for (const Marker& marker : entry.steps[step].markers)
        if (marker.offHash == 0 || !isSet(marker.offHash))
            markers.push_back(marker);
    return markers;
}

int QuestMap::RegionOf(uint32_t startHash)
{
    for (int i = 0; i < Data::QuestsCount; ++i)
        if (Data::Quests[i].startHash == startHash)
            return Data::Quests[i].region;
    return -1;
}
