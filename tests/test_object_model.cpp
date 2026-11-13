#include <cassert>
#include <string>

#include "ObjectModel.h"

int main()
{
    const char* expectedIcons[] = {
        "romfs:/korokseed.png", "romfs:/shrine.png", "romfs:/dlcshrine.png", "romfs:/village.png",
        "romfs:/hinox.png", "romfs:/talus.png", "romfs:/molduga.png",
        "romfs:/chest.png", "romfs:/chestspecial.png", "romfs:/wall.png"
    };
    // The locations have no legend category: the map does not show them.
    const int expectedLegendButtons[] = {0, 1, 1, ObjectModel::NoLegendButton, 2, 3, 4, 5, 6, 7};
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
