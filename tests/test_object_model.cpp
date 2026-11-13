#include <cassert>
#include <string>

#include "ObjectModel.h"

int main()
{
    const char* expectedIcons[] = {
        "romfs:/korokseed.png", "romfs:/shrine.png", "romfs:/dlcshrine.png", "romfs:/village.png",
        "romfs:/hinox.png", "romfs:/talus.png", "romfs:/molduga.png",
        "", "romfs:/chestarmor.png", "romfs:/wall.png"
    };
    // The locations and the ordinary chests have no legend category: the map does not show them. The special
    // chests have one per group.
    const int none = ObjectModel::NoLegendButton;
    const int expectedLegendButtons[] = {0, 1, 1, none, 2, 3, 4, none, none, 8};
    static_assert(sizeof(expectedIcons) / sizeof(expectedIcons[0]) == static_cast<size_t>(Data::ObjectType::Count), "");

    for (int value = 0; value < static_cast<int>(Data::ObjectType::Count); ++value)
    {
        const Data::ObjectType type = static_cast<Data::ObjectType>(value);
        assert(ObjectModel::IsValidType(type));
        assert(ObjectModel::TryGetMetadata(type) != nullptr);
        const ObjectModel::TypeMetadata& metadata = ObjectModel::GetMetadata(type);
        assert(std::string(metadata.iconPath) == expectedIcons[value]);
        assert(metadata.legendButtonIndex == expectedLegendButtons[value]);
    }
    assert(!ObjectModel::IsValidType(Data::ObjectType::Count));
    assert(ObjectModel::TryGetMetadata(Data::ObjectType::Count) == nullptr);
    return 0;
}
