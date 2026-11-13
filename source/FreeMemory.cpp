#include "FreeMemory.h"

#include <malloc.h>
#include <unistd.h>

#include <switch.h>

// Not built by tools/hostview, which has its own (see host.cpp).
uint64_t FreeMemory::Bytes()
{
    if (!envHasHeapOverride())
        return Unknown;

    // The part of the heap malloc has not reached yet, plus what it holds free inside the part it has.
    const char* heapEnd = static_cast<const char*>(envGetHeapOverrideAddr()) + envGetHeapOverrideSize();
    const char* top = static_cast<const char*>(sbrk(0));
    uint64_t free = 0;
    if (top != reinterpret_cast<const char*>(-1) && heapEnd > top)
        free = static_cast<uint64_t>(heapEnd - top);
    const struct mallinfo info = mallinfo();
    free += static_cast<uint64_t>(static_cast<unsigned int>(info.fordblks));
    return free;
}
