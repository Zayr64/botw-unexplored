#include "ShrineChests.h"

#include <algorithm>

const Data::ShrineChestList* ShrineChests::Find(uint32_t locationHash)
{
    const Data::ShrineChestList* begin = Data::ShrineChestLists;
    const Data::ShrineChestList* end = begin + Data::ShrineChestListsCount;
    const Data::ShrineChestList* found = std::lower_bound(begin, end, locationHash,
        [](const Data::ShrineChestList& list, uint32_t hash) { return list.locationHash < hash; });
    return found != end && found->locationHash == locationHash ? found : nullptr;
}

int ShrineChests::CountOpened(const Data::ShrineChestList& list, const uint32_t* openedFlags, int openedCount)
{
    int opened = 0;
    for (int i = 0; i < list.count; ++i)
        opened += std::binary_search(openedFlags, openedFlags + openedCount, Data::ShrineChestFlags[list.first + i]);
    return opened;
}
