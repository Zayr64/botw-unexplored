#pragma once

// The size of the map's icons at each zoom: the scale MapObject applies to an icon's texture, in map units per
// texture pixel (the screen size is that times the zoom).
//
// On the overview, where hundreds of icons pile up, they shrink to a bit more than half. Up to zoom 1.7 they keep one
// size on the screen; from there they grow with the map, and from GrowthZoom on they keep one size again. Until zayr18
// they grew without end: 5.9 times at the largest zoom (10), where a chest took 150 pixels and hid the place it marks.
namespace IconScale
{
    constexpr float GrowthZoom = 3.0f;

    float ForZoom(float zoom);
}
