#include <cassert>
#include <cmath>
#include <cstdio>

#include "IconScale.h"

namespace
{
    // Pixels on the screen of an icon 100 texture pixels wide.
    float Screen(float zoom)
    {
        return 100.0f * IconScale::ForZoom(zoom) * zoom;
    }

    bool Near(float a, float b)
    {
        return std::fabs(a - b) < 0.01f;
    }
}

int main()
{
    // The overview: smaller, growing to the steady size at 0.9.
    assert(Screen(0.2f) < Screen(0.5f) && Screen(0.5f) < Screen(0.9f));
    // One size on the screen from 0.9 to 1.7 (42.5 pixels), as before zayr18.
    assert(Near(Screen(0.9f), 42.5f) && Near(Screen(1.2f), 42.5f) && Near(Screen(1.7f), 42.5f));
    // Growing with the map up to GrowthZoom (the world size of zayr17 and before: 0.25 per texture pixel).
    assert(Near(IconScale::ForZoom(2.0f), 0.25f) && Screen(2.5f) > Screen(2.0f));
    assert(Near(Screen(IconScale::GrowthZoom), 25.0f * IconScale::GrowthZoom));
    // ...and one size again from there, up to the largest zoom.
    assert(Near(Screen(5.0f), Screen(IconScale::GrowthZoom)) && Near(Screen(10.0f), Screen(IconScale::GrowthZoom)));
    // Never zero or negative, even with a zoom that makes no sense.
    assert(IconScale::ForZoom(0.0f) > 0.0f && IconScale::ForZoom(-1.0f) > 0.0f);
    std::printf("icon scale: ok\n");
    return 0;
}
