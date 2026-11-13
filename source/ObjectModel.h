#pragma once

#include <cstdint>
#include <array>
#include <cstddef>
#include <string>

#include "ObjectTypes.h"

namespace ObjectModel
{
    constexpr size_t ObjectTypeCount = static_cast<size_t>(Data::ObjectType::Count);

    // For a type with no legend category of its own: the locations and the ordinary chests, which the map does
    // not show, and the special chests, whose category is that of their group.
    constexpr int NoLegendButton = -1;

    struct TypeMetadata
    {
        const char* iconPath;
        bool hasName;
        int legendButtonIndex;
    };

    const TypeMetadata* TryGetMetadata(Data::ObjectType type);
    const TypeMetadata& GetMetadata(Data::ObjectType type);
    bool IsValidType(Data::ObjectType type);
}
