#pragma once

#include <cstdint>

namespace FreeMemory
{
    constexpr uint64_t Unknown = UINT64_MAX;

    // Bytes the app can still allocate: what is left of the heap the homebrew loader gave it. Textures come
    // out of the same heap. Unknown when the loader did not say how large the heap is.
    // Walks the heap, so it is not something to call every frame.
    uint64_t Bytes();
}
