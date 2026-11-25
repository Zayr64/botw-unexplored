#include "IconScale.h"

#include <algorithm>

float IconScale::ForZoom(float zoom)
{
    if (!(zoom > 0.0f))
        zoom = 0.2f;
    float overview = (zoom - 0.2f) / (0.9f - 0.2f);
    overview = std::max(0.0f, std::min(1.0f, overview));
    const float steady = (0.85f / zoom) * 0.5f * (0.55f + 0.45f * overview); // one size on the screen
    constexpr float minScale = 0.25f;                                       // growing with the map
    return std::max(steady, minScale * std::min(zoom, GrowthZoom) / zoom);
}
