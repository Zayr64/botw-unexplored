#include "QuestMap.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "ProgressData.h"

namespace
{
    const char* Header = "# BOTW_UNEXPLORED_QUESTS 1";
    std::vector<QuestMap::Entry> s_Entries;

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
    if (line != Header)
        return false;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const std::vector<std::string> f = SplitTabs(line);
        if (f.size() != 10 || (f[0] != "side" && f[0] != "shrine") || (f[1] != "0" && f[1] != "1"))
            continue;
        Entry entry;
        entry.shrine = f[0] == "shrine";
        entry.dlc = f[1] == "1";
        if (!ParseHash(f[2], entry.startHash) || !ParseHash(f[3], entry.finishHash) ||
            !ParseHash(f[4], entry.requiresHash) || !ParseFloat(f[5], entry.x) || !ParseFloat(f[6], entry.z) ||
            entry.startHash == 0 || f[7].empty())
            continue;
        entry.name = f[7];
        entry.location = f[8];
        entry.description = f[9];
        entries.push_back(entry);
    }
    return true;
}

bool QuestMap::Load(const std::string& path)
{
    s_Entries.clear();
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    std::ostringstream text;
    text << file.rdbuf();
    return Parse(text.str(), s_Entries);
}

const std::vector<QuestMap::Entry>& QuestMap::Entries()
{
    return s_Entries;
}

int QuestMap::RegionOf(uint32_t startHash)
{
    for (int i = 0; i < Data::QuestsCount; ++i)
        if (Data::Quests[i].startHash == startHash)
            return Data::Quests[i].region;
    return -1;
}
