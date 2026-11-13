#pragma once

#include <cstdint>
#include <array>
#include <cstddef>
#include <string>

#include "ObjectTypes.h"

namespace ObjectModel
{
    constexpr size_t ObjectTypeCount = static_cast<size_t>(Data::ObjectType::Count);

    // For a type the map does not show (the locations, which only count for the map completion).
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
