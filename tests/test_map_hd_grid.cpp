#include <cassert>
#include <set>
#include <sstream>

#include "MapHdGrid.h"

int main()
{
    using namespace MapHdGrid;
    static_assert(Columns == 12 && Rows == 10, "12000 x 10000 in 1024-pixel chunks");

    // Chunks cover the map exactly, without gaps or overlaps
    float area = 0.0f;
    for (int index = 0; index < ChunkCount; ++index)
    {
        const Rect rect = ChunkRect(index);
        assert(rect.right > rect.left && rect.top > rect.bottom);
        assert(rect.left >= -1500.0f && rect.right <= 1500.0f && rect.bottom >= -1250.0f && rect.top <= 1250.0f);
        area += (rect.right - rect.left) * (rect.top - rect.bottom);
    }
    assert(area == 3000.0f * 2500.0f);

    // First chunk at the top-left corner, last one smaller at the bottom-right
    const Rect first = ChunkRect(0);
    assert(first.left == -1500.0f && first.top == 1250.0f && first.right == -1244.0f && first.bottom == 994.0f);
    const int last = ChunkCount - 1;
    assert(ChunkWidthPixels(last) == 12000 - 11 * 1024 && ChunkHeightPixels(last) == 10000 - 9 * 1024);
    const Rect lastRect = ChunkRect(last);
    assert(lastRect.right == 1500.0f && lastRect.bottom == -1250.0f);
    assert(ChunkFileName(0) == "00_00.jpg" && ChunkFileName(last) == "11_09.jpg" && ChunkFileName(13) == "01_01.jpg");
    assert(ChunkFileName(-1).empty() && ChunkFileName(ChunkCount).empty() && ChunkWidthPixels(ChunkCount) == 0);

    // Visible chunks: nearest to the view center first, and only the overlapping ones
    const std::vector<int> center = VisibleChunks(0.0f, 0.0f, 10.0f, 10.0f);
    assert(center.size() == 1 && center[0] == 4 * Columns + 5); // x -220..36, y -30..226
    const std::vector<int> meeting = VisibleChunks(36.0f, -30.0f, 10.0f, 10.0f);
    assert(meeting.size() == 4); // where columns 5, 6 and rows 4, 5 meet
    const std::vector<int> corner = VisibleChunks(-1499.0f, 1249.0f, 1.0f, 1.0f);
    assert(corner.size() == 1 && corner[0] == 0);
    const std::vector<int> wide = VisibleChunks(0.0f, 0.0f, 640.0f / 1.5f, 360.0f / 1.5f);
    assert(wide.size() <= 15);
    const Rect nearest = ChunkRect(wide[0]);
    assert(nearest.left <= 0.0f && nearest.right >= 0.0f && nearest.bottom <= 0.0f && nearest.top >= 0.0f);
    assert(std::set<int>(wide.begin(), wide.end()).size() == wide.size());
    assert(VisibleChunks(5000.0f, 0.0f, 10.0f, 10.0f).empty());

    assert(!IsUsableZoom(0.25f) && !IsUsableZoom(1.0f) && IsUsableZoom(1.5f) && IsUsableZoom(10.0f));

    std::istringstream good("BOTW_UNEXPLORED_MAP_HD\r\n1\n12000 10000 1024\n");
    assert(IsValidInfo(good));
    std::istringstream wrongSize("BOTW_UNEXPLORED_MAP_HD\n1\n6000 5000 1024\n");
    assert(!IsValidInfo(wrongSize));
    std::istringstream wrongVersion("BOTW_UNEXPLORED_MAP_HD\n99999999999999999999\n12000 10000 1024\n");
    assert(!IsValidInfo(wrongVersion));
    std::istringstream empty("");
    assert(!IsValidInfo(empty));

    // From the Album fewer chunks stay loaded, but more than a screen needs at the lowest zoom they show at
    assert(MaxLoadedChunks(true) == 40 && MaxLoadedChunks(false) == 24);
    assert(static_cast<int>(wide.size()) < MaxLoadedChunks(false));
    // ...and one more is loaded only while 32 MB stay free after its 8 MB: with the 60 MB the console has from the
    // Album once the map is up, some 20 halved chunks (about 1 MB each) fit, more than a screen needs
    assert(!HasMemoryForChunk(0) && !HasMemoryForChunk(39ull * 1024 * 1024));
    assert(HasMemoryForChunk(40ull * 1024 * 1024) && HasMemoryForChunk(60ull * 1024 * 1024) && HasMemoryForChunk(UINT64_MAX));
    assert((60 - 40) + 1 >= static_cast<int>(wide.size()));

    // From the Album each chunk is halved: 2 x 2 means, an odd edge alone
    assert(Reduction(true) == 1 && Reduction(false) == 2);
    // ...and loaded again at full size when zoomed in, where a screen holds at most 3 x 3 of them
    assert(WantedReduction(true, 1.5f) == 1 && WantedReduction(false, 1.5f) == 2 && WantedReduction(false, 2.4f) == 2);
    assert(WantedReduction(false, 2.5f) == 1 && WantedReduction(false, 10.0f) == 1);
    assert(VisibleChunks(36.0f, -30.0f, 640.0f / FullZoom, 360.0f / FullZoom).size() <= 9);
    assert(HalfSize(1024) == 512 && HalfSize(736) == 368 && HalfSize(784) == 392 && HalfSize(5) == 3);
    {
        // 3 x 2 RGB: the first output pixel is the mean of the 2 x 2 block, the second the last column alone
        const unsigned char pixels[3 * 2 * 3] = {0, 10, 255, 100, 20, 255, 7, 7, 7,
                                                 0, 30, 0,   100, 40, 0,   9, 9, 9};
        unsigned char out[2 * 1 * 3] = {};
        Halve(pixels, 3, 2, out);
        assert(out[0] == 50 && out[1] == 25 && out[2] == 128);
        assert(out[3] == 8 && out[4] == 8 && out[5] == 8);
        // Every chunk of the grid halves to whole pixels
        for (int index = 0; index < ChunkCount; ++index)
            assert(ChunkWidthPixels(index) % 2 == 0 && ChunkHeightPixels(index) % 2 == 0);
    }

    // Textures, in MB: from the Album a screen at FullZoom with 6 chunks at full size and 3 halved fits (27 MB, as on
    // 25/11), a 7th upgrade does not (it holds both textures for a moment); the 15 halved chunks of a screen at
    // MinZoom fit; 24 at full size (what piled up on 26/11 and showed black) are far over. As an application, the 40
    // chunks at full size fit: no limit of its own there.
    assert(TextureMegabytes(1) == 4 && TextureMegabytes(2) == 1);
    assert(TextureBudget(true) == MaxLoadedApplication * 4 && TextureBudget(false) == 30);
    assert(6 * TextureMegabytes(1) + 3 * TextureMegabytes(2) <= TextureBudget(false));
    assert(5 * TextureMegabytes(1) + 4 * TextureMegabytes(2) + TextureMegabytes(1) <= TextureBudget(false));
    assert(6 * TextureMegabytes(1) + 3 * TextureMegabytes(2) + TextureMegabytes(1) > TextureBudget(false));
    assert(static_cast<int>(wide.size()) * TextureMegabytes(2) <= TextureBudget(false));
    assert(MaxLoadedApplet * TextureMegabytes(1) > 3 * TextureBudget(false));

    // Eviction: the least recently used chunk, never one used in the current frame
    assert(PickEviction({{3, 10}, {7, 4}, {9, 8}}, 12) == 7);
    assert(PickEviction({{3, 12}, {7, 12}}, 12) == -1);
    assert(PickEviction({}, 1) == -1);
    return 0;
}
